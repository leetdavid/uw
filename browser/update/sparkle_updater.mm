// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/update/sparkle_updater.h"

#import <Foundation/Foundation.h>

#include <optional>
#include <utility>

#include "base/apple/bundle_locations.h"
#include "base/functional/callback.h"
#include "base/version.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/upgrade_detector/build_state.h"
#include "uw/browser/update/pending_update.h"

// The parts of Sparkle's SPUUpdater and SPUStandardUserDriver uw calls. uw
// loads Sparkle.framework at runtime, so it does not link its headers.
@protocol UwSparkleUpdater <NSObject>
- (instancetype)initWithHostBundle:(NSBundle*)hostBundle
                 applicationBundle:(NSBundle*)applicationBundle
                        userDriver:(id)userDriver
                          delegate:(id)delegate;
- (BOOL)startUpdater:(NSError* __autoreleasing*)error;
@end

@protocol UwSparkleUserDriver <NSObject>
- (instancetype)initWithHostBundle:(NSBundle*)hostBundle delegate:(id)delegate;
@end

@interface UwSparkleDelegate : NSObject
@end

@implementation UwSparkleDelegate

- (BOOL)updater:(id)updater
       willInstallUpdateOnQuit:(id)item
    immediateInstallationBlock:(void (^)(void))immediateInstallHandler {
  uw::OnUpdateDownloaded(base::BindRepeating(^{
    immediateInstallHandler();
  }));
  return YES;
}

@end

namespace uw {
namespace {

UwSparkleDelegate* g_delegate;
id g_updater;

}  // namespace

void StartSparkleUpdater() {
  NSBundle* app = base::apple::OuterBundle();
  if (g_updater || ![app objectForInfoDictionaryKey:@"SUFeedURL"]) {
    return;
  }
  NSBundle* sparkle = [NSBundle
      bundleWithURL:[app.privateFrameworksURL
                        URLByAppendingPathComponent:@"Sparkle.framework"]];
  if (![sparkle load]) {
    return;
  }
  Class updater_class = NSClassFromString(@"SPUUpdater");
  Class driver_class = NSClassFromString(@"SPUStandardUserDriver");
  if (!updater_class || !driver_class) {
    return;
  }
  g_delegate = [[UwSparkleDelegate alloc] init];
  id<UwSparkleUserDriver> driver =
      [(id<UwSparkleUserDriver>)[driver_class alloc] initWithHostBundle:app
                                                               delegate:nil];
  id<UwSparkleUpdater> updater = [(id<UwSparkleUpdater>)[updater_class alloc]
      initWithHostBundle:app
       applicationBundle:app
              userDriver:driver
                delegate:g_delegate];
  if ([updater startUpdater:nil]) {
    g_updater = updater;
  }
}

void OnUpdateDownloaded(base::RepeatingClosure install) {
  SetPendingUpdate(std::move(install));
  g_browser_process->GetBuildState()->SetUpdate(
      BuildState::UpdateType::kNormalUpdate, base::Version(), std::nullopt);
}

}  // namespace uw
