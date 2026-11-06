// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_KEY_H_
#define UW_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_KEY_H_

#include <optional>
#include <string>

#include "components/prefs/pref_change_registrar.h"
#include "url/gurl.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/accelerators/global_accelerator_listener/global_accelerator_listener.h"
#include "ui/gfx/native_ui_types.h"

namespace uw {

// The little window's key as uw's menu bar has it now: Option+Cmd+N, or
// Ctrl+Alt+N on Windows, unless the user changed it. None when it has no key.
std::optional<ui::Accelerator> LittleWindowKey();

// Runs New Little Window from the menu bar, the new button's menu, or the key
// in uw. While the in-uw switch is off, the key does nothing, and a click
// still opens one.
void RunNewLittleWindowCommand();

// Opens `url` in a little window, as a link from another app does while its
// switch is on. False when the switch is off.
bool OpenLinkFromAnotherAppInLittleWindow(const GURL& url);

// Listens for the little window's key in other apps while its switch is on,
// for as long as uw runs, per docs/plans/little-window.md#settings.
class LittleWindowKeyListener : public ui::GlobalAcceleratorListener::Observer {
 public:
  // Starts listening, once, as the first window opens.
  static void Start();
  // Null before Start().
  static LittleWindowKeyListener* Get();
  // Browser tests leave other apps' keys alone, unless a test asks.
  static void ListenInTestsForTesting();

  LittleWindowKeyListener(const LittleWindowKeyListener&) = delete;
  LittleWindowKeyListener& operator=(const LittleWindowKeyListener&) = delete;

  // On Windows, what the keyboard types with AltGr and the key's letter, when
  // it types something. uw then leaves the key to other apps.
  const std::u16string& altgr_character() const { return altgr_; }
  // Whether the key is taken from other apps now.
  bool listening() const { return registered_.has_value(); }
  // Reads the switches and the key again, as after a change.
  void Update();

  // ui::GlobalAcceleratorListener::Observer:
  void OnKeyPressed(const ui::Accelerator& accelerator) override;
  void ExecuteCommand(const std::string& accelerator_group_id,
                      const std::string& command_id) override;

 private:
  LittleWindowKeyListener();
  ~LittleWindowKeyListener() override;

  PrefChangeRegistrar prefs_;
  std::optional<ui::Accelerator> registered_;
  std::u16string altgr_;
};

// Platform parts, in little_window_key_mac.mm, little_window_key_win.cc, and
// little_window_key_stub.cc.
namespace internal {
// The key as uw's menu bar or accelerator table has it.
std::optional<ui::Accelerator> PlatformLittleWindowKey();
// Whether one of uw's windows is the front window of the screen.
bool IsUwInFront();
// Whether the command being run now comes from a key press, not a click.
bool IsKeyPress();
// What the current keyboard types with AltGr and `key`'s letter, or nothing.
std::u16string AltGrCharacter(const ui::Accelerator& key);
// Brings uw to the front, as a key pressed in another app asks.
void BringUwForward();
// Lets a new little window show on the Space in view, over a full-screen app
// too, so opening it never moves the user to another Space.
void JoinActiveSpace(gfx::NativeWindow window);
}  // namespace internal

}  // namespace uw

#endif  // UW_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_KEY_H_
