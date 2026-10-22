// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_WEBUI_UW_TERMINAL_UW_TERMINAL_HANDLER_H_
#define UW_BROWSER_UI_WEBUI_UW_TERMINAL_UW_TERMINAL_HANDLER_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "uw/browser/terminal/terminal_shell.h"
#include "uw/browser/ui/webui/uw_terminal/uw_terminal.mojom.h"
#include "uw/components/terminal/shell_session.h"

namespace content {
class WebContents;
}

namespace uw {

// Runs the shell of one terminal tab and carries text between it and the
// tab's page, and the agent's tools. Owned by the page's controller, so the
// shell ends when the tab closes or its page goes away.
class UwTerminalHandler : public mojom::TerminalHandler,
                          public ShellSession::Client,
                          public TerminalShell {
 public:
  UwTerminalHandler(content::WebContents* contents,
                    mojo::PendingRemote<mojom::TerminalPage> page,
                    mojo::PendingReceiver<mojom::TerminalHandler> receiver);
  UwTerminalHandler(const UwTerminalHandler&) = delete;
  UwTerminalHandler& operator=(const UwTerminalHandler&) = delete;
  ~UwTerminalHandler() override;

  // mojom::TerminalHandler:
  void Start(int32_t columns, int32_t rows) override;
  // Every byte that reaches the shell passes through here.
  void Write(const std::string& text) override;
  void Resize(int32_t columns, int32_t rows) override;
  void AckOutput() override;
  void PastePaths(PastePathsCallback callback) override;

  // ShellSession::Client:
  void OnShellOutput(std::string text) override;
  void OnShellFolderChanged(const base::FilePath& folder) override;
  void OnShellExited(bool started) override;

  // TerminalShell:
  void GetActivity(base::OnceCallback<void(ShellHost::Activity)> done) override;
  void Read(int from, ReadDone done) override;
  base::TimeTicks last_output() const override;

 private:
  // Adds a password prompt that shows only in its text.
  void OnActivity(base::OnceCallback<void(ShellHost::Activity)> done,
                  ShellHost::Activity activity);

  const raw_ptr<content::WebContents> contents_;
  mojo::Remote<mojom::TerminalPage> page_;
  mojo::Receiver<mojom::TerminalHandler> receiver_;
  std::unique_ptr<ShellSession> session_;
  // The page draws the shell's output and has not said it is done.
  bool drawing_ = false;
  base::TimeTicks last_output_;
  base::WeakPtrFactory<UwTerminalHandler> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_TERMINAL_UW_TERMINAL_HANDLER_H_
