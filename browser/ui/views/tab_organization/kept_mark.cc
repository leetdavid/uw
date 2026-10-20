// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/kept_mark.h"

#include <iterator>
#include <optional>
#include <string>

#include "base/functional/bind.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"

namespace uw {
namespace {

constexpr int kIconSize = 13;
constexpr int kPadding = 4;
constexpr SkAlpha kRestingAlpha = 0x8C;

// An anchor: a ring, a shank, and the arms.
constexpr gfx::PathElement kAnchorPath[] = {gfx::CANVAS_DIMENSIONS,
                                            24,
                                            gfx::STROKE,
                                            2,
                                            gfx::CIRCLE,
                                            12,
                                            5,
                                            3,
                                            gfx::NEW_PATH,
                                            gfx::STROKE,
                                            2,
                                            gfx::MOVE_TO,
                                            12,
                                            22,
                                            gfx::V_LINE_TO,
                                            8,
                                            gfx::NEW_PATH,
                                            gfx::STROKE,
                                            2,
                                            gfx::MOVE_TO,
                                            5,
                                            12,
                                            gfx::H_LINE_TO,
                                            2,
                                            gfx::ARC_TO,
                                            10,
                                            10,
                                            0,
                                            0,
                                            0,
                                            22,
                                            12,
                                            gfx::H_LINE_TO,
                                            19};
constexpr gfx::VectorIconRep kAnchorReps[] = {{kAnchorPath}};
constexpr gfx::VectorIcon kAnchorIcon(kAnchorReps,
                                      std::size(kAnchorReps),
                                      "uw_anchor");

}  // namespace

KeptMark::KeptMark(tabs::TabHandle tab)
    : ImageButton(
          base::BindRepeating(&KeptMark::LetGo, base::Unretained(this))),
      tab_(tab) {
  const std::u16string hint = l10n_util::GetStringUTF16(IDS_UW_KEPT_TAB);
  SetTooltipText(hint);
  GetViewAccessibility().SetName(hint);
  SetBorder(views::CreateEmptyBorder(gfx::Insets(kPadding)));
  SetImageHorizontalAlignment(ALIGN_CENTER);
  SetImageVerticalAlignment(ALIGN_MIDDLE);
  SetVisible(false);
}

KeptMark::~KeptMark() = default;

void KeptMark::AddedToWidget() {
  ImageButton::AddedToWidget();
  if (TabOrganizationController* controller = Controller()) {
    changed_ = controller->Observe(
        base::BindRepeating(&KeptMark::Refresh, base::Unretained(this)));
  }
  Refresh();
}

void KeptMark::RemovedFromWidget() {
  changed_ = {};
  ImageButton::RemovedFromWidget();
}

void KeptMark::OnThemeChanged() {
  ImageButton::OnThemeChanged();
  const SkColor color =
      GetColorProvider()->GetColor(kColorTabForegroundInactiveFrameActive);
  SetImageModel(STATE_NORMAL,
                ui::ImageModel::FromVectorIcon(
                    kAnchorIcon, SkColorSetA(color, kRestingAlpha), kIconSize));
  const ui::ImageModel hot =
      ui::ImageModel::FromVectorIcon(kAnchorIcon, color, kIconSize);
  SetImageModel(STATE_HOVERED, hot);
  SetImageModel(STATE_PRESSED, hot);
}

TabOrganizationController* KeptMark::Controller() const {
  tabs::TabInterface* tab = tab_.Get();
  return tab ? TabOrganizationController::From(tab->GetBrowserWindowInterface())
             : nullptr;
}

void KeptMark::Refresh() {
  const TabOrganizationController* controller = Controller();
  const std::optional<PageId> id =
      controller ? controller->IdFor(tab_.Get()) : std::nullopt;
  const bool kept = id && controller->IsKept(*id);
  if (kept == kept_) {
    return;
  }
  kept_ = kept;
  parent()->InvalidateLayout();
}

void KeptMark::LetGo() {
  TabOrganizationController* controller = Controller();
  const std::optional<PageId> id =
      controller ? controller->IdFor(tab_.Get()) : std::nullopt;
  if (id && controller->IsKept(*id)) {
    controller->ToggleKept({*id});
  }
}

BEGIN_METADATA(KeptMark)
END_METADATA

}  // namespace uw
