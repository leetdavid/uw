// Copyright 2026 The uw Authors. All rights reserved.

// The terminal tools of AgentTools: run, screen, and keys.

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/fixed_flat_map.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "uw/browser/agent/agent_tools.h"
#include "uw/browser/terminal/terminal_shell.h"
#include "uw/browser/terminal/terminal_tab.h"

namespace uw {
namespace {

// Claude Code's waits: 2 minutes, or up to 10 when the agent asks.
constexpr base::TimeDelta kDefaultWait = base::Minutes(2);
constexpr base::TimeDelta kMaxWait = base::Minutes(10);
// How often a wait looks at the shell.
constexpr base::TimeDelta kCheckEvery = base::Milliseconds(100);
// Output that has stopped for this long has settled.
constexpr base::TimeDelta kQuiet = base::Milliseconds(250);
// A quick command, such as cd, can be back at its prompt before a check sees
// it leave.
constexpr base::TimeDelta kQuickCommand = base::Milliseconds(500);
// A login shell reads the user's profile before its first prompt.
constexpr base::TimeDelta kStartLimit = base::Minutes(1);
// How long keys wait for a program to answer them.
constexpr base::TimeDelta kKeysLimit = base::Seconds(5);
// What Ctrl-C sends.
constexpr char kCtrlC[] = "\x03";
// Long output keeps this many lines of its start and of its end.
constexpr size_t kHeadLines = 50;
constexpr size_t kTailLines = 150;
// About a long page's reading.
constexpr size_t kMaxOutput = 30000;
// What screen and keys answer with: the screen and the scrollback above it.
constexpr int kScreenLines = 100;
// What an approval shows of a program that waits for keys.
constexpr size_t kApprovalScreenLines = 8;

base::DictValue Error(std::string_view message) {
  return base::DictValue().Set("error", message);
}

std::string TabName(int number) {
  return base::StrCat({"Tab ", base::NumberToString(number)});
}

// The bytes a named key sends, as a terminal sends them.
std::optional<std::string> KeyBytes(std::string_view name) {
  static constexpr auto kNamed =
      base::MakeFixedFlatMap<std::string_view, std::string_view>({
          {"Backspace", "\x7f"},
          {"Delete", "\x1b[3~"},
          {"Down", "\x1b[B"},
          {"End", "\x1b[F"},
          {"Enter", "\r"},
          {"Escape", "\x1b"},
          {"Home", "\x1b[H"},
          {"Left", "\x1b[D"},
          {"PageDown", "\x1b[6~"},
          {"PageUp", "\x1b[5~"},
          {"Right", "\x1b[C"},
          {"Space", " "},
          {"Tab", "\t"},
          {"Up", "\x1b[A"},
      });
  if (const auto key = kNamed.find(name); key != kNamed.end()) {
    return std::string(key->second);
  }
  if (name.size() == 6 && name.starts_with("Ctrl-") &&
      base::IsAsciiAlpha(name[5])) {
    return std::string(
        1, static_cast<char>(base::ToUpperASCII(name[5]) - 'A' + 1));
  }
  return std::nullopt;
}

// The lines of `text`, without the empty ones at its end.
std::vector<std::string_view> Lines(std::string_view text) {
  std::vector<std::string_view> lines = base::SplitStringPiece(
      text, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  while (!lines.empty() && lines.back().empty()) {
    lines.pop_back();
  }
  return lines;
}

// Long output keeps its first and last lines, as Claude Code keeps it.
std::string Shortened(std::vector<std::string_view> lines) {
  const std::string left_out = base::StrCat(
      {"[",
       base::NumberToString(lines.size() -
                            std::min(lines.size(), kHeadLines + kTailLines)),
       " lines left out]"});
  if (lines.size() > kHeadLines + kTailLines) {
    std::vector<std::string_view> kept(lines.begin(),
                                       lines.begin() + kHeadLines);
    kept.push_back(left_out);
    kept.insert(kept.end(), lines.end() - kTailLines, lines.end());
    lines = std::move(kept);
  }
  std::string text = base::JoinString(lines, "\n");
  if (text.size() <= kMaxOutput) {
    return text;
  }
  // A third from the start and the rest from the end, cut between whole
  // characters.
  std::string head;
  base::TruncateUTF8ToByteSize(text, kMaxOutput / 3, &head);
  size_t tail = text.size() - (kMaxOutput - head.size());
  while (tail < text.size() && (text[tail] & 0xC0) == 0x80) {
    ++tail;
  }
  return base::StrCat({head, "\n[", base::NumberToString(tail - head.size()),
                       " characters left out]\n",
                       std::string_view(text).substr(tail)});
}

// What a command printed: the lines after the one it was typed on, without
// the prompt that follows once it has ended.
std::string CommandOutput(std::string_view text, bool ended) {
  std::vector<std::string_view> lines = Lines(text);
  if (!lines.empty()) {
    lines.erase(lines.begin());
  }
  if (ended && !lines.empty()) {
    lines.pop_back();
  }
  return Shortened(std::move(lines));
}

// How long a wait was, as the agent and the chat say it.
std::string Duration(base::TimeDelta wait) {
  const int64_t minutes = wait.InMinutes();
  return minutes >= 1
             ? base::StrCat({base::NumberToString(minutes), " min"})
             : base::StrCat({base::NumberToString(wait.InSeconds()), " s"});
}

std::string PasswordError(int number) {
  return base::StrCat(
      {"A program in tab ", base::NumberToString(number),
       " waits for a password. Only the user types passwords; uw asked them "
       "to type it in that tab. To cancel it, send Ctrl-C alone with keys."});
}

std::string BusyError(int number, const ShellHost::Activity& activity) {
  if (activity.password) {
    return PasswordError(number);
  }
  return base::StrCat(
      {TabName(number), " is still running",
       activity.program.empty() ? "" : base::StrCat({" ", activity.program}),
       ". Read it with screen, send it keys, or set new_tab to run this in "
       "another terminal tab."});
}

}  // namespace

// static
bool AgentTools::IsTerminalTool(std::string_view tool) {
  return tool == "run" || tool == "screen" || tool == "keys" ||
         tool == kTriggerTool;
}

std::optional<AgentTools::Approval> AgentTools::TerminalApprovalFor(
    std::string_view tool,
    const base::DictValue& args,
    bool every_step) {
  if (tool == "screen") {
    return std::nullopt;
  }
  Approval approval;
  // Outside Ask first, the command check gives the reason.
  if (every_step) {
    approval.reason = "Ask first is on.";
  }
  std::string error;
  if (tool == "run" || tool == kTriggerTool) {
    const std::string* command = args.FindString("command");
    const int number = TerminalFor(args, error);
    if (!command ||
        base::TrimWhitespaceASCII(*command, base::TRIM_ALL).empty() ||
        number < 0) {
      // The step fails on its own.
      return std::nullopt;
    }
    approval.tab = number ? number : next_tab_;
    approval.folder = number ? FolderOf(number) : "~";
    approval.command = *command;
    approval.action =
        base::StrCat({tool == "run" ? "Run " : "Leave running: ", *command});
    // A trigger runs out of sight, in the tab's folder only.
    if (tool == kTriggerTool) {
      approval.tab = 0;
    }
    return approval;
  }
  Tab* tab = TerminalTabFrom(args, error);
  const std::string* text = args.FindString("text");
  const base::ListValue* keys = args.FindList("keys");
  if (!tab || ((!text || text->empty()) && (!keys || keys->empty()))) {
    return std::nullopt;
  }
  approval.tab = *args.FindInt("tab");
  approval.folder = FolderOf(approval.tab);
  std::vector<std::string> sent;
  if (text && !text->empty()) {
    approval.text = *text;
    sent.push_back(base::StrCat({"\"", *text, "\""}));
  }
  if (keys) {
    for (const base::Value& key : *keys) {
      if (key.is_string()) {
        approval.keys.push_back(key.GetString());
        sent.push_back(key.GetString());
      }
    }
  }
  std::vector<std::string_view> shown = Lines(tab->screen);
  if (shown.size() > kApprovalScreenLines) {
    shown.erase(shown.begin(), shown.end() - kApprovalScreenLines);
  }
  approval.screen = base::JoinString(shown, "\n");
  approval.action = base::StrCat({"Send ", base::JoinString(sent, " then ")});
  return approval;
}

void AgentTools::EndWait() {
  if (wait_) {
    wait_->end_now = true;
  }
}

AgentTools::Tab* AgentTools::TerminalTabFrom(const base::DictValue& args,
                                             std::string& error) {
  Tab* tab = TabFrom(args, error);
  if (tab &&
      (!tab->terminal || !IsAgentTerminal(tab->handle.Get()->GetContents()))) {
    error = base::StrCat({TabName(*args.FindInt("tab")),
                          " is not one of your terminal tabs. run opens one."});
    return nullptr;
  }
  return tab;
}

int AgentTools::TerminalFor(const base::DictValue& args, std::string& error) {
  if (args.FindBool("new_tab").value_or(false)) {
    return 0;
  }
  if (const std::optional<int> number = args.FindInt("tab")) {
    return TerminalTabFrom(args, error) ? *number : -1;
  }
  for (const auto& [number, tab] : tabs_) {
    if (tab.terminal && tab.handle.Get()) {
      return number;
    }
  }
  return 0;
}

int AgentTools::OpenTerminal() {
  tabs::TabInterface* chat = chat_.Get();
  if (!chat) {
    return 0;
  }
  // Opened as a link from the chat's tab, the terminal nests under it.
  NavigateParams params(chat->GetBrowserWindowInterface(), TerminalUrl(),
                        ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  params.source_contents = chat->GetContents();
  ::Navigate(&params);
  tabs::TabInterface* tab = params.navigated_or_inserted_contents
                                ? tabs::TabInterface::MaybeGetFromContents(
                                      params.navigated_or_inserted_contents)
                                : nullptr;
  if (!tab) {
    return 0;
  }
  MarkAsAgentTerminal(tab->GetContents());
  const int number = next_tab_++;
  tabs_[number].handle = tab->GetHandle();
  tabs_[number].terminal = true;
  return number;
}

std::string AgentTools::FolderOf(int number) const {
  const auto tab = tabs_.find(number);
  tabs::TabInterface* open =
      tab == tabs_.end() ? nullptr : tab->second.handle.Get();
  const base::FilePath folder =
      open ? TerminalFolder(open->GetContents()) : base::FilePath();
  const base::FilePath home = base::GetHomeDir();
  base::FilePath inside;
  if (folder.empty() || folder == home) {
    return "~";
  }
  if (home.AppendRelativePath(folder, &inside)) {
    return base::StrCat({"~/", inside.value()});
  }
  return folder.value();
}

TerminalShell* AgentTools::ShellOf(int number) const {
  const auto tab = tabs_.find(number);
  tabs::TabInterface* open =
      tab == tabs_.end() ? nullptr : tab->second.handle.Get();
  return open ? TerminalShell::From(open->GetContents()) : nullptr;
}

void AgentTools::Run(const base::DictValue& args, Done done) {
  const std::string* command = args.FindString("command");
  if (!command || base::TrimWhitespaceASCII(*command, base::TRIM_ALL).empty()) {
    std::move(done).Run(Error("run needs a command."));
    return;
  }
  std::string error;
  const int number = TerminalFor(args, error);
  if (number < 0) {
    std::move(done).Run(Error(error));
    return;
  }
  wait_ = std::make_unique<TerminalWait>();
  wait_->command = *command;
  wait_->bytes = base::StrCat({*command, "\r"});
  wait_->limit = kDefaultWait;
  if (const std::optional<double> seconds = args.FindDouble("wait")) {
    wait_->limit =
        std::clamp(base::Seconds(*seconds), base::TimeDelta(), kMaxWait);
  }
  wait_->since = base::TimeTicks::Now();
  wait_->done = std::move(done);
  wait_->tab = number;
  if (!number) {
    wait_->opened = true;
    wait_->tab = OpenTerminal();
    if (!wait_->tab) {
      FinishWait(Error("The terminal tab could not be opened."));
      return;
    }
  }
  CheckShell();
}

void AgentTools::Trigger(const base::DictValue& args, Done done) {
  base::expected<TriggerAsk, std::string> ask = ParseTrigger(args);
  std::string error;
  const int number = ask.has_value() ? TerminalFor(args, error) : 0;
  if (!ask.has_value() || number < 0) {
    std::move(done).Run(Error(ask.has_value() ? error : ask.error()));
    return;
  }
  const auto tab = tabs_.find(number);
  tabs::TabInterface* open =
      tab == tabs_.end() ? nullptr : tab->second.handle.Get();
  const base::FilePath folder =
      open ? TerminalFolder(open->GetContents()) : base::FilePath();
  std::move(done).Run(
      start_trigger_.Run(*ask, folder.empty() ? base::GetHomeDir() : folder));
}

void AgentTools::Keys(const base::DictValue& args, Done done) {
  std::string error;
  if (!TerminalTabFrom(args, error)) {
    std::move(done).Run(Error(error));
    return;
  }
  const std::string* text = args.FindString("text");
  std::string bytes = text ? *text : std::string();
  if (const base::ListValue* keys = args.FindList("keys")) {
    for (const base::Value& key : *keys) {
      const std::optional<std::string> sent =
          key.is_string() ? KeyBytes(key.GetString()) : std::nullopt;
      if (!sent) {
        std::move(done).Run(Error(base::StrCat(
            {"Unknown key ", key.is_string() ? key.GetString() : "",
             ". Use Enter, Tab, Escape, Backspace, Delete, Space, Up, Down, "
             "Left, Right, Home, End, PageUp, PageDown, or Ctrl- and a "
             "letter, such as Ctrl-C."})));
        return;
      }
      bytes += *sent;
    }
  }
  if (bytes.empty()) {
    std::move(done).Run(Error("keys needs text or keys to send."));
    return;
  }
  wait_ = std::make_unique<TerminalWait>();
  wait_->tab = *args.FindInt("tab");
  wait_->keys = true;
  wait_->bytes = std::move(bytes);
  wait_->limit = kKeysLimit;
  wait_->since = base::TimeTicks::Now();
  wait_->done = std::move(done);
  CheckShell();
}

void AgentTools::Screen(const base::DictValue& args, Done done) {
  std::string error;
  if (!TerminalTabFrom(args, error)) {
    std::move(done).Run(Error(error));
    return;
  }
  const int number = *args.FindInt("tab");
  TerminalShell* shell = ShellOf(number);
  if (!shell) {
    std::move(done).Run(
        Error(base::StrCat({TabName(number), "'s shell has not started."})));
    return;
  }
  shell->GetActivity(base::BindOnce(
      [](base::WeakPtr<AgentTools> self, int number, Done done,
         ShellHost::Activity activity) {
        TerminalShell* shell = self ? self->ShellOf(number) : nullptr;
        if (!shell) {
          std::move(done).Run(
              Error(base::StrCat({TabName(number), " is closed."})));
          return;
        }
        shell->Read(-kScreenLines,
                    base::BindOnce(
                        [](base::WeakPtr<AgentTools> self, int number,
                           Done done, ShellHost::Activity activity,
                           const std::string& text, int cursor_line) {
                          if (!self || !self->tabs_.contains(number)) {
                            return;
                          }
                          Tab& tab = self->tabs_[number];
                          tab.screen = text;
                          content::WebContents* contents =
                              tab.handle.Get() ? tab.handle.Get()->GetContents()
                                               : nullptr;
                          base::DictValue result =
                              base::DictValue()
                                  .Set("tab", number)
                                  .Set("title", contents
                                                    ? base::UTF16ToUTF8(
                                                          contents->GetTitle())
                                                    : std::string())
                                  .Set("folder", self->FolderOf(number))
                                  .Set("text", Shortened(Lines(text)));
                          if (!activity.program.empty()) {
                            result.Set("program", activity.program);
                          }
                          if (activity.password) {
                            result.Set("password", true);
                          }
                          std::move(done).Run(std::move(result));
                        },
                        self, number, std::move(done), std::move(activity)));
      },
      weak_factory_.GetWeakPtr(), number, std::move(done)));
}

void AgentTools::CheckShell() {
  check_.Start(
      FROM_HERE, kCheckEvery,
      base::BindOnce(
          [](base::WeakPtr<AgentTools> self) {
            if (!self || !self->wait_) {
              return;
            }
            TerminalShell* shell = self->ShellOf(self->wait_->tab);
            if (!shell) {
              // A new tab's page has not loaded yet.
              if (self->wait_->opened && self->wait_->starting &&
                  base::TimeTicks::Now() - self->wait_->since < kStartLimit) {
                self->CheckShell();
                return;
              }
              self->FinishWait(Error(
                  base::StrCat({TabName(self->wait_->tab), " is closed."})));
              return;
            }
            shell->GetActivity(base::BindOnce(&AgentTools::OnActivity, self));
          },
          weak_factory_.GetWeakPtr()));
}

void AgentTools::OnActivity(ShellHost::Activity activity) {
  if (!wait_) {
    return;
  }
  TerminalWait& wait = *wait_;
  TerminalShell* shell = ShellOf(wait.tab);
  if (!shell) {
    FinishWait(Error(base::StrCat({TabName(wait.tab), " is closed."})));
    return;
  }
  const base::TimeTicks now = base::TimeTicks::Now();
  const bool quiet = now - shell->last_output() >= kQuiet;
  if (wait.starting) {
    if (wait.end_now) {
      FinishWait(
          Error("Not sent: the user wrote in or stopped the chat "
                "first."));
      return;
    }
    // Ctrl-C alone cancels a password prompt; nothing else reaches one.
    if (activity.password && !(wait.keys && wait.bytes == kCtrlC)) {
      FinishWait(Error(PasswordError(wait.tab)));
      return;
    }
    // A new shell has started once it has printed its prompt; one in use
    // can take a command at its prompt, and keys at any time.
    if (wait.keys || (activity.at_prompt && (quiet || !wait.opened))) {
      Send();
      return;
    }
    if (wait.opened && now - wait.since < kStartLimit) {
      CheckShell();
      return;
    }
    FinishWait(Error(wait.opened ? "The terminal's shell did not start."
                                 : BusyError(wait.tab, activity)));
    return;
  }
  // A password prompt hands the tab to the user, and the wait pauses until
  // they have typed it.
  if (activity.password && !wait.end_now) {
    if (wait.password_since.is_null()) {
      wait.password_since = now;
      if (on_password_) {
        const std::string program =
            activity.program.empty()
                ? std::string(base::SplitStringPiece(wait.command, " ",
                                                     base::TRIM_WHITESPACE,
                                                     base::SPLIT_WANT_NONEMPTY)
                                  .front())
                : activity.program;
        on_password_.Run(wait.tab, program);
      }
    }
    CheckShell();
    return;
  }
  if (!wait.password_since.is_null()) {
    wait.password_time += now - wait.password_since;
    wait.password_since = base::TimeTicks();
    if (on_password_) {
      on_password_.Run(0, std::string());
    }
  }
  const base::TimeDelta waited = now - wait.since - wait.password_time;
  if (wait.keys) {
    if (wait.end_now || (quiet && waited >= kQuiet) || waited >= wait.limit) {
      Collect(false);
      return;
    }
    CheckShell();
    return;
  }
  wait.left_prompt |= !activity.at_prompt;
  const bool ended = activity.at_prompt && quiet &&
                     (wait.left_prompt || waited >= kQuickCommand);
  if (ended || wait.end_now || waited >= wait.limit) {
    Collect(ended);
    return;
  }
  CheckShell();
}

void AgentTools::Send() {
  TerminalShell* shell = ShellOf(wait_->tab);
  wait_->starting = false;
  // The command's output starts on the line it is typed on.
  shell->Read(
      std::numeric_limits<int>::max(),
      base::BindOnce(
          [](base::WeakPtr<AgentTools> self, const std::string& text,
             int cursor_line) {
            if (!self || !self->wait_) {
              return;
            }
            TerminalShell* shell = self->ShellOf(self->wait_->tab);
            if (!shell) {
              self->FinishWait(Error(
                  base::StrCat({TabName(self->wait_->tab), " is closed."})));
              return;
            }
            self->wait_->from_line = cursor_line;
            shell->Write(self->wait_->bytes);
            self->wait_->since = base::TimeTicks::Now();
            if (!self->wait_->keys && self->on_running_) {
              self->on_running_.Run(self->wait_->tab, self->wait_->command);
            }
            self->CheckShell();
          },
          weak_factory_.GetWeakPtr()));
}

void AgentTools::Collect(bool ended) {
  TerminalShell* shell = ShellOf(wait_->tab);
  shell->Read(wait_->keys ? -kScreenLines : wait_->from_line,
              base::BindOnce(&AgentTools::OnCollected,
                             weak_factory_.GetWeakPtr(), ended));
}

void AgentTools::OnCollected(bool ended,
                             const std::string& text,
                             int cursor_line) {
  if (!wait_) {
    return;
  }
  const int number = wait_->tab;
  if (tabs_.contains(number)) {
    tabs_[number].screen = text;
  }
  base::DictValue result = base::DictValue().Set("tab", number);
  if (wait_->keys) {
    result.Set("text", Shortened(Lines(text)));
    FinishWait(std::move(result));
    return;
  }
  result.Set("folder", FolderOf(number));
  result.Set("output", CommandOutput(text, ended));
  if (!ended) {
    result.Set("running", true);
    result.Set("waited", Duration(wait_->limit));
    result.Set(
        "note",
        base::StrCat({wait_->end_now
                          ? "The user wrote in or stopped the chat."
                          : base::StrCat({"Still running after ",
                                          Duration(wait_->limit), "."}),
                      " It keeps running in tab ", base::NumberToString(number),
                      ": read it later with screen, or stop it with keys and "
                      "Ctrl-C."}));
  }
  FinishWait(std::move(result));
}

void AgentTools::FinishWait(base::DictValue result) {
  check_.Stop();
  std::unique_ptr<TerminalWait> wait = std::move(wait_);
  if (!wait->password_since.is_null() && on_password_) {
    on_password_.Run(0, std::string());
  }
  if (!wait->keys && !wait->starting && on_running_) {
    on_running_.Run(0, std::string());
  }
  std::move(wait->done).Run(std::move(result));
}

}  // namespace uw
