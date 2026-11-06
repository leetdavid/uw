// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_KEY_CARD_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_KEY_CARD_H_

#include <optional>

#include "base/callback_list.h"
#include "base/functional/callback_forward.h"
#include "base/time/time.h"
#include "components/tab_groups/tab_group_id.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/label.h"

class BrowserWindowInterface;

namespace tabs {
class TabInterface;
}

namespace views {
class View;
class Widget;
}  // namespace views

namespace uw {

// What the keys held with Cmd act on: the selected tab and the tabs under it
// with Option, and its task with Ctrl. Ctrl+Alt and Alt+Shift on Windows. See
// docs/adr/0032.
enum class KeyScope { kNone, kBranch, kTask };

// The key card: once a scope's two keys are held for half a second, it lists
// that scope's keys over the window in front, and the sidebar marks what they
// act on. See docs/plans/keyboard-and-mouse.md#key-card.
class KeyCard {
 public:
  // Starts watching the keys, once for the app.
  static void Watch();
  // The scope whose card shows in `window`, or kNone.
  static KeyScope ShownIn(const BrowserWindowInterface* window);
  // Tells `changed` when the card shows, hides, or changes scope.
  static base::CallbackListSubscription Observe(base::RepeatingClosure changed);

  static views::Widget* WidgetForTesting();
  static void SetDelayForTesting(base::TimeDelta delay);
};

// The tint of a sidebar row the shown card's keys act on: `tab`'s row, or
// with `tab` null, the row of `task`. Transparent for any other row.
SkColor KeyScopeTint(const views::View* row,
                     tabs::TabInterface* tab,
                     std::optional<tab_groups::TabGroupId> task);

// The number on a task's row while the task card shows: the key that goes to
// the task with the task keys. Nothing past the ninth task.
class TaskKeyNumber : public views::Label {
  METADATA_HEADER(TaskKeyNumber, views::Label)

 public:
  // `task` is empty for the Scratchpad's heading, which shows 0.
  explicit TaskKeyNumber(std::optional<tab_groups::TabGroupId> task);
  TaskKeyNumber(const TaskKeyNumber&) = delete;
  TaskKeyNumber& operator=(const TaskKeyNumber&) = delete;
  ~TaskKeyNumber() override;

  // views::Label:
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void OnThemeChanged() override;

 private:
  void Update();

  const std::optional<tab_groups::TabGroupId> task_;
  base::CallbackListSubscription shown_;
  base::CallbackListSubscription tree_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_KEY_CARD_H_
