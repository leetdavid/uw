// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TERMINAL_CLOSE_WARNING_H_
#define UW_BROWSER_UI_VIEWS_TERMINAL_CLOSE_WARNING_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/tab_groups/tab_group_id.h"
#include "uw/browser/terminal/busy_terminals.h"
#include "uw/browser/terminal/close_guard.h"

class BrowserWindowInterface;

namespace views {
class View;
class Widget;
}  // namespace views

namespace uw {

// A window's CloseGuard. Before a close the user asked for stops a program in
// a terminal tab, one dialog over the window names the programs, with Cancel
// and the close. A close with no busy terminal goes ahead without a question.
class CloseWarning : public CloseGuard {
 public:
  explicit CloseWarning(BrowserWindowInterface* window);
  CloseWarning(const CloseWarning&) = delete;
  CloseWarning& operator=(const CloseWarning&) = delete;
  ~CloseWarning() override;

  // Browser tests run without the question, so a test that ends with a
  // program running still quits. A test of the question turns it on.
  static void AskInTestsForTesting(bool ask);
  // The dialog over `window`, while it asks.
  static views::Widget* DialogForTesting(BrowserWindowInterface* window);

  // CloseGuard:
  bool HoldTabs(const std::vector<content::WebContents*>& pages,
                TabsDone done) override;
  bool HoldTask(const tab_groups::TabGroupId& task,
                base::OnceClosure close) override;
  bool HoldWindow() override;
  bool HoldQuit(base::OnceCallback<void(bool quit)> done) override;

 private:
  enum class Closing { kTab, kTabs, kTask, kWindow, kQuit };

  bool CanAsk() const;
  // Asks the shells among `pages`, and the user if any is busy. `done` gets
  // the answer: true without a question when none is.
  void Ask(Closing closing,
           size_t count,
           const std::vector<content::WebContents*>& pages,
           std::optional<tab_groups::TabGroupId> task,
           base::OnceCallback<void(bool)> done);
  void Show(std::vector<BusyTerminal> busy);
  std::unique_ptr<views::View> List(const std::vector<BusyTerminal>& busy);
  // Where `page` is, for the dialog's list: its task in a window close, its
  // workspace when quitting.
  std::u16string PlaceOf(content::WebContents* page) const;
  // The dialog's answer. It runs once the dialog is done with.
  void Answered(bool close);
  void Finish(bool close);

  const raw_ptr<BrowserWindowInterface> window_;
  // The question out, if any. Closes asked for meanwhile are dropped, but a
  // quit is cancelled, since quitting waits for an answer.
  bool asking_ = false;
  Closing closing_ = Closing::kTab;
  size_t count_ = 0;
  std::optional<tab_groups::TabGroupId> task_;
  base::OnceCallback<void(bool)> done_;
  raw_ptr<views::Widget> dialog_ = nullptr;
  // While a close the user agreed to runs, its pages are not held again.
  bool releasing_ = false;
  // The next close of this window is the one the user agreed to.
  bool window_agreed_ = false;
  base::WeakPtrFactory<CloseWarning> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TERMINAL_CLOSE_WARNING_H_
