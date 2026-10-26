// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/connectors/remote_transport.h"

#include <utility>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace uw {
namespace {

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("uw_connector", R"(
      semantics {
        sender: "uw connectors"
        description:
          "Talks MCP to a web connector the user added: lists its tools and "
          "prompts, and runs the ones uw's agent calls in a chat."
        trigger: "uw starts with the connector on, the user adds or turns it "
          "on, or the agent calls one of its tools."
        data:
          "What the agent passes to the connector's tools, and the key or "
          "sign-in the user gave the connector."
        destination: OTHER
        destination_other: "The connector's address, which the user added."
      }
      policy {
        cookies_allowed: NO
        setting: "No connector runs until the user adds one."
        policy_exception_justification: "Not implemented."
      })");

// Longest answer uw reads; a screenshot can be several megabytes.
constexpr size_t kMaxAnswer = 32 * 1024 * 1024;

// A header value as MCP sends one: as it is when it is plain ASCII, or
// base64 between "=?base64?" and "?=".
std::string HeaderValue(std::string_view value) {
  const bool plain =
      !value.empty() && value.front() != ' ' && value.back() != ' ' &&
      std::ranges::all_of(value,
                          [](char c) { return c >= 0x20 && c <= 0x7e; }) &&
      !(base::StartsWith(value, "=?base64?") && base::EndsWith(value, "?="));
  return plain ? std::string(value)
               : base::StrCat({"=?base64?", base::Base64Encode(value), "?="});
}

// Where the server says to read how to sign in, from its WWW-Authenticate
// header.
std::string ResourceMetadata(const net::HttpResponseHeaders& headers) {
  std::optional<std::string> value =
      headers.GetNormalizedHeader("WWW-Authenticate");
  if (!value) {
    return std::string();
  }
  constexpr std::string_view kKey = "resource_metadata=\"";
  const size_t start = value->find(kKey);
  if (start == std::string::npos) {
    return std::string();
  }
  const size_t from = start + kKey.size();
  const size_t end = value->find('"', from);
  return end == std::string::npos ? std::string()
                                  : value->substr(from, end - from);
}

}  // namespace

struct RemoteTransport::Request {
  std::unique_ptr<network::SimpleURLLoader> loader;
  Done done;
  bool initialize = false;
};

RemoteTransport::RemoteTransport(
    GURL url,
    std::map<std::string, std::string> headers,
    scoped_refptr<network::SharedURLLoaderFactory> loader,
    Token token)
    : url_(std::move(url)),
      headers_(std::move(headers)),
      loader_(std::move(loader)),
      token_(std::move(token)) {}

RemoteTransport::~RemoteTransport() = default;

void RemoteTransport::Send(int id,
                           std::string_view version,
                           mcp::Era era,
                           std::string_view method,
                           std::string_view name,
                           std::string body,
                           Done done) {
  Post(id, version, era, method, name, std::move(body), std::move(done));
}

void RemoteTransport::Notify(std::string body) {
  Post(next_notification_--, legacy_version_, mcp::Era::kLegacy,
       "notifications/initialized", {}, std::move(body), base::DoNothing());
}

void RemoteTransport::Forget(int id) {
  requests_.erase(id);
}

void RemoteTransport::Post(int id,
                           std::string_view version,
                           mcp::Era era,
                           std::string_view method,
                           std::string_view name,
                           std::string body,
                           Done done) {
  auto resource = std::make_unique<network::ResourceRequest>();
  resource->url = url_;
  resource->method = "POST";
  resource->credentials_mode = network::mojom::CredentialsMode::kOmit;
  net::HttpRequestHeaders& headers = resource->headers;
  for (const auto& [key, value] : headers_) {
    headers.SetHeader(key, value);
  }
  if (std::string token = token_ ? token_.Run() : std::string();
      !token.empty()) {
    headers.SetHeader(net::HttpRequestHeaders::kAuthorization,
                      base::StrCat({"Bearer ", token}));
  }
  headers.SetHeader(net::HttpRequestHeaders::kAccept,
                    "application/json, text/event-stream");
  if (!version.empty()) {
    headers.SetHeader("MCP-Protocol-Version", version);
  }
  if (era == mcp::Era::kModern) {
    headers.SetHeader("Mcp-Method", method);
    if (!name.empty()) {
      headers.SetHeader("Mcp-Name", HeaderValue(name));
    }
  } else if (!session_.empty()) {
    headers.SetHeader("Mcp-Session-Id", session_);
  }

  auto request = std::make_unique<Request>();
  request->done = std::move(done);
  request->initialize = method == "initialize";
  request->loader =
      network::SimpleURLLoader::Create(std::move(resource), kTrafficAnnotation);
  request->loader->SetAllowHttpErrorResults(true);
  request->loader->SetOnRedirectCallback(base::BindRepeating(
      [](base::WeakPtr<RemoteTransport> self, int id, const GURL&,
         const net::RedirectInfo&, const network::mojom::URLResponseHead&,
         std::vector<std::string>*) {
        if (self) {
          self->OnRedirect(id);
        }
      },
      weak_factory_.GetWeakPtr(), id));
  request->loader->AttachStringForUpload(std::move(body), "application/json");
  request->loader->DownloadToString(
      loader_.get(),
      base::BindOnce(&RemoteTransport::OnComplete, weak_factory_.GetWeakPtr(),
                     id),
      kMaxAnswer);
  requests_[id] = std::move(request);
}

// Runs inside the loader, so the answer goes out from a fresh task.
void RemoteTransport::OnRedirect(int id) {
  auto request = requests_.find(id);
  if (request == requests_.end()) {
    return;
  }
  request->second->loader.reset();
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&RemoteTransport::Finish, weak_factory_.GetWeakPtr(), id,
                     McpReply::Problem("The connector's address redirects "
                                       "elsewhere. Add the address it goes "
                                       "to instead.")));
}

void RemoteTransport::OnComplete(int id, std::optional<std::string> body) {
  auto found = requests_.find(id);
  if (found == requests_.end()) {
    return;
  }
  Request& request = *found->second;
  const int net_error = request.loader->NetError();
  const network::mojom::URLResponseHead* head = request.loader->ResponseInfo();
  const int status = head && head->headers ? head->headers->response_code() : 0;
  McpReply reply;
  if (!head || (net_error != net::OK && status == 0)) {
    reply.problem = base::StrCat({"uw couldn't reach ", url_.host(), ": ",
                                  net::ErrorToShortString(net_error)});
    Finish(id, std::move(reply));
    return;
  }
  if (status == 401) {
    reply.needs_sign_in = true;
    reply.http_status = status;
    reply.resource_metadata = ResourceMetadata(*head->headers);
    reply.problem = "The connector asks you to sign in.";
    Finish(id, std::move(reply));
    return;
  }
  if (request.initialize) {
    if (std::optional<std::string> session =
            head->headers->GetNormalizedHeader("Mcp-Session-Id")) {
      session_ = *session;
    }
  }
  const std::string text = std::move(body).value_or(std::string());
  if (head->mime_type == "text/event-stream") {
    for (mcp::Message& message : mcp::ParseEventStream(text)) {
      if (message.id == id) {
        reply.message = std::move(message);
        break;
      }
    }
  } else {
    reply.message = mcp::ParseMessage(text);
  }
  if (request.initialize && reply.message) {
    if (const std::string* version =
            reply.message->result.FindString("protocolVersion")) {
      legacy_version_ = *version;
    }
  }
  if (status < 200 || status >= 300) {
    reply.http_status = status;
    if (!reply.message) {
      reply.problem = base::StrCat(
          {"The connector answered HTTP ", base::NumberToString(status), "."});
    }
  } else if (!reply.message) {
    reply.problem = "The connector's answer couldn't be read.";
  }
  Finish(id, std::move(reply));
}

void RemoteTransport::Finish(int id, McpReply reply) {
  auto request = requests_.find(id);
  if (request == requests_.end()) {
    return;
  }
  Done done = std::move(request->second->done);
  requests_.erase(request);
  std::move(done).Run(std::move(reply));
}

}  // namespace uw
