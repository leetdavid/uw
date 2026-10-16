// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/webui/uw_terminal/uw_terminal_handler.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/task/thread_pool.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/file_info.h"
#include "ui/base/l10n/l10n_util.h"
#include "uw/browser/terminal/terminal_tab.h"
#include "uw/components/terminal/shell_host.h"
#include "uw/components/terminal/shell_quote.h"

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

// Each pasted image gets a folder of its own, so its name can stay plain.
base::FilePath SaveImage(const std::vector<uint8_t>& png) {
  base::FilePath folder;
  if (!base::CreateNewTempDirectory("uw-paste", &folder)) {
    return base::FilePath();
  }
  const base::FilePath path = folder.Append("image.png");
  return base::WriteFile(path, png) ? path : base::FilePath();
}

std::string QuotePath(const base::FilePath& path) {
  return path.empty() ? std::string() : QuotePathsForShell({path});
}

void PasteImage(mojom::TerminalHandler::PastePathsCallback callback,
                const std::vector<uint8_t>& png) {
  if (png.empty()) {
    std::move(callback).Run(std::string());
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
      base::BindOnce(&SaveImage, png),
      base::BindOnce(&QuotePath).Then(std::move(callback)));
}

void PasteImageWithoutText(mojom::TerminalHandler::PastePathsCallback callback,
                           std::u16string text) {
  if (!text.empty()) {
    std::move(callback).Run(std::string());
    return;
  }
  ui::Clipboard::GetForCurrentThread()->ReadPng(
      ui::ClipboardBuffer::kCopyPaste, std::nullopt,
      base::BindOnce(&PasteImage, std::move(callback)));
}

void PasteFilesOrImage(mojom::TerminalHandler::PastePathsCallback callback,
                       std::vector<ui::FileInfo> files) {
  if (files.empty()) {
    ui::Clipboard::GetForCurrentThread()->ReadText(
        ui::ClipboardBuffer::kCopyPaste, std::nullopt,
        base::BindOnce(&PasteImageWithoutText, std::move(callback)));
    return;
  }
  std::vector<base::FilePath> paths;
  for (const ui::FileInfo& file : files) {
    paths.push_back(file.path);
  }
  std::move(callback).Run(QuotePathsForShell(paths));
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
  // A command from the command bar, run as if typed and entered.
  if (const std::string command = TakeStartCommand(contents_);
      !command.empty()) {
    Write(command + "\r");
  }
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

void UwTerminalHandler::PastePaths(PastePathsCallback callback) {
  ui::Clipboard::GetForCurrentThread()->ReadFilenames(
      ui::ClipboardBuffer::kCopyPaste, std::nullopt,
      base::BindOnce(&PasteFilesOrImage, std::move(callback)));
}

void UwTerminalHandler::OnShellOutput(std::string text) {
  page_->Output(text);
}

void UwTerminalHandler::OnShellFolderChanged(const base::FilePath& folder) {
  SetTerminalFolder(contents_, folder);
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
