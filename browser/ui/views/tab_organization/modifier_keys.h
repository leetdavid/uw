// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MODIFIER_KEYS_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MODIFIER_KEYS_H_

#include "base/callback_list.h"
#include "base/functional/callback_forward.h"
#include "base/no_destructor.h"

namespace uw {

// Whether Option or Cmd is held down while uw is the app in front, for key
// hints. macOS sends a lone Option or Cmd press as a modifier change, not as
// a key event a view would get. Switching to another app lets go of both. See
// docs/plans/keyboard-and-mouse.md#key-hints.
class ModifierKeys {
 public:
  static ModifierKeys& Get();

  ModifierKeys(const ModifierKeys&) = delete;
  ModifierKeys& operator=(const ModifierKeys&) = delete;

  bool option() const { return option_; }
  bool command() const { return command_; }

  // Runs `changed` each time either key goes down or up.
  base::CallbackListSubscription Observe(base::RepeatingClosure changed);

  void SetForTesting(bool option, bool command);

 private:
  friend class base::NoDestructor<ModifierKeys>;

  ModifierKeys();
  ~ModifierKeys();

  void Set(bool option, bool command);

  bool option_ = false;
  bool command_ = false;
  base::RepeatingClosureList observers_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MODIFIER_KEYS_H_
