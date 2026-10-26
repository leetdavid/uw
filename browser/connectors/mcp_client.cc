// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/connectors/mcp_client.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"

namespace uw {
namespace {

// A connector that runs with npx may download itself before it answers.
constexpr base::TimeDelta kStartTimeout = base::Seconds(90);
constexpr base::TimeDelta kListTimeout = base::Seconds(60);
// A list longer than this many pages is cut there.
constexpr int kMaxPages = 20;

McpReply Failed(McpReply reply, std::string_view what) {
  if (reply.problem.empty()) {
    reply.problem =
        reply.message && reply.message->error
            ? base::StrCat({what, ": ", reply.message->error->message})
            : std::string(what);
  }
  return reply;
}

}  // namespace

McpReply::McpReply() = default;
McpReply::McpReply(McpReply&&) = default;
McpReply& McpReply::operator=(McpReply&&) = default;
McpReply::~McpReply() = default;

// static
McpReply McpReply::Problem(std::string problem) {
  McpReply reply;
  reply.problem = std::move(problem);
  return reply;
}

McpClient::Listing::Listing() = default;
McpClient::Listing::Listing(Listing&&) = default;
McpClient::Listing& McpClient::Listing::operator=(Listing&&) = default;
McpClient::Listing::~Listing() = default;

McpClient::McpClient(std::unique_ptr<McpTransport> transport)
    : transport_(std::move(transport)) {}

McpClient::~McpClient() = default;

void McpClient::Connect(std::optional<mcp::Era> era, Connected done) {
  if (era == mcp::Era::kLegacy) {
    Initialize(std::move(done));
    return;
  }
  Send(mcp::Era::kModern, "server/discover", {}, {}, kStartTimeout,
       base::BindOnce(&McpClient::OnDiscover, weak_factory_.GetWeakPtr(),
                      std::move(done)));
}

void McpClient::List(Connected done) {
  Listing listing;
  listing.era = era_;
  listing.version = version_;
  ListTools(std::move(listing), std::nullopt, std::move(done));
}

void McpClient::Request(std::string_view method,
                        base::DictValue params,
                        std::string_view name,
                        base::TimeDelta timeout,
                        McpTransport::Done done) {
  Send(era_, method, std::move(params), name, timeout, std::move(done));
}

void McpClient::Send(mcp::Era era,
                     std::string_view method,
                     base::DictValue params,
                     std::string_view name,
                     base::TimeDelta timeout,
                     McpTransport::Done done) {
  const int id = next_id_++;
  Pending& pending = pending_[id];
  pending.done = std::move(done);
  pending.timer = std::make_unique<base::OneShotTimer>();
  pending.timer->Start(
      FROM_HERE, timeout,
      base::BindOnce(&McpClient::OnTimeout, weak_factory_.GetWeakPtr(), id));
  const std::string_view version =
      era == mcp::Era::kModern ? mcp::kModernVersion
      : method == "initialize" ? std::string_view()
                               : std::string_view(version_);
  transport_->Send(
      id, version, era, method, name,
      mcp::RequestBody(id, era, method, std::move(params)),
      base::BindOnce(&McpClient::OnReply, weak_factory_.GetWeakPtr(), id));
}

void McpClient::OnReply(int id, McpReply reply) {
  auto pending = pending_.find(id);
  if (pending == pending_.end()) {
    return;
  }
  McpTransport::Done done = std::move(pending->second.done);
  pending_.erase(pending);
  std::move(done).Run(std::move(reply));
}

void McpClient::OnTimeout(int id) {
  transport_->Forget(id);
  McpReply reply = McpReply::Problem("The connector didn't answer in time.");
  reply.timed_out = true;
  OnReply(id, std::move(reply));
}

void McpClient::OnDiscover(Connected done, McpReply reply) {
  if (reply.needs_sign_in) {
    std::move(done).Run(base::unexpected(std::move(reply)));
    return;
  }
  if (reply.message && !reply.message->error) {
    const base::ListValue* versions =
        reply.message->result.FindList("supportedVersions");
    if (!versions ||
        std::ranges::find(*versions, base::Value(mcp::kModernVersion)) ==
            versions->end()) {
      std::move(done).Run(base::unexpected(McpReply::Problem(
          "This connector speaks a newer MCP than uw does.")));
      return;
    }
    era_ = mcp::Era::kModern;
    version_ = mcp::kModernVersion;
    List(std::move(done));
    return;
  }
  if (reply.message && mcp::IsModernError(*reply.message->error)) {
    std::move(done).Run(base::unexpected(McpReply::Problem(
        base::StrCat({"This connector speaks MCP ",
                      base::JoinString(reply.message->error->supported, ", "),
                      ", which uw doesn't."}))));
    return;
  }
  // A program that stopped, or a web address that answered nothing, is no
  // sign of a legacy server. Silence is: some never answer a request before
  // initialize.
  if (!reply.message && reply.http_status == 0 && !reply.timed_out) {
    std::move(done).Run(base::unexpected(std::move(reply)));
    return;
  }
  Initialize(std::move(done));
}

void McpClient::Initialize(Connected done) {
  Send(mcp::Era::kLegacy, "initialize", mcp::InitializeParams(), {},
       kStartTimeout,
       base::BindOnce(&McpClient::OnInitialize, weak_factory_.GetWeakPtr(),
                      std::move(done)));
}

void McpClient::OnInitialize(Connected done, McpReply reply) {
  if (!reply.message || reply.message->error) {
    std::move(done).Run(base::unexpected(
        Failed(std::move(reply), "The connector refused to start")));
    return;
  }
  era_ = mcp::Era::kLegacy;
  const std::string* version =
      reply.message->result.FindString("protocolVersion");
  version_ = version ? *version : mcp::kLegacyVersion;
  transport_->Notify(mcp::InitializedNotification());
  List(std::move(done));
}

void McpClient::ListTools(Listing listing,
                          std::optional<std::string> cursor,
                          Connected done) {
  base::DictValue params;
  if (cursor) {
    params.Set("cursor", *cursor);
  }
  Request("tools/list", std::move(params), {}, kListTimeout,
          base::BindOnce(&McpClient::OnTools, weak_factory_.GetWeakPtr(),
                         std::move(listing), std::move(done)));
}

void McpClient::OnTools(Listing listing, Connected done, McpReply reply) {
  if (!reply.message) {
    std::move(done).Run(
        base::unexpected(Failed(std::move(reply), "Listing its tools failed")));
    return;
  }
  // A connector without tools may refuse the request; it simply has none.
  if (!reply.message->error) {
    for (mcp::Tool& tool : mcp::ParseTools(reply.message->result)) {
      listing.tools.push_back(std::move(tool));
    }
    std::optional<std::string> next = mcp::NextCursor(reply.message->result);
    if (next && listing.tools.size() < kMaxPages * 100u) {
      ListTools(std::move(listing), std::move(next), std::move(done));
      return;
    }
  }
  ListPrompts(std::move(listing), std::nullopt, std::move(done));
}

void McpClient::ListPrompts(Listing listing,
                            std::optional<std::string> cursor,
                            Connected done) {
  base::DictValue params;
  if (cursor) {
    params.Set("cursor", *cursor);
  }
  Request("prompts/list", std::move(params), {}, kListTimeout,
          base::BindOnce(&McpClient::OnPrompts, weak_factory_.GetWeakPtr(),
                         std::move(listing), std::move(done)));
}

void McpClient::OnPrompts(Listing listing, Connected done, McpReply reply) {
  if (reply.message && !reply.message->error) {
    for (mcp::Prompt& prompt : mcp::ParsePrompts(reply.message->result)) {
      listing.prompts.push_back(std::move(prompt));
    }
    std::optional<std::string> next = mcp::NextCursor(reply.message->result);
    if (next && listing.prompts.size() < kMaxPages * 100u) {
      ListPrompts(std::move(listing), std::move(next), std::move(done));
      return;
    }
  }
  std::move(done).Run(std::move(listing));
}

}  // namespace uw
