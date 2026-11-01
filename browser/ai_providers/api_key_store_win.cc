// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ai_providers/api_key_store.h"

#include <windows.h>

#include <dpapi.h>

#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/common/chrome_paths.h"

// Each key is a file in uw's user data folder, encrypted with DPAPI for the
// Windows user, as Chromium's own os_crypt protects its key on Windows. Every
// profile shares them, as every profile on the Mac shares the Keychain's.
// Tests start with a user data folder of their own, so they never meet the
// user's keys.

namespace uw {
namespace {

base::FilePath KeyFile(const std::string& service, const std::string& account) {
  base::FilePath folder;
  base::PathService::Get(chrome::DIR_USER_DATA, &folder);
  return folder.AppendASCII(service).AppendASCII(base::HexEncode(account));
}

}  // namespace

ApiKeyStore::ApiKeyStore() : service_name_("uw-ai") {}
ApiKeyStore::~ApiKeyStore() = default;

bool ApiKeyStore::SetKey(const std::string& account, const std::string& key) {
  if (account.empty() || key.empty()) {
    return false;
  }
  DATA_BLOB plain = {static_cast<DWORD>(key.size()),
                     reinterpret_cast<BYTE*>(const_cast<char*>(key.data()))};
  DATA_BLOB sealed = {};
  if (!::CryptProtectData(&plain, L"uw", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &sealed)) {
    return false;
  }
  const std::string bytes(reinterpret_cast<const char*>(sealed.pbData),
                          sealed.cbData);
  ::LocalFree(sealed.pbData);
  const base::FilePath file = KeyFile(service_name_, account);
  return base::CreateDirectory(file.DirName()) &&
         base::ImportantFileWriter::WriteFileAtomically(file, bytes);
}

std::optional<std::string> ApiKeyStore::GetKey(const std::string& account) {
  std::string bytes;
  if (account.empty() ||
      !base::ReadFileToString(KeyFile(service_name_, account), &bytes)) {
    return std::nullopt;
  }
  DATA_BLOB sealed = {static_cast<DWORD>(bytes.size()),
                      reinterpret_cast<BYTE*>(bytes.data())};
  DATA_BLOB plain = {};
  if (!::CryptUnprotectData(&sealed, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &plain)) {
    return std::nullopt;
  }
  std::string key(reinterpret_cast<const char*>(plain.pbData), plain.cbData);
  ::SecureZeroMemory(plain.pbData, plain.cbData);
  ::LocalFree(plain.pbData);
  return key;
}

bool ApiKeyStore::DeleteKey(const std::string& account) {
  const base::FilePath file = KeyFile(service_name_, account);
  return !account.empty() && base::PathExists(file) && base::DeleteFile(file);
}

}  // namespace uw
