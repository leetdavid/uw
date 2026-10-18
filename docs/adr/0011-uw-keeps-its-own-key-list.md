# uw keeps its own key list

On 2026-10-19 the user asked to change keys inside uw. uw keeps its own list of keys for its commands, app-wide, and applies them in its key lookup and menus. macOS already lets a user rebind any menu item by title in System Settings → Keyboard → App Shortcuts, and uw could have written that setting from its own page. We chose a list of uw's own so the same code can serve a Windows build, so a key can be taken from another command with the change shown, and so keys that are not in a menu can be changed too.

Considered options: writing macOS's App Shortcuts setting for uw (rejected: macOS only, and it may apply only after a relaunch); a page that lists keys and opens System Settings for changes (rejected: the user types menu titles there, and nothing warns of a clash).

Consequences: Chromium's own keys live in three tables uw does not own. Changing them from the list is a later ticket, T123, and costs patch upkeep on every Chromium update.
