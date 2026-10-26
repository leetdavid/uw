// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/workspaces/icon_picker.h"

#include <memory>
#include <string_view>
#include <utility>

#include "base/i18n/break_iterator.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/emoji/emoji_panel_helper.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "uw/browser/ui/look/menu_card.h"
#include "uw/browser/workspaces/workspace_list.h"

namespace uw {
namespace {

constexpr int kFieldSize = 34;
constexpr int kFieldRadius = 9;
constexpr int kFieldEmojiSize = 17;
constexpr int kCellWidth = 32;
constexpr int kCellHeight = 30;
constexpr int kCellRadius = 8;
constexpr int kCellEmojiSize = 16;
constexpr int kGap = 2;
constexpr int kRingWidth = 2;
constexpr int kColumns = 8;

gfx::FontList EmojiFont(int size) {
  const gfx::FontList base;
  return base.DeriveWithSizeDelta(size - base.GetFontSize());
}

// The last character of `text`, as a reader sees one, emoji and all.
std::u16string LastCharacter(const std::u16string& text) {
  base::i18n::BreakIterator characters(
      text, base::i18n::BreakIterator::BREAK_CHARACTER);
  std::u16string last;
  if (characters.Init()) {
    while (characters.Advance()) {
      last = std::u16string(characters.GetString());
    }
  }
  return last;
}

// One emoji of the grid, ringed when it is the picked one.
class IconCell : public views::Button {
  METADATA_HEADER(IconCell, views::Button)

 public:
  IconCell(PressedCallback callback, std::string_view icon)
      : Button(std::move(callback)), icon_(icon) {
    SetPreferredSize(gfx::Size(kCellWidth, kCellHeight));
    SetLayoutManager(std::make_unique<views::FillLayout>());
    auto* label =
        AddChildView(std::make_unique<views::Label>(base::UTF8ToUTF16(icon_)));
    label->SetFontList(EmojiFont(kCellEmojiSize));
    label->SetBackgroundColor(SK_ColorTRANSPARENT);
    label->SetSubpixelRenderingEnabled(false);
    GetViewAccessibility().SetName(base::UTF8ToUTF16(icon_));
  }

  const std::string& icon() const { return icon_; }

  void SetSelected(bool selected) {
    selected_ = selected;
    GetViewAccessibility().SetIsSelected(selected);
    SchedulePaint();
  }

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (!selected_ && GetState() != STATE_HOVERED) {
      return;
    }
    const ui::ColorProvider* colors = GetColorProvider();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kColorMenuCardHover));
    gfx::RectF bounds(GetLocalBounds());
    canvas->DrawRoundRect(bounds, kCellRadius, flags);
    if (selected_) {
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(kRingWidth);
      flags.setColor(colors->GetColor(kColorMenuCardAccent));
      bounds.Inset(kRingWidth / 2.0f);
      canvas->DrawRoundRect(bounds, kCellRadius - kRingWidth / 2.0f, flags);
    }
  }

  void StateChanged(ButtonState old_state) override { SchedulePaint(); }

 private:
  const std::string icon_;
  bool selected_ = false;
};

BEGIN_METADATA(IconCell)
END_METADATA

}  // namespace

IconField::IconField(const std::string& icon, IconPicked typed)
    : typed_(std::move(typed)), icon_(icon) {
  SetController(this);
  SetText(base::UTF8ToUTF16(icon_));
  SetFontList(EmojiFont(kFieldEmojiSize));
  SetHorizontalAlignment(gfx::ALIGN_CENTER);
  SetPreferredSize(gfx::Size(kFieldSize, kFieldSize));
  SetBackgroundColor(kColorMenuCardHover);
  SetBorder(views::CreateRoundedRectBorder(kRingWidth, kFieldRadius,
                                           kColorMenuCardHover));
  SetAccessibleName(l10n_util::GetStringUTF16(IDS_UW_WORKSPACE_ICON));
}

IconField::~IconField() = default;

void IconField::SetIcon(const std::string& icon) {
  icon_ = icon;
  SetText(base::UTF8ToUTF16(icon_));
}

void IconField::OpenEmojiPicker() {
  RequestFocus();
  SelectAll(/*reversed=*/false);
  ui::ShowEmojiPanel();
}

void IconField::ContentsChanged(views::Textfield* sender,
                                const std::u16string& new_contents) {
  const std::u16string last = LastCharacter(new_contents);
  if (last.empty()) {
    SetText(base::UTF8ToUTF16(icon_));
    return;
  }
  if (last != new_contents) {
    SetText(last);
  }
  icon_ = base::UTF16ToUTF8(last);
  typed_.Run(icon_);
}

BEGIN_METADATA(IconField)
END_METADATA

IconGrid::IconGrid(const std::string& selected,
                   IconPicked picked,
                   base::RepeatingClosure more) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), kGap));
  views::View* row = nullptr;
  int column = 0;
  for (std::string_view icon : WorkspaceList::SuggestedIcons()) {
    if (column++ % kColumns == 0) {
      row = AddChildView(std::make_unique<views::View>());
      row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kGap));
    }
    row->AddChildView(std::make_unique<IconCell>(
        base::BindRepeating(picked, std::string(icon)), icon));
  }
  auto* link = AddChildView(std::make_unique<views::Link>(
      l10n_util::GetStringUTF16(IDS_UW_MORE_EMOJI)));
  link->SetCallback(std::move(more));
  link->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  link->SetEnabledColor(kColorMenuCardAccent);
  link->SetForceUnderline(false);
  link->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(2, 2, 0, 0));
  Select(selected);
}

IconGrid::~IconGrid() = default;

void IconGrid::Select(const std::string& icon) {
  for (views::View* row : children()) {
    for (views::View* child : row->children()) {
      if (auto* cell = views::AsViewClass<IconCell>(child)) {
        cell->SetSelected(cell->icon() == icon);
      }
    }
  }
}

BEGIN_METADATA(IconGrid)
END_METADATA

}  // namespace uw
