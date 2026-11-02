// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/tray/uw_tray.h"

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"  // nogncheck
#include "chrome/browser/status_icons/status_icon.h"
#include "chrome/browser/status_icons/status_icon_menu_model.h"
#include "chrome/browser/status_icons/status_icon_observer.h"
#include "chrome/browser/status_icons/status_tray.h"
#include "chrome/browser/ui/browser_commands.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/win/app_icon.h"
#include "chrome/grit/branded_strings.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/base_window.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_family.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"

namespace uw {
namespace {

enum Command { kNewWindow, kExit };

class Tray : public StatusIconObserver, public StatusIconMenuModel::Delegate {
 public:
  Tray() {
    StatusTray* tray = g_browser_process->status_tray();
    std::unique_ptr<gfx::ImageFamily> icons = GetAppIconImageFamily();
    if (!tray || !icons) {
      return;
    }
    gfx::ImageSkia image(
        gfx::ImageSkiaRep(icons->CreateExact(16, 16).AsBitmap(), 1.0f));
    image.AddRepresentation(
        gfx::ImageSkiaRep(icons->CreateExact(32, 32).AsBitmap(), 2.0f));
    StatusIcon* icon =
        tray->CreateStatusIcon(StatusTray::OTHER_ICON, image,
                               l10n_util::GetStringUTF16(IDS_PRODUCT_NAME));
    if (!icon) {
      return;
    }
    auto menu = std::make_unique<StatusIconMenuModel>(this);
    menu->AddItem(kNewWindow, l10n_util::GetStringUTF16(IDS_NEW_WINDOW));
    menu->AddItem(kExit, l10n_util::GetStringUTF16(IDS_EXIT));
    icon->SetContextMenu(std::move(menu));
    icon->AddObserver(this);
  }

 private:
  // StatusIconObserver:
  void OnStatusIconClicked() override {
    for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
      if (window->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
          window->GetWindow()->IsVisible()) {
        window->GetWindow()->Activate();
        return;
      }
    }
    NewWindow();
  }

  // StatusIconMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == kExit) {
      chrome::AttemptUserExit();
    } else {
      NewWindow();
    }
  }

  void NewWindow() {
    if (Profile* profile = ProfileManager::GetLastUsedProfileAllowedByPolicy()) {
      chrome::NewEmptyWindow(profile);
    }
  }
};

}  // namespace

void StartTray() {
  static base::NoDestructor<Tray> tray;
}

}  // namespace uw
