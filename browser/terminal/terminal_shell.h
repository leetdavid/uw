// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_TERMINAL_TERMINAL_SHELL_H_
#define UW_BROWSER_TERMINAL_TERMINAL_SHELL_H_

#include <string>

#include "base/functional/callback.h"
#include "base/time/time.h"
#include "uw/components/terminal/shell_host.h"

namespace content {
class WebContents;
}

namespace uw {

// The shell of a terminal tab, as the agent's tools reach it. The tab's page
// runs the shell; this carries the agent's text to it through the same
// single write as the user's typing, and reads the screen back.
class TerminalShell {
 public:
  // The shell of the terminal tab showing `contents`, or null before its
  // page has loaded.
  static TerminalShell* From(content::WebContents* contents);

  // Sends `text` to the shell as if typed. Nothing happens before the shell
  // starts.
  virtual void Write(const std::string& text) = 0;

  // What the terminal waits for. Nothing before the shell starts.
  virtual void GetActivity(
      base::OnceCallback<void(ShellHost::Activity)> done) = 0;

  // The tab's text from the line `from` through the cursor's line, and the
  // cursor line's number. Lines count from the top of the scrollback; a
  // negative `from` counts back from the cursor's line.
  using ReadDone =
      base::OnceCallback<void(const std::string& text, int cursor_line)>;
  virtual void Read(int from, ReadDone done) = 0;

  // When the page last finished drawing what the shell printed, or now while
  // some waits to be drawn. The shell is read only as fast as the page draws,
  // so output can wait unread until then.
  virtual base::TimeTicks last_output() const = 0;

 protected:
  virtual ~TerminalShell() = default;

  // Makes `shell` the shell of `contents`, and takes it back. A shell takes
  // back only itself, so a page's next shell stays.
  static void Set(content::WebContents* contents, TerminalShell* shell);
  static void Unset(content::WebContents* contents, TerminalShell* shell);
};

}  // namespace uw

#endif  // UW_BROWSER_TERMINAL_TERMINAL_SHELL_H_
