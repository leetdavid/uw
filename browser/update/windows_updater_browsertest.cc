// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/update/windows_updater.h"

#include <string>

#include "base/base64.h"
#include "base/containers/span.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"

namespace uw {
namespace {

constexpr char kFeed[] =
    R"({"version":"153.0.8010.5302","installer":"uw-windows-x64-setup.exe",)"
    R"("size":176499200,"sha256":"88ad512e"})";

struct Signer {
  Signer() { ED25519_keypair(public_key, private_key); }

  std::string Key() const { return base::Base64Encode(public_key); }

  std::string Sign(std::string_view feed) const {
    uint8_t signature[ED25519_SIGNATURE_LEN];
    ED25519_sign(signature, reinterpret_cast<const uint8_t*>(feed.data()),
                 feed.size(), private_key);
    return base::Base64Encode(signature);
  }

  uint8_t public_key[ED25519_PUBLIC_KEY_LEN];
  uint8_t private_key[ED25519_PRIVATE_KEY_LEN];
};

TEST(WindowsUpdaterTest, ASignedFeedNamesTheInstaller) {
  Signer signer;
  std::optional<UpdateFeed> feed =
      ReadUpdateFeed(kFeed, signer.Sign(kFeed) + "\n", signer.Key());
  ASSERT_TRUE(feed);
  EXPECT_EQ(feed->version, base::Version("153.0.8010.5302"));
  EXPECT_EQ(feed->installer, "uw-windows-x64-setup.exe");
  EXPECT_EQ(feed->size, 176499200);
  EXPECT_EQ(feed->sha256, "88ad512e");
}

TEST(WindowsUpdaterTest, AChangedFeedOrAnotherKeyIsRefused) {
  Signer signer;
  std::string changed = kFeed;
  changed.replace(changed.find("5302"), 4, "5399");
  EXPECT_FALSE(ReadUpdateFeed(changed, signer.Sign(kFeed), signer.Key()));
  EXPECT_FALSE(ReadUpdateFeed(kFeed, Signer().Sign(kFeed), signer.Key()));
  EXPECT_FALSE(ReadUpdateFeed(kFeed, signer.Sign(kFeed)));
  EXPECT_FALSE(ReadUpdateFeed(kFeed, "", signer.Key()));
}

TEST(WindowsUpdaterTest, AnInstallerNameMayNotLeaveItsFolder) {
  Signer signer;
  for (std::string name : {"..\\uw.exe", "a/b.exe", "C:uw.exe", ".hidden"}) {
    std::string feed = R"({"version":"1.2.3.4","installer":")" + name +
                       R"(","size":1,"sha256":"00"})";
    std::string escaped = feed;
    for (size_t at = 0; (at = escaped.find('\\', at)) != std::string::npos;
         at += 2) {
      escaped.insert(at, "\\");
    }
    EXPECT_FALSE(ReadUpdateFeed(escaped, signer.Sign(escaped), signer.Key()))
        << name;
  }
}

}  // namespace
}  // namespace uw
