// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/scratchpad_label.h"

#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_view.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace uw {
namespace {

constexpr int kLabelHeight = 24;
constexpr int kLabelInset = 8;

}  // namespace

int ScratchpadLabelHeight(const std::vector<views::View*>& children) {
  return children.empty() || views::IsViewClass<TabGroupView>(children.front())
             ? 0
             : kLabelHeight;
}

void PaintScratchpadLabel(const views::View* container,
                          const std::vector<views::View*>& children,
                          gfx::Canvas* canvas) {
  if (ScratchpadLabelHeight(children) == 0 ||
      children.front()->y() < kLabelHeight) {
    return;
  }
  const int inset =
      GetLayoutConstant(LayoutConstant::kVerticalTabStripHorizontalPadding) +
      kLabelInset;
  const gfx::Rect bounds = container->GetMirroredRect(
      gfx::Rect(inset, 0, container->width() - 2 * inset, kLabelHeight));
  canvas->DrawStringRectWithFlags(
      l10n_util::GetStringUTF16(IDS_UW_SCRATCHPAD),
      gfx::FontList().DeriveWithSizeDelta(-1).DeriveWithWeight(
          gfx::Font::Weight::SEMIBOLD),
      container->GetColorProvider()->GetColor(ui::kColorSysOnSurfaceSubtle),
      bounds, gfx::Canvas::TEXT_ALIGN_TO_HEAD);
}

}  // namespace uw
