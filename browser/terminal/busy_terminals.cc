// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/terminal/busy_terminals.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/barrier_callback.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "content/public/browser/web_contents.h"
#include "uw/browser/terminal/terminal_shell.h"

namespace uw {

BusyTerminal::BusyTerminal() = default;
BusyTerminal::BusyTerminal(const BusyTerminal&) = default;
BusyTerminal& BusyTerminal::operator=(const BusyTerminal&) = default;
BusyTerminal::~BusyTerminal() = default;

bool HasTerminal(const std::vector<content::WebContents*>& pages) {
  for (content::WebContents* page : pages) {
    if (TerminalShell::From(page)) {
      return true;
    }
  }
  return false;
}

void FindBusyTerminals(
    const std::vector<content::WebContents*>& pages,
    base::OnceCallback<void(std::vector<BusyTerminal>)> done) {
  std::vector<std::pair<content::WebContents*, TerminalShell*>> shells;
  for (content::WebContents* page : pages) {
    if (TerminalShell* shell = TerminalShell::From(page)) {
      shells.emplace_back(page, shell);
    }
  }
  // The answers come back in any order, so each carries its place.
  const auto answered =
      base::BarrierCallback<std::pair<size_t, std::optional<BusyTerminal>>>(
          shells.size(),
          base::BindOnce(
              [](base::OnceCallback<void(std::vector<BusyTerminal>)> done,
                 std::vector<std::pair<size_t, std::optional<BusyTerminal>>>
                     answers) {
                std::ranges::sort(answers, {}, [](const auto& answer) {
                  return answer.first;
                });
                std::vector<BusyTerminal> busy;
                for (auto& [index, terminal] : answers) {
                  if (terminal && terminal->page) {
                    busy.push_back(std::move(*terminal));
                  }
                }
                std::move(done).Run(std::move(busy));
              },
              std::move(done)));
  for (size_t i = 0; i < shells.size(); ++i) {
    shells[i].second->GetActivity(base::BindOnce(
        [](decltype(answered) answered, size_t index,
           base::WeakPtr<content::WebContents> page,
           ShellHost::Activity activity) {
          std::optional<BusyTerminal> terminal;
          if (!activity.program.empty()) {
            terminal.emplace();
            terminal->page = std::move(page);
            terminal->program = std::move(activity.program);
          }
          answered.Run({index, std::move(terminal)});
        },
        answered, i, shells[i].first->GetWeakPtr()));
  }
}

}  // namespace uw
