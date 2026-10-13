// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/scratchpad_label.h"

#include <algorithm>

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_view.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace uw {
namespace {

constexpr int kLabelInset = 8;
constexpr SkAlpha kLabelAlpha = 0x8C;

}  // namespace

const views::View* FirstScratchpadTab(
    const std::vector<views::View*>& children) {
  const auto first = std::ranges::find_if_not(children, [](views::View* child) {
    return views::IsViewClass<TabGroupView>(child);
  });
  return first == children.end() ? nullptr : *first;
}

void PaintScratchpadLabel(const views::View* container,
                          const std::vector<views::View*>& children,
                          gfx::Canvas* canvas) {
  const views::View* first = FirstScratchpadTab(children);
  if (!first) {
    return;
  }
  // A collapsed strip leaves no room between the last task and the tab.
  const auto above = std::ranges::find(children, first);
  const int top = first->y() - kScratchpadLabelHeight;
  if (top < (above == children.begin()
                 ? 0
                 : (*std::prev(above))->bounds().bottom())) {
    return;
  }
  const int inset =
      GetLayoutConstant(LayoutConstant::kVerticalTabStripHorizontalPadding) +
      kLabelInset;
  const gfx::Rect bounds = container->GetMirroredRect(gfx::Rect(
      inset, top, container->width() - 2 * inset, kScratchpadLabelHeight));
  canvas->DrawStringRectWithFlags(
      l10n_util::GetStringUTF16(IDS_UW_SCRATCHPAD),
      gfx::FontList().DeriveWithSizeDelta(-1).DeriveWithWeight(
          gfx::Font::Weight::SEMIBOLD),
      SkColorSetA(container->GetColorProvider()->GetColor(
                      kColorTabForegroundInactiveFrameActive),
                  kLabelAlpha),
      bounds, gfx::Canvas::TEXT_ALIGN_TO_HEAD);
}

}  // namespace uw
