// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/little_window/little_window_toolbar.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/font_list.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/flex_layout_view.h"
#include "ui/views/view_class_properties.h"
#include "uw/browser/little_window/little_window.h"
#include "uw/browser/ui/glass/glass.h"
#include "uw/browser/workspaces/workspace_list.h"

namespace uw {

namespace {

constexpr int kHeight = 30;
constexpr int kRadius = 9;
constexpr int kArrowWidth = 26;
constexpr int kArrowIconSize = 13;
constexpr int kDividerHeight = 14;
// The menu's rows, above the numbers of Chromium's own commands.
constexpr int kFirstRow = 56400;

// The pill, which takes the field's fill.
class Pill : public views::FlexLayoutView {
  METADATA_HEADER(Pill, views::FlexLayoutView)

 public:
  Pill() {
    SetOrientation(views::LayoutOrientation::kHorizontal);
    SetCrossAxisAlignment(views::LayoutAlignment::kCenter);
    SetBackground(
        views::CreateRoundedRectBackground(kColorLocationBarBackground, kRadius));
    SetProperty(views::kMarginsKey, gfx::Insets::VH(0, 4));
  }
};

BEGIN_METADATA(Pill)
END_METADATA

}  // namespace

LittleWindowToolbar::LittleWindowToolbar(LittleWindow* little_window,
                                         BrowserView* browser_view)
    : little_window_(little_window), browser_view_(browser_view) {
  ToolbarView* toolbar = browser_view->toolbar();
  if (!toolbar) {
    return;
  }
  auto pill = std::make_unique<Pill>();
  auto* move = pill->AddChildView(std::make_unique<views::LabelButton>(
      base::BindRepeating(
          [](LittleWindow* little_window) { little_window->MoveTo({}); },
          base::Unretained(little_window))));
  move->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, 9, 0, 10)));
  move->SetMinSize(gfx::Size(0, kHeight));
  move->SetLabelStyle(views::style::STYLE_BODY_4_MEDIUM);
  views::InstallRoundRectHighlightPathGenerator(move, gfx::Insets(), kRadius);

  auto* divider = pill->AddChildView(std::make_unique<views::Separator>());
  divider->SetOrientation(views::Separator::Orientation::kVertical);
  divider->SetPreferredLength(kDividerHeight);

  auto* arrow = pill->AddChildView(std::make_unique<views::ImageButton>(
      base::BindRepeating(&LittleWindowToolbar::ShowWorkspaceMenu,
                          base::Unretained(this))));
  arrow->SetPreferredSize(gfx::Size(kArrowWidth, kHeight));
  arrow->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  arrow->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  arrow->SetImageModel(views::Button::STATE_NORMAL,
                       ui::ImageModel::FromVectorIcon(
                           vector_icons::kKeyboardArrowDownIcon,
                           ui::kColorIconSecondary, kArrowIconSize));
  const std::u16string choose =
      l10n_util::GetStringUTF16(IDS_UW_LITTLE_WINDOW_CHOOSE_WORKSPACE);
  arrow->SetTooltipText(choose);
  arrow->GetViewAccessibility().SetName(choose);
  views::InstallRoundRectHighlightPathGenerator(arrow, gfx::Insets(), kRadius);
  arrow_.SetView(arrow);

  pill_.SetView(toolbar->AddChildView(std::move(pill)));
  // The toolbar adds its buttons once the window is shown, after this.
  toolbar_.Observe(toolbar);
  Place();
  Update();
}

void LittleWindowToolbar::OnChildViewAdded(views::View* observed_view,
                                           views::View* child) {
  if (child->GetID() == VIEW_ID_APP_MENU) {
    Place();
  }
}

void LittleWindowToolbar::Place() {
  views::View* pill = pill_.view();
  ToolbarView* toolbar = browser_view_->toolbar();
  views::View* app_menu = toolbar ? toolbar->GetViewByID(VIEW_ID_APP_MENU) : nullptr;
  if (!pill || !app_menu || app_menu->parent() != pill->parent()) {
    return;
  }
  const std::optional<size_t> menu = toolbar->GetIndexOf(app_menu);
  const std::optional<size_t> at = toolbar->GetIndexOf(pill);
  if (menu && at && *at + 1 != *menu) {
    toolbar->ReorderChildView(pill, *at < *menu ? *menu - 1 : *menu);
  }
}

LittleWindowToolbar::~LittleWindowToolbar() {
  toolbar_.Reset();
  if (views::View* pill = pill_.view()) {
    pill->parent()->RemoveChildViewT(pill);
  }
}

void LittleWindowToolbar::Update() {
  views::View* pill = pill_.view();
  if (!pill) {
    return;
  }
  const WorkspaceList::Workspace* workspace =
      WorkspaceList::Get().Find(little_window_->workspace());
  auto* move = static_cast<views::LabelButton*>(pill->children().front());
  const std::u16string name =
      workspace ? base::UTF8ToUTF16(workspace->name) : std::u16string();
  move->SetText(base::UTF8ToUTF16(workspace ? workspace->icon + "  " : "") +
                l10n_util::GetStringFUTF16(IDS_UW_LITTLE_WINDOW_MOVE_TO, name));
  move->GetViewAccessibility().SetName(
      l10n_util::GetStringFUTF16(IDS_UW_LITTLE_WINDOW_MOVE_TO, name));
}

void LittleWindowToolbar::ShowWorkspaceMenu() {
  views::View* arrow = arrow_.view();
  if (!arrow || (menu_runner_ && menu_runner_->IsRunning())) {
    return;
  }
  rows_.clear();
  menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  menu_->AddTitle(l10n_util::GetStringUTF16(IDS_UW_LITTLE_WINDOW_MENU_TITLE));
  const std::string own = little_window_->workspace();
  const base::FilePath profile =
      little_window_->window()->GetProfile()->GetBaseName();
  std::vector<const WorkspaceList::Workspace*> same;
  std::vector<const WorkspaceList::Workspace*> elsewhere;
  for (const WorkspaceList::Workspace& workspace :
       WorkspaceList::Get().workspaces()) {
    if (workspace.id == own) {
      same.insert(same.begin(), &workspace);
    } else if (workspace.profile == profile) {
      same.push_back(&workspace);
    } else {
      elsewhere.push_back(&workspace);
    }
  }
  auto add = [&](const WorkspaceList::Workspace* workspace) {
    menu_->AddItem(kFirstRow + static_cast<int>(rows_.size()),
                   base::UTF8ToUTF16(workspace->icon + "  " + workspace->name));
    rows_.push_back(workspace->id);
  };
  for (const WorkspaceList::Workspace* workspace : same) {
    add(workspace);
  }
  if (!elsewhere.empty()) {
    menu_->AddSeparator(ui::NORMAL_SEPARATOR);
    menu_->AddTitle(
        l10n_util::GetStringUTF16(IDS_UW_REOPENS_IN_ANOTHER_PROFILE));
    for (const WorkspaceList::Workspace* workspace : elsewhere) {
      add(workspace);
    }
  }
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_.get(), views::MenuRunner::HAS_MNEMONICS);
  menu_runner_->RunMenuAt(arrow->GetWidget(), nullptr,
                          arrow->GetBoundsInScreen(),
                          views::MenuAnchorPosition::kTopRight,
                          ui::mojom::MenuSourceType::kNone);
  FrostOpenMenu(little_window_->window());
}

void LittleWindowToolbar::ExecuteCommand(int command_id, int event_flags) {
  const size_t row = command_id - kFirstRow;
  if (row < rows_.size()) {
    little_window_->MoveTo(rows_[row]);
  }
}

bool LittleWindowToolbar::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accelerator) const {
  // The first row is the little window's own workspace, on Cmd+O.
  if (command_id != kFirstRow) {
    return false;
  }
  *accelerator = ui::Accelerator(ui::VKEY_O, ui::EF_PLATFORM_ACCELERATOR);
  return true;
}

}  // namespace uw
