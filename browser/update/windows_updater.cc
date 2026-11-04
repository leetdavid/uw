// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/update/windows_updater.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "base/version_info/version_info.h"
#include "build/buildflag.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/first_run/upgrade_util.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/installer/util/install_util.h"
#include "crypto/hash.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"
#include "url/gurl.h"
#include "uw/browser/branding/release_buildflags.h"
#include "uw/browser/update/update_check.h"

namespace uw {
namespace {

// Points a build at another list of releases, to test an update.
constexpr char kFeedSwitch[] = "uw-update-feed";

constexpr char kFeedAsset[] = "uw-windows-update.json";
constexpr char kSignatureAsset[] = "uw-windows-update.json.sig";

constexpr base::TimeDelta kFirstCheck = base::Minutes(3);
constexpr base::TimeDelta kCheckEvery = base::Hours(5);
constexpr size_t kMaxReleases = 4 * 1024 * 1024;
constexpr size_t kMaxFeed = 16 * 1024;
constexpr int64_t kMaxInstaller = 1024 * 1024 * 1024;

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("uw_windows_update", R"(
      semantics {
        sender: "uw updates"
        description:
          "uw for Windows looks for a newer release on GitHub every 5 hours "
          "and downloads its installer."
        trigger: "A few minutes after launch, then every 5 hours, and when "
                 "the user opens About uw."
        data: "None. The requests carry no user data."
        destination: OTHER
        destination_other: "GitHub's releases for uw."
      }
      policy {
        cookies_allowed: NO
        setting: "None."
        policy_exception_justification: "uw's own updates."
      })");

// The installer either installed the update or, with uw running, staged it
// for the next relaunch. See chrome/installer/util/util_constants.h.
constexpr int kInstalled = 0;
constexpr int kStaged = 3;

std::unique_ptr<network::SimpleURLLoader> Request(const GURL& url,
                                                  bool api = false) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  if (api) {
    request->headers.SetHeader("Accept", "application/vnd.github+json");
  }
  return network::SimpleURLLoader::Create(std::move(request),
                                          kTrafficAnnotation);
}

// The download addresses of a release's feed, its signature, and its
// installer, from GitHub's answer for one release or a list of them, newest
// first.
struct ReleaseAssets {
  GURL feed;
  GURL signature;
  std::vector<std::pair<std::string, GURL>> files;
};

std::optional<ReleaseAssets> AssetsOf(const base::DictValue& release) {
  const base::ListValue* assets = release.FindList("assets");
  if (!assets) {
    return std::nullopt;
  }
  ReleaseAssets found;
  for (const base::Value& asset : *assets) {
    const base::DictValue* dict = asset.GetIfDict();
    const std::string* name = dict ? dict->FindString("name") : nullptr;
    const std::string* url =
        dict ? dict->FindString("browser_download_url") : nullptr;
    if (!name || !url) {
      continue;
    }
    GURL address(*url);
    if (*name == kFeedAsset) {
      found.feed = address;
    } else if (*name == kSignatureAsset) {
      found.signature = address;
    }
    found.files.emplace_back(*name, address);
  }
  if (!found.feed.is_valid() || !found.signature.is_valid()) {
    return std::nullopt;
  }
  return found;
}

std::optional<ReleaseAssets> NewestRelease(const std::string& answer) {
  std::optional<base::Value> value =
      base::JSONReader::Read(answer, base::JSON_PARSE_RFC);
  if (!value) {
    return std::nullopt;
  }
  if (const base::DictValue* release = value->GetIfDict()) {
    return AssetsOf(*release);
  }
  if (const base::ListValue* releases = value->GetIfList()) {
    for (const base::Value& release : *releases) {
      if (const base::DictValue* dict = release.GetIfDict()) {
        if (std::optional<ReleaseAssets> assets = AssetsOf(*dict)) {
          return assets;
        }
      }
    }
  }
  return std::nullopt;
}

// The version uw installed last, which is newer than the running one once an
// update is staged. Also makes the folder downloads go to.
base::Version InstalledVersion(const base::FilePath& downloads) {
  base::CreateDirectory(downloads);
  return InstallUtil::GetChromeVersion(/*system_install=*/false);
}

bool MatchesFeed(const base::FilePath& installer, const UpdateFeed& feed) {
  base::File file(installer, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid() || file.GetLength() != feed.size) {
    return false;
  }
  crypto::hash::Hasher hasher(crypto::hash::kSha256);
  std::vector<uint8_t> chunk(1024 * 1024);
  while (true) {
    std::optional<size_t> read = file.ReadAtCurrentPos(chunk);
    if (!read) {
      return false;
    }
    if (*read == 0) {
      break;
    }
    hasher.Update(base::span(chunk).first(*read));
  }
  std::array<uint8_t, crypto::hash::kSha256Size> digest;
  hasher.Finish(digest);
  return base::ToLowerASCII(base::HexEncode(digest)) ==
         base::ToLowerASCII(feed.sha256);
}

// Checks the download against the feed, then runs it. Returns whether the new
// uw is installed or staged.
bool Install(const base::FilePath& installer, const UpdateFeed& feed) {
  if (!MatchesFeed(installer, feed)) {
    LOG(WARNING) << "uw update: the installer does not match its feed";
    base::DeleteFile(installer);
    return false;
  }
  base::CommandLine command(installer);
  command.AppendSwitch("do-not-launch-chrome");
  base::Process process = base::LaunchProcess(command, base::LaunchOptions());
  int code = -1;
  bool exited = process.IsValid() &&
                process.WaitForExitWithTimeout(base::Minutes(10), &code);
  base::DeleteFile(installer);
  if (!exited || (code != kInstalled && code != kStaged)) {
    LOG(WARNING) << "uw update: the installer failed with " << code;
    return false;
  }
  return true;
}

class Updater {
 public:
  Updater() = default;

  void Start(const GURL& releases) {
    releases_ = releases;
    timer_.Start(FROM_HERE, kFirstCheck,
                 base::BindOnce(&Updater::Check, base::Unretained(this)));
  }

  void Check() {
    timer_.Start(FROM_HERE, kCheckEvery,
                 base::BindOnce(&Updater::Check, base::Unretained(this)));
    if (busy_) {
      return;
    }
    busy_ = true;
    ReportUpdateState(UpdateState::kChecking);
    loader_ = Request(releases_, /*api=*/true);
    loader_->DownloadToString(
        g_browser_process->shared_url_loader_factory().get(),
        base::BindOnce(&Updater::OnReleases, base::Unretained(this)),
        kMaxReleases);
  }

 private:
  void OnReleases(std::optional<std::string> answer) {
    if (!answer) {
      return Done(UpdateState::kFailed);
    }
    std::optional<ReleaseAssets> assets = NewestRelease(*answer);
    if (!assets) {
      return Done(UpdateState::kUpToDate);
    }
    assets_ = std::move(*assets);
    loader_ = Request(assets_.signature);
    loader_->DownloadToString(
        g_browser_process->shared_url_loader_factory().get(),
        base::BindOnce(&Updater::OnSignature, base::Unretained(this)),
        kMaxFeed);
  }

  void OnSignature(std::optional<std::string> signature) {
    if (!signature) {
      return Done(UpdateState::kFailed);
    }
    signature_ = std::move(*signature);
    loader_ = Request(assets_.feed);
    loader_->DownloadToString(
        g_browser_process->shared_url_loader_factory().get(),
        base::BindOnce(&Updater::OnFeed, base::Unretained(this)), kMaxFeed);
  }

  void OnFeed(std::optional<std::string> text) {
    std::optional<UpdateFeed> feed =
        text ? ReadUpdateFeed(*text, signature_) : std::nullopt;
    if (!feed) {
      return Done(UpdateState::kFailed);
    }
    if (feed->version <= version_info::GetVersion()) {
      return Done(UpdateState::kUpToDate);
    }
    feed_ = std::move(*feed);
    if (!base::PathService::Get(chrome::DIR_USER_DATA, &downloads_)) {
      return Done(UpdateState::kFailed);
    }
    downloads_ = downloads_.AppendASCII("uw-update");
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
        base::BindOnce(&InstalledVersion, downloads_),
        base::BindOnce(&Updater::OnInstalledVersion, base::Unretained(this)));
  }

  void OnInstalledVersion(base::Version installed) {
    // An update already waits for the relaunch.
    if (installed.IsValid() && installed >= feed_.version) {
      return Done(UpdateState::kReady);
    }
    GURL installer;
    for (const auto& [name, url] : assets_.files) {
      if (name == feed_.installer) {
        installer = url;
      }
    }
    if (!installer.is_valid() || feed_.size <= 0 ||
        feed_.size > kMaxInstaller) {
      return Done(UpdateState::kFailed);
    }
    ReportUpdateState(UpdateState::kDownloading);
    loader_ = Request(installer);
    loader_->DownloadToFile(
        g_browser_process->shared_url_loader_factory().get(),
        base::BindOnce(&Updater::OnDownloaded, base::Unretained(this)),
        downloads_.AppendASCII(feed_.installer), feed_.size);
  }

  void OnDownloaded(base::FilePath installer) {
    if (installer.empty()) {
      return Done(UpdateState::kFailed);
    }
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE,
        {base::MayBlock(), base::WithBaseSyncPrimitives(),
         base::TaskPriority::BEST_EFFORT},
        base::BindOnce(&Install, installer, feed_),
        base::BindOnce(&Updater::OnInstalled, base::Unretained(this)));
  }

  void OnInstalled(bool installed) {
    if (installed) {
      LOG(WARNING) << "uw update: " << feed_.version.GetString()
                   << " waits for a relaunch";
    }
    Done(installed ? UpdateState::kReady : UpdateState::kFailed);
  }

  void Done(UpdateState state) {
    loader_.reset();
    busy_ = false;
    ReportUpdateState(state);
  }

  GURL releases_;
  base::OneShotTimer timer_;
  bool busy_ = false;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  ReleaseAssets assets_;
  std::string signature_;
  UpdateFeed feed_;
  base::FilePath downloads_;
};

}  // namespace

std::optional<UpdateFeed> ReadUpdateFeed(std::string_view feed,
                                         std::string_view signature,
                                         std::string_view key) {
  std::optional<std::vector<uint8_t>> signed_by = base::Base64Decode(key);
  std::optional<std::vector<uint8_t>> mark =
      base::Base64Decode(base::TrimWhitespaceASCII(signature, base::TRIM_ALL));
  if (!signed_by || signed_by->size() != ED25519_PUBLIC_KEY_LEN || !mark ||
      mark->size() != ED25519_SIGNATURE_LEN ||
      !ED25519_verify(reinterpret_cast<const uint8_t*>(feed.data()),
                      feed.size(), mark->data(), signed_by->data())) {
    return std::nullopt;
  }
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(feed, base::JSON_PARSE_RFC);
  if (!dict) {
    return std::nullopt;
  }
  const std::string* version = dict->FindString("version");
  const std::string* installer = dict->FindString("installer");
  std::optional<int> size = dict->FindInt("size");
  const std::string* sha256 = dict->FindString("sha256");
  if (!version || !installer || !size || !sha256) {
    return std::nullopt;
  }
  UpdateFeed read;
  read.size = *size;
  read.version = base::Version(*version);
  read.installer = *installer;
  read.sha256 = *sha256;
  // The installer's name becomes a file name, so it may not climb out.
  if (!read.version.IsValid() || read.installer.empty() ||
      read.installer.find_first_of("/\\:") != std::string::npos ||
      read.installer.starts_with('.')) {
    return std::nullopt;
  }
  return read;
}

bool RelaunchIntoStagedUpdate(
    const base::CommandLine& command_line,
    base::OnceCallback<void(const base::CommandLine&)> carry_on) {
  if (!command_line.HasSwitch(switches::kFromInstaller)) {
    return false;
  }
  base::CommandLine launch(command_line);
  launch.RemoveSwitch(switches::kFromInstaller);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
      base::BindOnce(&upgrade_util::IsUpdatePendingRestart),
      base::BindOnce(
          [](base::CommandLine launch,
             base::OnceCallback<void(const base::CommandLine&)> carry_on,
             bool staged) {
            if (staged) {
              chrome::AttemptRelaunch();
            } else {
              std::move(carry_on).Run(launch);
            }
          },
          std::move(launch), std::move(carry_on)));
  return true;
}

void StartWindowsUpdater() {
  static base::NoDestructor<Updater> updater;
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  GURL releases(command_line.HasSwitch(kFeedSwitch)
                    ? command_line.GetSwitchValueASCII(kFeedSwitch)
                    : BUILDFLAG(UW_UPDATE_FEED_URL));
  if (releases.is_valid()) {
    updater->Start(releases);
    SetUpdateChecker(
        base::BindRepeating(&Updater::Check, base::Unretained(updater.get())));
  }
}

}  // namespace uw
