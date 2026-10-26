// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/split_mark.h"

#include <iterator>
#include <string>
#include <vector>

#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/grit/generated_resources.h"
#include "components/tabs/public/split_tab_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"

namespace uw {
namespace {

constexpr int kIconSize = 13;
constexpr int kPadding = 4;
constexpr SkAlpha kRestingAlpha = 0x8C;

// Two columns in a rounded frame.
constexpr gfx::PathElement kSplitPath[] = {
    gfx::CANVAS_DIMENSIONS, 24, gfx::STROKE, 2, gfx::ROUND_RECT, 3, 3, 18, 18,
    2, gfx::NEW_PATH, gfx::STROKE, 2, gfx::MOVE_TO, 12, 3, gfx::V_LINE_TO, 21};
constexpr gfx::VectorIconRep kSplitReps[] = {{kSplitPath}};
constexpr gfx::VectorIcon kSplitIcon(kSplitReps,
                                     std::size(kSplitReps),
                                     "uw_split");

}  // namespace

SplitMark::SplitMark(tabs::TabHandle tab) : tab_(tab) {
  const std::u16string hint =
      l10n_util::GetStringUTF16(IDS_SPLIT_TAB_NEW_VERTICAL);
  SetTooltipText(hint);
  GetViewAccessibility().SetName(hint);
  SetBorder(views::CreateEmptyBorder(gfx::Insets(kPadding)));
  SetVisible(false);
}

SplitMark::~SplitMark() = default;

bool SplitMark::ShownOnRow(bool hovered) const {
  tabs::TabInterface* tab = tab_.Get();
  if (hovered || !tab || tab->IsActivated() || !tab->GetSplit()) {
    return false;
  }
  const split_tabs::SplitTabData* split =
      tab->GetBrowserWindowInterface()->GetTabStripModel()->GetSplitData(
          *tab->GetSplit());
  if (!split) {
    return false;
  }
  const std::vector<tabs::TabInterface*> pair = split->ListTabs();
  return !pair.empty() && pair.front() == tab;
}

void SplitMark::OnThemeChanged() {
  ImageView::OnThemeChanged();
  const SkColor color =
      GetColorProvider()->GetColor(kColorTabForegroundInactiveFrameActive);
  SetImage(ui::ImageModel::FromVectorIcon(
      kSplitIcon, SkColorSetA(color, kRestingAlpha), kIconSize));
}

BEGIN_METADATA(SplitMark)
END_METADATA

}  // namespace uw
