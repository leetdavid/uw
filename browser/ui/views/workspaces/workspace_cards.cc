// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/workspaces/workspace_cards.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/i18n/message_formatter.h"
#include "base/location.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/browser_process.h"  // nogncheck
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/strings/grit/components_strings.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/color/color_id.h"
#include "ui/gfx/font_list.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/ui/look/menu_card.h"
#include "uw/browser/ui/vector_icons/vector_icons.h"
#include "uw/browser/ui/views/tab_organization/menu_card_row.h"
#include "uw/browser/ui/views/theme_picker/theme_picker.h"
#include "uw/browser/ui/views/workspaces/card_bubble.h"
#include "uw/browser/ui/views/workspaces/icon_picker.h"
#include "uw/browser/ui/views/workspaces/workspace_name_row.h"
#include "uw/browser/ui/views/workspaces/workspace_switcher.h"
#include "uw/browser/ui/views/workspaces/workspace_text.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

constexpr int kCardWidth = 300;
constexpr int kDeleteCardWidth = 340;
constexpr int kFieldHeight = 34;
constexpr int kFieldRadius = 9;
constexpr int kRingWidth = 2;
constexpr int kGap = 8;
constexpr int kSectionGap = 14;
constexpr int kIconSize = 14;
constexpr gfx::Insets kFieldInsets = gfx::Insets::VH(0, 8);
constexpr int kNewProfileCommand = 1000;

// Runs `action` with the window that shows workspace `id`, switching to it
// first if this window shows another.
void InWorkspace(BrowserWindowInterface* window,
                 const std::string& id,
                 base::OnceCallback<void(BrowserWindowInterface*)> action) {
  WorkspaceWindow* shown = WorkspaceWindow::From(window);
  if (!shown) {
    return;
  }
  if (shown->workspace() == id) {
    std::move(action).Run(window);
    return;
  }
  shown->Switch(id, std::move(action));
}

std::unique_ptr<views::Label> SmallLabel(const std::u16string& text,
                                         ui::ColorId color) {
  auto label = std::make_unique<views::Label>(text);
  label->SetFontList(label->font_list().DeriveWithSizeDelta(-1));
  label->SetEnabledColor(color);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return label;
}

// A text field with the 2px ring of the primary color, as uw's name fields
// have.
std::unique_ptr<views::Textfield> RingedField(int accessible_name) {
  auto field = std::make_unique<views::Textfield>();
  field->SetPreferredSize(gfx::Size(0, kFieldHeight));
  field->SetTextColorId(kColorMenuCardText);
  field->SetBackgroundColor(kColorMenuCardBackground);
  field->SetBorder(views::CreatePaddedBorder(
      views::CreateRoundedRectBorder(kRingWidth, kFieldRadius,
                                     kColorMenuCardAccent),
      kFieldInsets));
  field->SetAccessibleName(l10n_util::GetStringUTF16(accessible_name));
  return field;
}

CardBubble* MakeCardBubble(views::View* anchor,
                           views::BubbleBorder::Arrow arrow,
                           base::OnceClosure closing) {
  auto* bubble = new CardBubble(anchor, arrow, std::move(closing));
  bubble->set_corner_radius(kMenuCardRadius);
  bubble->set_adjust_if_offscreen(true);
  return bubble;
}

// The profile row of the new workspace card: a person, the profile's name,
// its workspaces' emoji, and up and down chevrons.
class ProfileButton : public views::Button {
  METADATA_HEADER(ProfileButton, views::Button)

 public:
  explicit ProfileButton(PressedCallback callback)
      : Button(std::move(callback)) {
    auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
    layout->SetOrientation(views::LayoutOrientation::kHorizontal);
    layout->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);
    layout->SetInteriorMargin(kFieldInsets);
    layout->SetDefault(views::kMarginsKey, gfx::Insets::TLBR(0, 0, 0, kGap));
    SetPreferredSize(gfx::Size(0, kFieldHeight));
    SetBackground(
        views::CreateRoundedRectBackground(kColorMenuCardHover, kFieldRadius));
    AddChildView(
        std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
            kUserIcon, kColorMenuCardIcon, kIconSize)));
    name_ = AddChildView(std::make_unique<views::Label>());
    name_->SetEnabledColor(kColorMenuCardText);
    name_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    name_->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                                 views::MinimumFlexSizeRule::kScaleToZero,
                                 views::MaximumFlexSizeRule::kUnbounded));
    icons_ = AddChildView(std::make_unique<views::Label>());
    icons_->SetSubpixelRenderingEnabled(false);
    icons_->SetBackgroundColor(SK_ColorTRANSPARENT);
    AddChildView(
        std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
            kChevronsUpDownIcon, kColorMenuCardMuted, kIconSize)));
  }

  void Set(const std::u16string& name, const std::u16string& icons) {
    name_->SetText(name);
    icons_->SetText(icons);
    icons_->SetVisible(!icons.empty());
    GetViewAccessibility().SetName(name);
  }

  views::Label* name() { return name_; }
  views::Label* icons() { return icons_; }

 private:
  raw_ptr<views::Label> name_ = nullptr;
  raw_ptr<views::Label> icons_ = nullptr;
};

BEGIN_METADATA(ProfileButton)
END_METADATA

// The Change Icon card: the workspace's emoji and name, and the suggestions.
class IconCard : public views::View {
  METADATA_HEADER(IconCard, views::View)

 public:
  explicit IconCard(const WorkspaceList::Workspace& workspace)
      : id_(workspace.id) {
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), kGap));
    auto* top = AddChildView(std::make_unique<views::View>());
    auto* top_layout = top->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kGap));
    top_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    const IconPicked pick =
        base::BindRepeating(&IconCard::Pick, base::Unretained(this));
    field_ =
        top->AddChildView(std::make_unique<IconField>(workspace.icon, pick));
    auto* name = top->AddChildView(
        std::make_unique<views::Label>(base::UTF8ToUTF16(workspace.name)));
    name->SetEnabledColor(kColorMenuCardText);
    AddChildView(std::make_unique<IconGrid>(
        workspace.icon, pick,
        base::BindRepeating(&IconField::OpenEmojiPicker,
                            base::Unretained(field_.get()))));
  }

 private:
  void Pick(const std::string& icon) {
    WorkspaceList::Get().SetIcon(id_, icon);
    GetWidget()->Close();
  }

  const std::string id_;
  raw_ptr<IconField> field_ = nullptr;
};

BEGIN_METADATA(IconCard)
END_METADATA

}  // namespace

CardBubble* MakeWorkspaceMenu(WorkspaceSwitcher* switcher,
                              const std::string& id,
                              views::View* icon,
                              base::OnceClosure closing) {
  CardBubble* bubble = MakeCardBubble(icon, views::BubbleBorder::BOTTOM_CENTER,
                                      std::move(closing));
  SetUpMenuCard(bubble);
  bubble->set_highlight_button_when_shown(false);
  bubble->SetContentsView(std::make_unique<WorkspaceMenu>(switcher, id));
  return bubble;
}

CardBubble* MakeIconCard(WorkspaceSwitcher* switcher,
                         const std::string& id,
                         views::View* icon,
                         base::OnceClosure closing) {
  CardBubble* bubble = MakeCardBubble(icon, views::BubbleBorder::BOTTOM_CENTER,
                                      std::move(closing));
  bubble->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  bubble->set_highlight_button_when_shown(false);
  if (const WorkspaceList::Workspace* workspace =
          WorkspaceList::Get().Find(id)) {
    bubble->SetContentsView(std::make_unique<IconCard>(*workspace));
  }
  return bubble;
}

CardBubble* MakeNewWorkspaceCard(WorkspaceSwitcher* switcher,
                                 views::View* foot,
                                 base::OnceClosure closing) {
  CardBubble* bubble = MakeCardBubble(foot, views::BubbleBorder::BOTTOM_LEFT,
                                      std::move(closing));
  bubble->SetTitle(l10n_util::GetStringUTF16(IDS_UW_NEW_WORKSPACE));
  bubble->set_fixed_width(kCardWidth);
  NewWorkspaceCard* card = bubble->SetContentsView(
      std::make_unique<NewWorkspaceCard>(switcher, bubble));
  bubble->SetInitiallyFocusedView(card->name_field());
  bubble->SetAcceptCallbackWithClose(
      base::BindRepeating(&NewWorkspaceCard::Accept, base::Unretained(card)));
  bubble->SetCancelCallbackWithClose(
      base::BindRepeating(&NewWorkspaceCard::Cancel, base::Unretained(card)));
  return bubble;
}

CardBubble* MakeDeleteCard(WorkspaceSwitcher* switcher,
                           const std::string& id,
                           base::OnceClosure closing) {
  const WorkspaceList::Workspace* workspace = WorkspaceList::Get().Find(id);
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(switcher->window());
  CardBubble* bubble =
      MakeCardBubble(view ? view->contents_container() : switcher,
                     views::BubbleBorder::FLOAT, std::move(closing));
  if (!workspace) {
    return bubble;
  }
  bubble->set_fixed_width(kDeleteCardWidth);
  bubble->SetTitle(l10n_util::GetStringFUTF16(
      IDS_UW_DELETE_WORKSPACE_TITLE, base::UTF8ToUTF16(workspace->name)));
  const std::u16string profile = ProfileName(workspace->profile);
  const std::vector<std::u16string> others =
      WorkspaceNamesOn(workspace->profile, id);
  std::u16string text = base::i18n::MessageFormatter::FormatWithNamedArgs(
      l10n_util::GetStringUTF16(IDS_UW_DELETE_WORKSPACE_TABS), "COUNT",
      WorkspaceWindow::CountTabs(id));
  text += u" ";
  if (others.empty()) {
    text += l10n_util::GetStringFUTF16(IDS_UW_DELETE_LAST_WORKSPACE, profile);
  } else {
    text += base::i18n::MessageFormatter::FormatWithNamedArgs(
        l10n_util::GetStringUTF16(IDS_UW_DELETE_WORKSPACE_KEEPS), "COUNT",
        static_cast<int>(others.size()), "PROFILE", profile, "OTHERS",
        JoinNames(others));
  }
  auto body = std::make_unique<views::Label>(
      text, views::style::CONTEXT_DIALOG_BODY_TEXT,
      views::style::STYLE_PRIMARY);
  body->SetMultiLine(true);
  body->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  bubble->SetContentsView(std::move(body));

  const auto remove = [](const std::string& id, bool with_profile) {
    WorkspaceWindow::Delete(id, with_profile);
  };
  if (others.empty()) {
    // Both buttons delete the workspace. Keep Profile is the default, so
    // Return keeps the sign-ins.
    bubble->SetButtonLabel(ui::mojom::DialogButton::kOk,
                           l10n_util::GetStringUTF16(IDS_UW_KEEP_PROFILE));
    bubble->SetAcceptCallback(base::BindOnce(remove, id, false));
    auto delete_profile = std::make_unique<views::MdTextButton>(
        base::BindRepeating(
            [](CardBubble* bubble, const std::string& id) {
              bubble->GetWidget()->Close();
              WorkspaceWindow::Delete(id, true);
            },
            base::Unretained(bubble), id),
        l10n_util::GetStringUTF16(IDS_UW_DELETE_PROFILE_TOO));
    delete_profile->SetEnabledTextColors(ui::kColorSysError);
    bubble->SetExtraView(std::move(delete_profile));
  } else {
    bubble->SetButtonLabel(ui::mojom::DialogButton::kOk,
                           l10n_util::GetStringUTF16(IDS_UW_DELETE_WORKSPACE));
    bubble->SetAcceptCallback(base::BindOnce(remove, id, false));
    bubble->RegisterWidgetInitializedCallback(base::BindOnce(
        [](CardBubble* bubble) {
          views::MdTextButton* button = bubble->GetOkButton();
          button->SetBgColorIdOverride(ui::kColorSysError);
          button->SetEnabledTextColors(ui::kColorSysOnError);
        },
        base::Unretained(bubble)));
  }
  return bubble;
}

WorkspaceMenu::WorkspaceMenu(WorkspaceSwitcher* switcher, const std::string& id)
    : switcher_(switcher), id_(id) {
  LayOutMenuCard(this);
  const auto add = [&](Id row_id, const gfx::VectorIcon& icon, int text,
                       base::RepeatingCallback<void(WorkspaceSwitcher*)> action)
      -> MenuCardRow* {
    auto row = std::make_unique<MenuCardRow>(
        base::BindRepeating(&WorkspaceMenu::Then, base::Unretained(this),
                            action),
        icon, l10n_util::GetStringUTF16(text));
    row->SetID(row_id);
    return AddChildView(std::move(row));
  };
  add(kRenameId, vector_icons::kEditIcon, IDS_UW_RENAME_WORKSPACE,
      base::BindRepeating(
          [](const std::string& id, WorkspaceSwitcher* switcher) {
            InWorkspace(
                switcher->window(), id,
                base::BindOnce([](BrowserWindowInterface* window) {
                  if (WorkspaceNameRow* row = WorkspaceNameRow::From(window)) {
                    row->StartRename();
                  }
                }));
          },
          id));
  add(kIconId, kSmileIcon, IDS_UW_CHANGE_WORKSPACE_ICON,
      base::BindRepeating(
          [](const std::string& id, WorkspaceSwitcher* switcher) {
            switcher->ShowIconCard(id);
          },
          id));
  add(kThemeId, kPaletteIcon, IDS_UW_THEME_MENU,
      base::BindRepeating(
          [](const std::string& id, WorkspaceSwitcher* switcher) {
            InWorkspace(switcher->window(), id,
                        base::BindOnce([](BrowserWindowInterface* window) {
                          ThemePicker::Open(window);
                        }));
          },
          id));
  AddChildView(MakeMenuCardSeparator());
  MenuCardRow* remove =
      add(kDeleteId, kDeleteIcon, IDS_UW_DELETE_WORKSPACE_MENU,
          base::BindRepeating(
              [](const std::string& id, WorkspaceSwitcher* switcher) {
                switcher->ShowDeleteCard(id);
              },
              id));
  remove->label()->SetEnabledColor(ui::kColorSysError);
  // One workspace always stays.
  remove->SetEnabled(WorkspaceList::Get().workspaces().size() > 1);
}

WorkspaceMenu::~WorkspaceMenu() = default;

void WorkspaceMenu::Then(
    base::RepeatingCallback<void(WorkspaceSwitcher*)> action) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<WorkspaceSwitcher> switcher,
             base::RepeatingCallback<void(WorkspaceSwitcher*)> action) {
            if (switcher) {
              action.Run(switcher.get());
            }
          },
          switcher_->GetWeakPtr(), std::move(action)));
  GetWidget()->Close();
}

BEGIN_METADATA(WorkspaceMenu)
END_METADATA

NewWorkspaceCard::NewWorkspaceCard(WorkspaceSwitcher* switcher,
                                   CardBubble* bubble)
    : switcher_(switcher),
      bubble_(bubble),
      profile_(switcher->window()->GetProfile()->GetBaseName()) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));

  main_ = AddChildView(std::make_unique<views::View>());
  main_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  auto* top = main_->AddChildView(std::make_unique<views::View>());
  auto* top_layout = top->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kGap));
  const std::string first_icon(WorkspaceList::Get().UnusedIcon());
  const IconPicked pick = base::BindRepeating(
      [](NewWorkspaceCard* card, const std::string& icon) {
        card->icon_->SetIcon(icon);
        card->grid_->Select(icon);
      },
      base::Unretained(this));
  icon_ = top->AddChildView(std::make_unique<IconField>(first_icon, pick));
  name_ = top->AddChildView(RingedField(IDS_UW_WORKSPACE_NAME));
  top_layout->SetFlexForView(name_, 1);
  name_->SetController(this);
  grid_ = main_->AddChildView(std::make_unique<IconGrid>(
      first_icon, pick,
      base::BindRepeating(&IconField::OpenEmojiPicker,
                          base::Unretained(icon_.get()))));
  grid_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(kGap, 0, 0, 0));
  auto* profile = main_->AddChildView(SmallLabel(
      l10n_util::GetStringUTF16(IDS_UW_WORKSPACE_PROFILE), kColorMenuCardText));
  profile->SetProperty(views::kMarginsKey,
                       gfx::Insets::TLBR(kSectionGap, 0, 6, 0));
  profile_button_ =
      main_->AddChildView(std::make_unique<ProfileButton>(base::BindRepeating(
          &NewWorkspaceCard::ShowProfileMenu, base::Unretained(this))));
  hint_ =
      main_->AddChildView(SmallLabel(std::u16string(), kColorMenuCardMuted));
  hint_->SetMultiLine(true);
  hint_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(6, 0, 0, 0));

  profile_step_ = AddChildView(std::make_unique<views::View>());
  profile_step_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 6));
  profile_step_->AddChildView(SmallLabel(
      l10n_util::GetStringUTF16(IDS_UW_NEW_PROFILE_NAME), kColorMenuCardText));
  profile_name_ =
      profile_step_->AddChildView(RingedField(IDS_UW_NEW_PROFILE_NAME));
  auto* profile_hint = profile_step_->AddChildView(SmallLabel(
      l10n_util::GetStringUTF16(IDS_UW_NEW_PROFILE_HINT), kColorMenuCardMuted));
  profile_hint->SetMultiLine(true);
  profile_step_->SetVisible(false);

  bubble_->SetButtonLabel(ui::mojom::DialogButton::kOk,
                          l10n_util::GetStringUTF16(IDS_UW_CREATE));
  Update();
}

NewWorkspaceCard::~NewWorkspaceCard() = default;

bool NewWorkspaceCard::naming_profile() const {
  return profile_step_->GetVisible();
}

void NewWorkspaceCard::PickProfile(const base::FilePath& profile) {
  profile_ = profile;
  new_profile_.clear();
  Update();
}

void NewWorkspaceCard::StartNewProfile() {
  profile_name_->SetText(new_profile_);
  main_->SetVisible(false);
  profile_step_->SetVisible(true);
  bubble_->SetButtonLabel(ui::mojom::DialogButton::kOk,
                          l10n_util::GetStringUTF16(IDS_DONE));
  bubble_->SetButtonEnabled(ui::mojom::DialogButton::kOk, true);
  if (GetWidget()) {
    bubble_->SizeToContents();
    profile_name_->RequestFocus();
  }
}

bool NewWorkspaceCard::Accept() {
  if (naming_profile()) {
    EndNewProfile(/*keep=*/true);
    return false;
  }
  const std::u16string name(
      base::TrimWhitespace(name_->GetText(), base::TRIM_ALL));
  if (name.empty()) {
    return false;
  }
  WorkspaceWindow::Create(switcher_->window(), base::UTF16ToUTF8(name),
                          icon_->icon(), profile_, new_profile_,
                          base::BindOnce([](BrowserWindowInterface* window) {
                            ThemePicker::Open(window);
                          }));
  return true;
}

bool NewWorkspaceCard::Cancel() {
  if (naming_profile()) {
    EndNewProfile(/*keep=*/false);
    return false;
  }
  return true;
}

void NewWorkspaceCard::ContentsChanged(views::Textfield* sender,
                                       const std::u16string& new_contents) {
  Update();
}

bool NewWorkspaceCard::IsCommandIdChecked(int command_id) const {
  return command_id >= 0 &&
         static_cast<size_t>(command_id) < menu_profiles_.size() &&
         new_profile_.empty() && menu_profiles_[command_id] == profile_;
}

void NewWorkspaceCard::ExecuteCommand(int command_id, int event_flags) {
  if (command_id == kNewProfileCommand) {
    StartNewProfile();
  } else if (command_id >= 0 &&
             static_cast<size_t>(command_id) < menu_profiles_.size()) {
    PickProfile(menu_profiles_[command_id]);
  }
}

void NewWorkspaceCard::ShowProfileMenu() {
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  menu_profiles_.clear();
  for (const ProfileAttributesEntry* entry :
       g_browser_process->profile_manager()
           ->GetProfileAttributesStorage()
           .GetAllProfilesAttributesSortedByNameWithCheck()) {
    const base::FilePath profile = entry->GetPath().BaseName();
    const int command = static_cast<int>(menu_profiles_.size());
    menu_profiles_.push_back(profile);
    menu_model_->AddCheckItem(command, entry->GetName());
    menu_model_->SetMinorText(*menu_model_->GetIndexOfCommandId(command),
                              WorkspaceIconsOn(profile));
  }
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_model_->AddItemWithStringId(kNewProfileCommand, IDS_UW_NEW_PROFILE);
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(), views::MenuRunner::COMBOBOX);
  menu_runner_->RunMenuAt(
      GetWidget(), nullptr, profile_button_->GetBoundsInScreen(),
      views::MenuAnchorPosition::kTopLeft, ui::mojom::MenuSourceType::kNone);
}

void NewWorkspaceCard::EndNewProfile(bool keep) {
  const std::u16string typed(
      base::TrimWhitespace(profile_name_->GetText(), base::TRIM_ALL));
  if (keep && !typed.empty()) {
    profile_.clear();
    new_profile_ = typed;
  }
  profile_step_->SetVisible(false);
  main_->SetVisible(true);
  bubble_->SetButtonLabel(ui::mojom::DialogButton::kOk,
                          l10n_util::GetStringUTF16(IDS_UW_CREATE));
  Update();
  if (GetWidget()) {
    bubble_->SizeToContents();
    name_->RequestFocus();
  }
}

void NewWorkspaceCard::Update() {
  auto* button = static_cast<ProfileButton*>(profile_button_.get());
  if (!new_profile_.empty()) {
    button->Set(new_profile_, std::u16string());
    hint_->SetText(l10n_util::GetStringUTF16(IDS_UW_NEW_PROFILE_HINT));
  } else {
    button->Set(ProfileName(profile_), WorkspaceIconsOn(profile_));
    std::vector<std::u16string> names = WorkspaceNamesOn(profile_);
    if (names.empty()) {
      names.push_back(ProfileName(profile_));
    }
    hint_->SetText(
        l10n_util::GetStringFUTF16(IDS_UW_WORKSPACE_SHARES, JoinNames(names)));
  }
  bubble_->SetButtonEnabled(
      ui::mojom::DialogButton::kOk,
      naming_profile() ||
          !base::TrimWhitespace(name_->GetText(), base::TRIM_ALL).empty());
}

BEGIN_METADATA(NewWorkspaceCard)
END_METADATA

}  // namespace uw
