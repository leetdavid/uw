// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/workspaces/workspace_text.h"

#include <memory>

#include "base/i18n/unicodestring.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"  // nogncheck
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "third_party/icu/source/common/unicode/locid.h"
#include "third_party/icu/source/common/unicode/unistr.h"
#include "third_party/icu/source/common/unicode/utypes.h"
#include "third_party/icu/source/i18n/unicode/listformatter.h"
#include "uw/browser/workspaces/workspace_list.h"

namespace uw {

std::u16string JoinNames(const std::vector<std::u16string>& names) {
  std::vector<icu::UnicodeString> strings;
  for (const std::u16string& name : names) {
    strings.emplace_back(name.data(), name.size());
  }
  UErrorCode error = U_ZERO_ERROR;
  std::unique_ptr<icu::ListFormatter> formatter(
      icu::ListFormatter::createInstance(icu::Locale::getDefault(), error));
  icu::UnicodeString joined;
  if (U_SUCCESS(error) && formatter) {
    formatter->format(strings.data(), strings.size(), joined, error);
  }
  return U_SUCCESS(error) ? base::i18n::UnicodeStringToString16(joined)
                          : std::u16string();
}

std::u16string ProfileName(const base::FilePath& profile) {
  ProfileManager* manager = g_browser_process->profile_manager();
  const ProfileAttributesEntry* entry =
      manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(
          manager->user_data_dir().Append(profile));
  return entry ? entry->GetName() : profile.LossyDisplayName();
}

std::vector<std::u16string> WorkspaceNamesOn(const base::FilePath& profile,
                                             std::string_view except) {
  std::vector<std::u16string> names;
  for (const WorkspaceList::Workspace* workspace :
       WorkspaceList::Get().ForProfile(profile)) {
    if (workspace->id != except) {
      names.push_back(base::UTF8ToUTF16(workspace->name));
    }
  }
  return names;
}

std::u16string WorkspaceIconsOn(const base::FilePath& profile) {
  std::u16string icons;
  for (const WorkspaceList::Workspace* workspace :
       WorkspaceList::Get().ForProfile(profile)) {
    icons += base::UTF8ToUTF16(workspace->icon);
  }
  return icons;
}

}  // namespace uw
