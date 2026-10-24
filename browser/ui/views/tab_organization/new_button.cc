// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/new_button.h"

#include <utility>

#include "base/functional/bind.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/separator.h"
#include "ui/views/view_class_properties.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/terminal/terminal_tab.h"
#include "uw/browser/ui/glass/glass.h"
#include "uw/browser/ui/glass/glass_background.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"
#include "uw/browser/workspaces/workspace_commands.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

constexpr int kHeight = 28;
constexpr int kPlusWidth = 32;
constexpr int kCaretWidth = 24;
constexpr int kPlusIconSize = 15;
constexpr int kCaretIconSize = 12;
constexpr int kDividerHeight = 14;
// The caret is the tint's text color at about 55%, as chevrons on the tint
// are.
constexpr SkAlpha kCaretAlpha = 0x8C;

std::unique_ptr<views::ImageButton> Half(views::Button::PressedCallback press,
                                         int width) {
  auto half = std::make_unique<views::ImageButton>(std::move(press));
  half->SetPreferredSize(gfx::Size(width, kHeight));
  half->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  half->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  views::InstallPillHighlightPathGenerator(half.get());
  return half;
}

}  // namespace

NewItemsMenu::NewItemsMenu(BrowserWindowInterface* window)
    : ui::SimpleMenuModel(this), window_(window) {
  AddItemWithStringId(IDC_NEW_TAB, IDS_NEW_TAB);
  AddItemWithStringId(IDC_CREATE_NEW_TAB_GROUP, IDS_UW_NEW_TASK_MAC);
  AddItemWithStringId(kNewChatCommand, IDS_UW_NEW_CHAT);
  AddItemWithStringId(kNewTerminalCommand, IDS_UW_NEW_TERMINAL);
  AddSeparator(ui::NORMAL_SEPARATOR);
  AddItemWithStringId(IDC_NEW_SPLIT_TAB, IDS_TAB_CXMENU_NEW_SPLIT_WITH_CURRENT);
  if (WorkspaceWindow::From(window)) {
    AddItemWithStringId(kNewWorkspaceCommand, IDS_UW_NEW_WORKSPACE);
  }
}

NewItemsMenu::~NewItemsMenu() = default;

void NewItemsMenu::ExecuteCommand(int command_id, int event_flags) {
  chrome::ExecuteCommand(window_, command_id);
}

bool NewItemsMenu::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accelerator) const {
  return GetCommandAccelerator(window_, command_id, accelerator);
}

NewButton::NewButton(BrowserWindowInterface* window) : window_(window) {
  SetOrientation(views::LayoutOrientation::kHorizontal);
  SetCrossAxisAlignment(views::LayoutAlignment::kCenter);
  SetBackground(CreateGlassPillBackground(
      GlassOfWindow(window), kColorNewTabButtonCRBackgroundFrameActive,
      kHeight / 2.0f));

  plus_ = AddChildView(Half(base::BindRepeating(
                                [](BrowserWindowInterface* window) {
                                  chrome::ExecuteCommand(window, IDC_NEW_TAB);
                                },
                                window_),
                            kPlusWidth));
  plus_->SetTooltipText(l10n_util::GetStringUTF16(IDS_TOOLTIP_NEW_TAB));
  plus_->set_context_menu_controller(this);

  auto* divider = AddChildView(std::make_unique<views::Separator>());
  divider->SetOrientation(views::Separator::Orientation::kVertical);
  divider->SetPreferredLength(kDividerHeight);
  divider->SetColorId(ui::kColorSysOnHeaderDivider);

  caret_ = AddChildView(
      Half(base::BindRepeating(&NewButton::ShowMenu, base::Unretained(this)),
           kCaretWidth));
  const std::u16string more = l10n_util::GetStringUTF16(IDS_UW_MORE_NEW_ITEMS);
  caret_->SetTooltipText(more);
  caret_->GetViewAccessibility().SetName(more);
}

NewButton::~NewButton() = default;

void NewButton::OnThemeChanged() {
  views::FlexLayoutView::OnThemeChanged();
  const SkColor color =
      GetColorProvider()->GetColor(kColorTabForegroundInactiveFrameActive);
  plus_->SetImageModel(views::Button::STATE_NORMAL,
                       ui::ImageModel::FromVectorIcon(vector_icons::kAdd2Icon,
                                                      color, kPlusIconSize));
  caret_->SetImageModel(views::Button::STATE_NORMAL,
                        ui::ImageModel::FromVectorIcon(
                            vector_icons::kKeyboardArrowDownIcon,
                            SkColorSetA(color, kCaretAlpha), kCaretIconSize));
  caret_->SetImageModel(
      views::Button::STATE_HOVERED,
      ui::ImageModel::FromVectorIcon(vector_icons::kKeyboardArrowDownIcon,
                                     color, kCaretIconSize));
}

void NewButton::SetCompact(bool compact) {
  for (views::View* child : children()) {
    if (child != plus_) {
      child->SetVisible(!compact);
    }
  }
}

bool NewButton::menu_showing() const {
  return menu_runner_ && menu_runner_->IsRunning();
}

void NewButton::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  ShowMenu();
}

void NewButton::ShowMenu() {
  menu_model_ = std::make_unique<NewItemsMenu>(window_);
  hover_lock_ =
      BrowserView::GetBrowserViewForBrowser(window_)
          ->tab_strip_view()
          ->GetExpandOnHoverLock(ExpandOnHoverLockType::kKeepCurrentState);
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(), views::MenuRunner::HAS_MNEMONICS,
      base::BindRepeating(&NewButton::OnMenuClosed, base::Unretained(this)));
  menu_runner_->RunMenuAt(GetWidget(), nullptr, GetBoundsInScreen(),
                          views::MenuAnchorPosition::kTopLeft,
                          ui::mojom::MenuSourceType::kNone);
  FrostOpenMenu(window_);
}

void NewButton::OnMenuClosed() {
  hover_lock_.reset();
}

BEGIN_METADATA(NewButton)
END_METADATA

}  // namespace uw
