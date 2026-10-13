// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/name_lock.h"

#include <string>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/automatic_organizer.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"

namespace uw {
namespace {

constexpr int kIconSize = 13;
constexpr int kPadding = 4;
constexpr SkAlpha kRestingAlpha = 0x8C;

BrowserView* WindowOf(const views::View* view) {
  const views::Widget* widget = view->GetWidget();
  return widget ? BrowserView::GetBrowserViewForNativeWindow(
                      widget->GetPrimaryWindowWidget()->GetNativeWindow())
                : nullptr;
}

}  // namespace

NameLock::NameLock(const tab_groups::TabGroupId& group)
    : ImageButton(
          base::BindRepeating(&NameLock::HandBack, base::Unretained(this))),
      group_(group) {
  const std::u16string hint = l10n_util::GetStringUTF16(IDS_UW_NAME_LOCK);
  SetTooltipText(hint);
  GetViewAccessibility().SetName(hint);
  SetBorder(views::CreateEmptyBorder(gfx::Insets(kPadding)));
  SetImageHorizontalAlignment(ALIGN_CENTER);
  SetImageVerticalAlignment(ALIGN_MIDDLE);
  SetVisible(false);
}

NameLock::~NameLock() = default;

void NameLock::AddedToWidget() {
  ImageButton::AddedToWidget();
  TabOrganizationController* controller = Controller();
  if (!controller) {
    return;
  }
  changed_ = controller->Observe(
      base::BindRepeating(&NameLock::Refresh, base::Unretained(this)));
  prefs_.Init(WindowOf(this)->browser()->GetProfile()->GetPrefs());
  prefs_.Add(kOrganizationEnabledPref,
             base::BindRepeating(&NameLock::Refresh, base::Unretained(this)));
  Refresh();
}

void NameLock::RemovedFromWidget() {
  changed_ = {};
  prefs_.Reset();
  ImageButton::RemovedFromWidget();
}

void NameLock::OnThemeChanged() {
  ImageButton::OnThemeChanged();
  const SkColor color =
      GetColorProvider()->GetColor(kColorTabForegroundInactiveFrameActive);
  SetImageModel(STATE_NORMAL,
                ui::ImageModel::FromVectorIcon(
                    vector_icons::kLockIcon, SkColorSetA(color, kRestingAlpha),
                    kIconSize));
  const ui::ImageModel hot =
      ui::ImageModel::FromVectorIcon(vector_icons::kLockIcon, color, kIconSize);
  SetImageModel(STATE_HOVERED, hot);
  SetImageModel(STATE_PRESSED, hot);
}

TabOrganizationController* NameLock::Controller() const {
  BrowserView* window = WindowOf(this);
  return window ? TabOrganizationController::From(window->browser()) : nullptr;
}

void NameLock::Refresh() {
  const TabOrganizationController* controller = Controller();
  SetVisible(controller && !prefs_.IsEmpty() &&
             prefs_.prefs()->GetBoolean(kOrganizationEnabledPref) &&
             controller->HasManualName(group_.ToString()));
}

void NameLock::HandBack() {
  if (TabOrganizationController* controller = Controller()) {
    controller->NameAutomatically(group_.ToString());
  }
}

BEGIN_METADATA(NameLock)
END_METADATA

NamingSpinner::NamingSpinner(const tab_groups::TabGroupId& group)
    : Throbber(kIconSize), group_(group) {
  GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_UW_ACTIVITY_NEEDS_NAME));
  SetProperty(views::kMarginsKey, gfx::Insets::VH(0, kPadding));
  SetVisible(false);
}

NamingSpinner::~NamingSpinner() = default;

void NamingSpinner::AddedToWidget() {
  Throbber::AddedToWidget();
  BrowserView* window = WindowOf(this);
  AutomaticOrganizer* organizer =
      window ? AutomaticOrganizer::From(window->browser()) : nullptr;
  if (!organizer) {
    return;
  }
  naming_ = organizer->ObserveNaming(
      base::BindRepeating(&NamingSpinner::Refresh, base::Unretained(this)));
  Refresh();
}

void NamingSpinner::RemovedFromWidget() {
  naming_ = {};
  Stop();
  Throbber::RemovedFromWidget();
}

void NamingSpinner::Refresh() {
  BrowserView* window = WindowOf(this);
  const AutomaticOrganizer* organizer =
      window ? AutomaticOrganizer::From(window->browser()) : nullptr;
  const bool naming = organizer && organizer->IsNaming(group_.ToString());
  SetVisible(naming);
  if (naming) {
    Start();
  } else {
    Stop();
  }
}

BEGIN_METADATA(NamingSpinner)
END_METADATA

}  // namespace uw
