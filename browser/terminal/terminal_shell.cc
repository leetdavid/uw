// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/terminal/terminal_shell.h"

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/supports_user_data.h"
#include "content/public/browser/web_contents.h"

namespace uw {
namespace {

const void* const kShellKey = &kShellKey;

struct ShellData : base::SupportsUserData::Data {
  raw_ptr<TerminalShell> shell;
};

}  // namespace

// static
TerminalShell* TerminalShell::From(content::WebContents* contents) {
  const auto* data =
      static_cast<const ShellData*>(contents->GetUserData(kShellKey));
  return data ? data->shell.get() : nullptr;
}

// static
void TerminalShell::Set(content::WebContents* contents, TerminalShell* shell) {
  auto data = std::make_unique<ShellData>();
  data->shell = shell;
  contents->SetUserData(kShellKey, std::move(data));
}

// static
void TerminalShell::Unset(content::WebContents* contents,
                          TerminalShell* shell) {
  if (From(contents) == shell) {
    contents->RemoveUserData(kShellKey);
  }
}

}  // namespace uw
