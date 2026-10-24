// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/uw_command/command_handler.h"
#include "uw/browser/uw_command/command_workspace.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_theme.h"
#include "uw/components/uw_command/command_messages.h"
#include "uw/components/uw_command/command_settings.h"

namespace uw {

namespace {

using Scope = CommandSetting::Scope;

base::Value StoredValue(const CommandSetting& setting,
                        Profile* profile,
                        const std::string& workspace) {
  switch (setting.scope) {
    case Scope::kProfile:
      return profile->GetPrefs()->GetValue(setting.pref).Clone();
    case Scope::kEverywhere:
      return g_browser_process->local_state()->GetValue(setting.pref).Clone();
    case Scope::kWorkspace:
      return ThemeSettingValue(ThemeOfWorkspace(workspace, profile), setting);
  }
}

void Store(const CommandSetting& setting,
           Profile* profile,
           const std::string& workspace,
           const base::Value& value) {
  switch (setting.scope) {
    case Scope::kProfile:
      profile->GetPrefs()->Set(setting.pref, value);
      return;
    case Scope::kEverywhere:
      g_browser_process->local_state()->Set(setting.pref, value);
      return;
    case Scope::kWorkspace: {
      Theme theme = ThemeOfWorkspace(workspace, profile);
      SetThemeSetting(theme, setting, value);
      SetThemeOfWorkspace(workspace, theme);
      return;
    }
  }
}

std::vector<std::string> AppliesTo(const CommandSetting& setting,
                                   Profile* profile,
                                   const std::string& workspace) {
  switch (setting.scope) {
    case Scope::kProfile:
      return WorkspaceNames(profile);
    case Scope::kEverywhere:
      return WorkspaceNames(nullptr);
    case Scope::kWorkspace: {
      const WorkspaceList::Workspace* found =
          WorkspaceList::Get().Find(workspace);
      return {found ? found->name : std::string()};
    }
  }
}

base::Value Describe(const CommandSetting& setting,
                     Profile* profile,
                     const std::string& workspace) {
  base::ListValue applies_to;
  for (std::string& name : AppliesTo(setting, profile, workspace)) {
    applies_to.Append(std::move(name));
  }
  base::DictValue described;
  described.Set("name", setting.name);
  described.Set("value", ShownSettingValue(
                             setting, StoredValue(setting, profile, workspace)));
  described.Set("takes", Takes(setting));
  described.Set("appliesTo", std::move(applies_to));
  return base::Value(std::move(described));
}

// The command bar converts only to a currency it has rates for, as its
// settings page offers.
bool HasRates(Profile* profile, const base::Value& currency) {
  const std::string& code = currency.GetString();
  const base::DictValue* per_euro = profile->GetPrefs()
                                        ->GetDict(kCommandBarRatesPref)
                                        .FindDict("per_euro");
  return code.empty() || code == "EUR" || (per_euro && per_euro->contains(code));
}

std::string Answer(const CommandRequest& request,
                   Profile* profile,
                   const std::string& workspace) {
  if (request.method == "settings.list") {
    base::ListValue settings;
    for (const CommandSetting& setting : CommandSettings()) {
      settings.Append(Describe(setting, profile, workspace));
    }
    base::DictValue result;
    result.Set("settings", std::move(settings));
    return CommandResult(request.id, base::Value(std::move(result)));
  }
  const std::string* name = request.params.FindString("name");
  const CommandSetting* setting = name ? FindCommandSetting(*name) : nullptr;
  if (!setting) {
    return CommandError(
        request.id, kCommandUsage,
        base::StrCat({"uw has no setting called ", name ? *name : "",
                      ".\n  uw settings list"}));
  }
  if (request.method == "settings.get") {
    return CommandResult(request.id, Describe(*setting, profile, workspace));
  }
  if (request.method != "settings.set") {
    return CommandError(request.id, kMethodNotFound,
                        base::StrCat({"This uw can't answer ", request.method,
                                      ". Update uw, then try again."}));
  }
  const std::string* text = request.params.FindString("value");
  if (!text) {
    return CommandError(request.id, kCommandUsage,
                        "uw settings set takes a setting and a value.");
  }
  base::expected<base::Value, std::string> value =
      ParseSettingValue(*setting, *text);
  if (value.has_value() && setting->kind == CommandSetting::Kind::kCurrency &&
      !HasRates(profile, *value)) {
    value = base::unexpected(base::StrCat(
        {*text, " isn't a currency uw has rates for. ", setting->name,
         " takes ", Takes(*setting), "."}));
  }
  if (!value.has_value()) {
    return CommandError(request.id, kCommandUsage, value.error());
  }
  Store(*setting, profile, workspace, *value);
  return CommandResult(request.id, Describe(*setting, profile, workspace));
}

}  // namespace

void RunSettingsCommand(CommandRequest request, CommandReply reply) {
  base::expected<CommandWorkspace, std::string> chosen =
      ChooseWorkspace(request.params);
  if (!chosen.has_value()) {
    std::move(reply).Run(
        CommandError(request.id, kCommandUsage, chosen.error()));
    return;
  }
  LoadWorkspace(
      std::move(*chosen),
      base::BindOnce(
          [](CommandRequest request, CommandReply reply, Profile* profile,
             const std::string& workspace) {
            std::move(reply).Run(
                profile ? Answer(request, profile, workspace)
                        : CommandError(request.id, kCommandFailed,
                                       "uw couldn't open that workspace."));
          },
          std::move(request), std::move(reply)));
}

}  // namespace uw
