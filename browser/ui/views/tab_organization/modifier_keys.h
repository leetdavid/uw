// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MODIFIER_KEYS_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MODIFIER_KEYS_H_

#include "base/callback_list.h"
#include "base/functional/callback_forward.h"
#include "base/no_destructor.h"

namespace uw {

// Which modifier keys are held down while uw is the app in front, for key
// hints and the key card, and how many other keys went down. macOS sends a
// lone modifier press as a modifier change, not as a key event a view would
// get. Switching to another app lets go of every key. On Windows, Option is
// Alt and Cmd is Ctrl, and Control is never held. See
// docs/plans/keyboard-and-mouse.md#key-hints and #key-card.
class ModifierKeys {
 public:
  static ModifierKeys& Get();

  ModifierKeys(const ModifierKeys&) = delete;
  ModifierKeys& operator=(const ModifierKeys&) = delete;

  bool option() const { return option_; }
  bool command() const { return command_; }
  bool control() const { return control_; }
  bool shift() const { return shift_; }
  // Counts the keys other than modifiers that went down.
  int presses() const { return presses_; }

  // Runs `changed` each time a modifier goes down or up, or another key goes
  // down.
  base::CallbackListSubscription Observe(base::RepeatingClosure changed);

  void SetForTesting(bool option,
                     bool command,
                     bool control = false,
                     bool shift = false);
  void PressForTesting();

 private:
  friend class base::NoDestructor<ModifierKeys>;

  ModifierKeys();
  ~ModifierKeys();

  void Set(bool option, bool command, bool control, bool shift);
  void Press();

  bool option_ = false;
  bool command_ = false;
  bool control_ = false;
  bool shift_ = false;
  int presses_ = 0;
  base::RepeatingClosureList observers_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MODIFIER_KEYS_H_
