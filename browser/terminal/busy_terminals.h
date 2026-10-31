// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_TERMINAL_BUSY_TERMINALS_H_
#define UW_BROWSER_TERMINAL_BUSY_TERMINALS_H_

#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/weak_ptr.h"

namespace content {
class WebContents;
}

namespace uw {

// A terminal tab where a program other than its shell runs.
struct BusyTerminal {
  BusyTerminal();
  BusyTerminal(const BusyTerminal&);
  BusyTerminal& operator=(const BusyTerminal&);
  ~BusyTerminal();

  base::WeakPtr<content::WebContents> page;
  // The program's name, as macOS reports it: `node` for `pnpm dev`.
  std::string program;
};

// Whether any of `pages` is a terminal tab whose shell has started, the
// only kind FindBusyTerminals has to ask.
bool HasTerminal(const std::vector<content::WebContents*>& pages);

// Asks the shell of each terminal tab among `pages` what runs in it, and
// gives `done` the busy ones, in the order of `pages`. A tab that closes
// meanwhile is left out.
void FindBusyTerminals(
    const std::vector<content::WebContents*>& pages,
    base::OnceCallback<void(std::vector<BusyTerminal>)> done);

}  // namespace uw

#endif  // UW_BROWSER_TERMINAL_BUSY_TERMINALS_H_
