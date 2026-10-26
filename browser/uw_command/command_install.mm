// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/uw_command/command_install.h"

#include <Security/Security.h>

#include <vector>

#include "base/apple/bundle_locations.h"
#include "base/files/file_util.h"
#include "base/mac/authorization_util.h"
#include "base/mac/scoped_authorizationref.h"
#include "base/no_destructor.h"

namespace uw {

namespace {

base::FilePath& LinkForTesting() {
  static base::NoDestructor<base::FilePath> link;
  return *link;
}

// Runs `script` as root with `arguments` as $1 and up, after macOS asks for an
// administrator's password. The shell prints its pid first, as
// ExecuteWithPrivilegesAndWait needs.
CommandInstall RunAsRoot(const char* script,
                         std::vector<const char*> arguments) {
  base::mac::ScopedAuthorizationRef authorization =
      base::mac::AuthorizationCreateToRunAsRoot(
          CFSTR("uw is changing the uw command in /usr/local/bin."));
  if (!authorization) {
    return CommandInstall::kCancelled;
  }
  std::vector<const char*> shell = {"-c", script, "sh"};
  shell.insert(shell.end(), arguments.begin(), arguments.end());
  shell.push_back(nullptr);
  int exit_status = -1;
  const OSStatus status = base::mac::ExecuteWithPrivilegesAndWait(
      authorization, "/bin/sh", kAuthorizationFlagDefaults, shell.data(),
      /*pipe=*/nullptr, &exit_status);
  return status == errAuthorizationSuccess && exit_status == 0
             ? CommandInstall::kDone
             : CommandInstall::kFailed;
}

}  // namespace

base::FilePath CommandLink() {
  return LinkForTesting().empty() ? base::FilePath(kCommandLink)
                                  : LinkForTesting();
}

void SetCommandLinkForTesting(const base::FilePath& link) {
  LinkForTesting() = link;
}

base::FilePath CommandBinary() {
  return base::apple::OuterBundlePath().Append("Contents/Helpers/uw");
}

bool LinksInto(const base::FilePath& link, const base::FilePath& app) {
  base::FilePath target;
  if (!base::ReadSymbolicLink(link, &target)) {
    return false;
  }
  return app.IsParent(target.IsAbsolute() ? target
                                          : link.DirName().Append(target));
}

bool CommandInstalled() {
  return LinksInto(CommandLink(), base::apple::OuterBundlePath());
}

CommandInstall LinkCommand(const base::FilePath& link,
                           const base::FilePath& binary) {
  if (base::PathIsWritable(link.DirName())) {
    base::DeleteFile(link);
    return base::CreateSymbolicLink(binary, link) ? CommandInstall::kDone
                                                  : CommandInstall::kFailed;
  }
  return RunAsRoot(R"sh(echo $$ && mkdir -p "$(dirname "$2")" && ln -sfn "$1" "$2")sh",
                   {binary.value().c_str(), link.value().c_str()});
}

CommandInstall UnlinkCommand(const base::FilePath& link) {
  if (base::PathIsWritable(link.DirName())) {
    return base::DeleteFile(link) ? CommandInstall::kDone
                                  : CommandInstall::kFailed;
  }
  return RunAsRoot(R"sh(echo $$ && rm -f "$1")sh", {link.value().c_str()});
}

}  // namespace uw
