// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/webui/uw_terminal/uw_terminal_handler.h"

#include <algorithm>
#include <utility>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "uw/components/terminal/shell_host.h"

namespace uw {
namespace {

// A page asks for the size it measured. Anything else is a mistake.
int Clamp(int32_t cells) {
  return std::clamp(cells, 1, 1000);
}

ShellHost::Options UserShell(int columns, int rows) {
  ShellHost::Options options = ShellHost::ForUser();
  options.columns = columns;
  options.rows = rows;
  return options;
}

}  // namespace

UwTerminalHandler::UwTerminalHandler(
    content::WebContents* contents,
    mojo::PendingRemote<mojom::TerminalPage> page,
    mojo::PendingReceiver<mojom::TerminalHandler> receiver)
    : contents_(contents),
      page_(std::move(page)),
      receiver_(this, std::move(receiver)) {}

UwTerminalHandler::~UwTerminalHandler() = default;

void UwTerminalHandler::Start(int32_t columns, int32_t rows) {
  if (session_) {
    return;
  }
  session_ = std::make_unique<ShellSession>(
      base::BindOnce(&UserShell, Clamp(columns), Clamp(rows)), this);
}

void UwTerminalHandler::Write(const std::string& text) {
  if (session_) {
    session_->Write(text);
  }
}

void UwTerminalHandler::Resize(int32_t columns, int32_t rows) {
  if (session_) {
    session_->Resize(Clamp(columns), Clamp(rows));
  }
}

void UwTerminalHandler::AckOutput() {
  if (session_) {
    session_->AckOutput();
  }
}

void UwTerminalHandler::OnShellOutput(std::string text) {
  page_->Output(text);
}

void UwTerminalHandler::OnShellFolderChanged(const base::FilePath& folder) {
  page_->SetFolder(folder.BaseName().AsUTF8Unsafe());
}

void UwTerminalHandler::OnShellExited(bool started) {
  if (started) {
    contents_->ClosePage();
    return;
  }
  // A tab that closed at once would explain nothing.
  page_->Output(base::StrCat(
      {l10n_util::GetStringUTF8(IDS_UW_TERMINAL_NO_SHELL), "\r\n"}));
}

}  // namespace uw
