// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include <algorithm>
#include <string>
#include <vector>

#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/profiles/profile_picker.h"
#include "chrome/browser/ui/startup/startup_browser_creator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/profiles/avatar_toolbar_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/sessions/core/tab_restore_types.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_id.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/tab_organization/new_button.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"
#include "uw/browser/ui/views/theme_picker/theme_picker.h"
#include "uw/browser/ui/views/workspaces/icon_picker.h"
#include "uw/browser/ui/views/workspaces/move_to_workspace_menu.h"
#include "uw/browser/ui/views/workspaces/workspace_cards.h"
#include "uw/browser/ui/views/workspaces/workspace_name_row.h"
#include "uw/browser/ui/views/workspaces/workspace_switcher.h"
#include "uw/browser/ui/views/workspaces/workspace_text.h"
#include "uw/browser/workspaces/workspace_commands.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

constexpr char kForm[] = "data:text/html,<input id=field>";
constexpr char kNotes[] = "chrome://version/?notes";

class WorkspaceViewsBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  static WorkspaceList& list() { return WorkspaceList::Get(); }

  Profile* profile() { return browser()->GetProfile(); }

  static std::string AddWorkspace(const std::string& name, Profile* profile) {
    return list().Add(name, std::string(list().UnusedIcon()),
                      profile->GetBaseName());
  }

  static Profile& MakeProfile() {
    ProfileManager* manager = g_browser_process->profile_manager();
    return profiles::testing::CreateProfileSync(
        manager, manager->GenerateNextProfileDirectoryPath());
  }

  // Lets the list's change reach the views, which hear of it on a later task.
  static void Settle() { base::RunLoop().RunUntilIdle(); }

  static WorkspaceSwitcher* SwitcherOf(BrowserWindowInterface* window) {
    return WorkspaceSwitcher::From(window);
  }

  static void Click(views::Button* button) {
    views::test::ButtonTestApi(button).NotifyClick(
        ui::MouseEvent(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                       ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON, 0));
  }

  static views::DialogDelegate* Dialog(views::Widget* card) {
    return card->widget_delegate()->AsDialogDelegate();
  }

  template <typename T>
  static T* ContentsOf(views::Widget* card) {
    return views::AsViewClass<T>(card->widget_delegate()->GetContentsView());
  }

  // The button under `view` that a reader hears as `name`.
  static views::Button* ButtonNamed(views::View* view,
                                    const std::u16string& name) {
    if (auto* button = views::AsViewClass<views::Button>(view);
        button && button->GetViewAccessibility().GetCachedName() == name) {
      return button;
    }
    for (views::View* child : view->children()) {
      if (views::Button* found = ButtonNamed(child, name)) {
        return found;
      }
    }
    return nullptr;
  }

  static std::vector<BrowserWindowInterface*> Windows() {
    return GetAllBrowserWindowInterfaces();
  }

  // The window of `member`'s group that shows, once it shows `workspace`.
  static BrowserWindowInterface* WaitForShown(BrowserWindowInterface* member,
                                              const std::string& workspace) {
    BrowserWindowInterface* shown = nullptr;
    EXPECT_TRUE(base::test::RunUntil([&] {
      std::vector<WorkspaceWindow*> group =
          WorkspaceWindow::From(member)->Partners();
      group.push_back(WorkspaceWindow::From(member));
      for (WorkspaceWindow* window : group) {
        if (window->workspace() == workspace && !window->hidden() &&
            window->window()->GetWindow()->IsVisible()) {
          shown = window->window();
          return true;
        }
      }
      return false;
    }));
    return shown;
  }

  static const WorkspaceList::Workspace* Named(const std::string& name) {
    const auto& all = list().workspaces();
    const auto it =
        std::ranges::find(all, name, &WorkspaceList::Workspace::name);
    return it == all.end() ? nullptr : &*it;
  }

  static content::WebContents* PageOf(BrowserWindowInterface* window) {
    return window->GetActiveTabInterface()->GetContents();
  }

  static bool HasTab(BrowserWindowInterface* window, const GURL& url) {
    TabStripModel* tabs = window->GetTabStripModel();
    for (int i = 0; i < tabs->count(); ++i) {
      if (tabs->GetWebContentsAt(i)->GetVisibleURL() == url) {
        return true;
      }
    }
    return false;
  }
};

IN_PROC_BROWSER_TEST_F(WorkspaceViewsBrowserTest,
                       TheSwitcherShowsEveryWorkspaceAndSwitches) {
  AddWorkspace("Reading", profile());
  const std::string side = AddWorkspace("Side project", profile());
  Settle();
  WorkspaceSwitcher* switcher = SwitcherOf(browser());
  ASSERT_TRUE(switcher);
  ASSERT_EQ(switcher->children().size(), 3u);
  views::Button* icon = switcher->IconFor(side);
  ASSERT_TRUE(icon);
  EXPECT_EQ(icon->GetPreferredSize().width(), 28);
  EXPECT_TRUE(
      icon->GetRenderedTooltipText(gfx::Point()).starts_with(u"Side project"));
  SaveWindowScreenshot(browser(), "workspaces-foot");

  Click(icon);
  BrowserWindowInterface* shown = WaitForShown(browser(), side);
  ASSERT_TRUE(shown);
  ASSERT_TRUE(SwitcherOf(shown));
  EXPECT_EQ(SwitcherOf(shown)->children().size(), 3u);
}

IN_PROC_BROWSER_TEST_F(WorkspaceViewsBrowserTest,
                       FromFiveWorkspacesTheOthersAreDots) {
  for (const char* name : {"Reading", "Side project", "Work", "Travel"}) {
    AddWorkspace(name, profile());
  }
  Settle();
  WorkspaceSwitcher* switcher = SwitcherOf(browser());
  const std::string current = WorkspaceWindow::From(browser())->workspace();
  for (const WorkspaceList::Workspace& workspace : list().workspaces()) {
    views::Button* icon = switcher->IconFor(workspace.id);
    ASSERT_TRUE(icon);
    EXPECT_EQ(icon->GetPreferredSize().width(),
              workspace.id == current ? 28 : 12)
        << workspace.name;
    EXPECT_FALSE(icon->GetRenderedTooltipText(gfx::Point()).empty());
  }
  SaveWindowScreenshot(browser(), "workspaces-dots");
}

IN_PROC_BROWSER_TEST_F(WorkspaceViewsBrowserTest, TheFootInDarkMode) {
  ThemeServiceFactory::GetForProfile(profile())->SetBrowserColorScheme(
      ThemeService::BrowserColorScheme::kDark);
  AddWorkspace("Reading", profile());
  AddWorkspace("Side project", profile());
  Settle();
  SaveWindowScreenshot(browser(), "workspaces-foot-dark");
}

IN_PROC_BROWSER_TEST_F(WorkspaceViewsBrowserTest,
                       TheNameRowNamesTheWorkspaceAndRenamesIt) {
  const std::string id = WorkspaceWindow::From(browser())->workspace();
  WorkspaceNameRow* row = WorkspaceNameRow::From(browser());
  ASSERT_TRUE(row);
  EXPECT_TRUE(row->IsDrawn());
  EXPECT_EQ(base::UTF16ToUTF8(row->name()->GetText()), list().Find(id)->name);
  SaveWindowScreenshot(browser(), "workspace-name-row");

  row->StartRename();
  ASSERT_TRUE(row->renaming());
  row->field()->SetText(u"Errands");
  SaveWindowScreenshot(browser(), "workspace-rename");
  row->HandleKeyEvent(row->field(), ui::KeyEvent(ui::EventType::kKeyPressed,
                                                 ui::VKEY_ESCAPE, ui::EF_NONE));
  EXPECT_FALSE(row->renaming());
  EXPECT_NE(list().Find(id)->name, "Errands");

  row->StartRename();
  row->field()->SetText(u"  Home  ");
  row->HandleKeyEvent(row->field(), ui::KeyEvent(ui::EventType::kKeyPressed,
                                                 ui::VKEY_RETURN, ui::EF_NONE));
  EXPECT_FALSE(row->renaming());
  EXPECT_EQ(list().Find(id)->name, "Home");
  Settle();
  EXPECT_EQ(row->name()->GetText(), u"Home");
}

IN_PROC_BROWSER_TEST_F(WorkspaceViewsBrowserTest,
                       TheMenuHasItsRowsAndKeepsOneWorkspace) {
  const std::string id = WorkspaceWindow::From(browser())->workspace();
  WorkspaceSwitcher* switcher = SwitcherOf(browser());
  switcher->ShowMenu(id);
  ASSERT_TRUE(switcher->card());
  views::View* menu = ContentsOf<WorkspaceMenu>(switcher->card());
  ASSERT_TRUE(menu);
  for (int row : {WorkspaceMenu::kRenameId, WorkspaceMenu::kIconId,
                  WorkspaceMenu::kThemeId, WorkspaceMenu::kDeleteId}) {
    EXPECT_TRUE(menu->GetViewByID(row)) << row;
  }
  // The last workspace cannot go.
  EXPECT_FALSE(menu->GetViewByID(WorkspaceMenu::kDeleteId)->GetEnabled());
  switcher->card()->CloseNow();

  AddWorkspace("Reading", profile());
  Settle();
  switcher->ShowMenu(id);
  menu = ContentsOf<WorkspaceMenu>(switcher->card());
  EXPECT_TRUE(menu->GetViewByID(WorkspaceMenu::kDeleteId)->GetEnabled());
  SaveWidgetScreenshot(switcher->card(), "workspace-menu");
  switcher->card()->CloseNow();
}

IN_PROC_BROWSER_TEST_F(WorkspaceViewsBrowserTest, ChangeIconPicksAnEmoji) {
  const std::string id = WorkspaceWindow::From(browser())->workspace();
  WorkspaceSwitcher* switcher = SwitcherOf(browser());
  switcher->ShowIconCard(id);
  views::Widget* card = switcher->card();
  ASSERT_TRUE(card);
  SaveWidgetScreenshot(card, "workspace-icon-card");
  views::Button* game =
      ButtonNamed(card->widget_delegate()->GetContentsView(), u"🎮");
  ASSERT_TRUE(game);
  Click(game);
  EXPECT_EQ(list().Find(id)->icon, "🎮");
  ASSERT_TRUE(base::test::RunUntil([&] { return !switcher->card(); }));
}

IN_PROC_BROWSER_TEST_F(WorkspaceViewsBrowserTest,
                       NewWorkspaceIsInTheNewButtonsMenuButNotInPrivate) {
  EXPECT_TRUE(
      NewItemsMenu(browser()).GetIndexOfCommandId(kNewWorkspaceCommand));
  Browser* incognito = CreateIncognitoBrowser();
  EXPECT_FALSE(
      NewItemsMenu(incognito).GetIndexOfCommandId(kNewWorkspaceCommand));
  EXPECT_FALSE(SwitcherOf(incognito));
  EXPECT_FALSE(WorkspaceNameRow::From(incognito));
  EXPECT_FALSE(chrome::IsCommandEnabled(CreateBrowserForPopup(profile()),
                                        kNewWorkspaceCommand));
}

// The narrow sidebar keeps only the new button in its foot. New Workspace and
// the keys still work there.
IN_PROC_BROWSER_TEST_F(WorkspaceViewsBrowserTest,
                       NewWorkspaceAndTheKeysWorkInTheNarrowSidebar) {
  const std::string work = AddWorkspace("Work", profile());
  Settle();
  tabs::VerticalTabStripStateController* state =
      tabs::VerticalTabStripStateController::From(browser());
  state->RequestCollapse(true);
  ASSERT_TRUE(base::test::RunUntil([&] { return state->IsCollapsed(); }));

  chrome::ExecuteCommand(browser(), kNewWorkspaceCommand);
  views::Widget* card = SwitcherOf(browser())->card();
  ASSERT_TRUE(card);
  EXPECT_TRUE(card->IsVisible());
  const gfx::Rect window =
      BrowserView::GetBrowserViewForBrowser(browser())->GetBoundsInScreen();
  EXPECT_TRUE(window.Contains(card->GetWindowBoundsInScreen()))
      << card->GetWindowBoundsInScreen().ToString() << " in "
      << window.ToString();
  SaveWidgetScreenshot(card, "new-workspace-card-narrow");
  card->CloseNow();

  RunWindowCommand(browser(), kGoToWorkspace1 + 1);
  EXPECT_TRUE(WaitForShown(browser(), work));
}

IN_PROC_BROWSER_TEST_F(WorkspaceViewsBrowserTest,
                       TheNewWorkspaceCardCreatesSwitchesAndOpensTheThemes) {
  chrome::ExecuteCommand(browser(), kNewWorkspaceCommand);
  views::Widget* card = SwitcherOf(browser())->card();
  ASSERT_TRUE(card);
  NewWorkspaceCard* contents = ContentsOf<NewWorkspaceCard>(card);
  ASSERT_TRUE(contents);
  EXPECT_FALSE(
      Dialog(card)->IsDialogButtonEnabled(ui::mojom::DialogButton::kOk));
  EXPECT_NE(contents->icon_field()->icon(), list().workspaces().front().icon);
  const std::u16string hint(contents->hint()->GetText());
  EXPECT_NE(hint.find(base::UTF8ToUTF16(list().workspaces().front().name)),
            std::u16string::npos)
      << hint;

  contents->name_field()->SetText(u"Side project");
  contents->ContentsChanged(contents->name_field(), u"Side project");
  EXPECT_TRUE(
      Dialog(card)->IsDialogButtonEnabled(ui::mojom::DialogButton::kOk));
  SaveWidgetScreenshot(card, "new-workspace-card");
  Dialog(card)->AcceptDialog();

  ASSERT_TRUE(base::test::RunUntil([&] { return Named("Side project"); }));
  const WorkspaceList::Workspace* created = Named("Side project");
  EXPECT_EQ(created->profile, profile()->GetBaseName());
  BrowserWindowInterface* shown = WaitForShown(browser(), created->id);
  ASSERT_TRUE(shown);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return ThemePicker::From(shown)->IsShowing(); }));
}

IN_PROC_BROWSER_TEST_F(WorkspaceViewsBrowserTest,
                       ANewProfileGetsOnlyANameAndItsWorkspace) {
  chrome::ExecuteCommand(browser(), kNewWorkspaceCommand);
  views::Widget* card = SwitcherOf(browser())->card();
  NewWorkspaceCard* contents = ContentsOf<NewWorkspaceCard>(card);
  ASSERT_TRUE(contents);

  contents->StartNewProfile();
  EXPECT_TRUE(contents->naming_profile());
  contents->profile_name_field()->SetText(u"Freelance");
  SaveWidgetScreenshot(card, "new-profile-step");
  // Return ends the step, not the card.
  EXPECT_FALSE(contents->Accept());
  EXPECT_FALSE(contents->naming_profile());

  contents->name_field()->SetText(u"Clients");
  contents->ContentsChanged(contents->name_field(), u"Clients");
  Dialog(card)->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return Named("Clients"); }));
  const WorkspaceList::Workspace* created = Named("Clients");
  EXPECT_NE(created->profile, profile()->GetBaseName());
  EXPECT_EQ(ProfileName(created->profile), u"Freelance");
  BrowserWindowInterface* shown = WaitForShown(browser(), created->id);
  ASSERT_TRUE(shown);
  EXPECT_EQ(shown->GetProfile()->GetBaseName(), created->profile);
}

IN_PROC_BROWSER_TEST_F(WorkspaceViewsBrowserTest,
                       PickingAProfileSaysWhoItIsSharedWith) {
  Profile& other = MakeProfile();
  AddWorkspace("Work", &other);
  AddWorkspace("Clients", &other);
  chrome::ExecuteCommand(browser(), kNewWorkspaceCommand);
  NewWorkspaceCard* contents =
      ContentsOf<NewWorkspaceCard>(SwitcherOf(browser())->card());
  contents->PickProfile(other.GetBaseName());
  const std::u16string hint(contents->hint()->GetText());
  EXPECT_NE(hint.find(u"Work"), std::u16string::npos) << hint;
  EXPECT_NE(hint.find(u"Clients"), std::u16string::npos) << hint;
  SwitcherOf(browser())->card()->CloseNow();
}

IN_PROC_BROWSER_TEST_F(WorkspaceViewsBrowserTest,
                       DeletingAWorkspaceClosesItsWindowsForGood) {
  const std::string personal = WorkspaceWindow::From(browser())->workspace();
  const std::string reading = AddWorkspace("Reading", profile());
  WorkspaceWindow::From(browser())->Switch(reading);
  BrowserWindowInterface* partner = WaitForShown(browser(), reading);
  ASSERT_TRUE(partner);
  ASSERT_TRUE(content::NavigateToURL(PageOf(partner), GURL(kNotes)));
  WorkspaceWindow::From(partner)->Switch(personal);
  ASSERT_EQ(WaitForShown(browser(), personal), browser());
  Settle();

  WorkspaceSwitcher* switcher = SwitcherOf(browser());
  switcher->ShowDeleteCard(reading);
  views::Widget* card = switcher->card();
  ASSERT_TRUE(card);
  EXPECT_FALSE(Dialog(card)->GetExtraView());
  // Deleting is drawn red, as the design says for a button that destroys.
  EXPECT_EQ(Dialog(card)->GetOkButton()->GetBgColorIdOverride(),
            ui::kColorSysError);
  SaveWidgetScreenshot(card, "delete-workspace");
  Dialog(card)->AcceptDialog();

  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !list().Find(reading) && Windows().size() == 1u; }));
  EXPECT_EQ(Windows()[0], browser());
  const sessions::TabRestoreService* restore =
      TabRestoreServiceFactory::GetForProfile(profile());
  for (const auto& entry : restore->entries()) {
    EXPECT_NE(entry->type, sessions::tab_restore::Type::WINDOW);
  }
}

IN_PROC_BROWSER_TEST_F(WorkspaceViewsBrowserTest,
                       DeletingTheShownWorkspaceSwitchesAwayFirst) {
  const std::string personal = WorkspaceWindow::From(browser())->workspace();
  const std::string reading = AddWorkspace("Reading", profile());
  WorkspaceWindow::From(browser())->Switch(reading);
  BrowserWindowInterface* partner = WaitForShown(browser(), reading);
  ASSERT_TRUE(partner);

  WorkspaceWindow::Delete(reading, /*with_profile=*/false);
  ASSERT_TRUE(base::test::RunUntil([&] { return Windows().size() == 1u; }));
  EXPECT_EQ(Windows()[0], browser());
  EXPECT_TRUE(browser()->GetWindow()->IsVisible());
  EXPECT_FALSE(WorkspaceWindow::From(browser())->hidden());
}

IN_PROC_BROWSER_TEST_F(WorkspaceViewsBrowserTest,
                       AProfilesLastWorkspaceAsksAboutTheProfile) {
  Profile& other = MakeProfile();
  const base::FilePath other_profile = other.GetBaseName();
  const std::string work = AddWorkspace("Work", &other);
  Settle();
  WorkspaceSwitcher* switcher = SwitcherOf(browser());
  switcher->ShowDeleteCard(work);
  views::Widget* card = switcher->card();
  ASSERT_TRUE(card);
  ASSERT_TRUE(Dialog(card)->GetExtraView());
  SaveWidgetScreenshot(card, "delete-last-workspace");
  // Keep Profile is the default.
  Dialog(card)->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return !list().Find(work); }));
  ProfileManager* manager = g_browser_process->profile_manager();
  EXPECT_TRUE(
      manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(
          manager->user_data_dir().Append(other_profile)));
}

IN_PROC_BROWSER_TEST_F(WorkspaceViewsBrowserTest,
                       DeleteProfileTooDeletesTheProfile) {
  Profile& other = MakeProfile();
  const base::FilePath other_path = other.GetPath();
  const std::string work = AddWorkspace("Work", &other);
  Settle();
  WorkspaceSwitcher* switcher = SwitcherOf(browser());
  switcher->ShowDeleteCard(work);
  views::Widget* card = switcher->card();
  auto* delete_profile =
      views::AsViewClass<views::Button>(Dialog(card)->GetExtraView());
  ASSERT_TRUE(delete_profile);
  Click(delete_profile);
  ASSERT_TRUE(base::test::RunUntil([&] { return !list().Find(work); }));
  ProfileManager* manager = g_browser_process->profile_manager();
  EXPECT_TRUE(base::test::RunUntil([&] {
    return !manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(
        other_path);
  }));
}

IN_PROC_BROWSER_TEST_F(WorkspaceViewsBrowserTest,
                       MoveToWorkspaceKeepsThePageWithinAProfile) {
  const std::string reading = AddWorkspace("Reading", profile());
  chrome::AddTabAt(browser(), GURL(kForm), -1, true);
  tabs::TabInterface* tab = browser()->GetActiveTabInterface();
  content::WebContents* page = tab->GetContents();
  ASSERT_TRUE(content::WaitForLoadStop(page));
  ASSERT_TRUE(content::ExecJs(
      page, "document.getElementById('field').value = 'half typed'"));

  ui::SimpleMenuModel menu(nullptr);
  AddMoveToWorkspace(&menu, tab);
  const std::optional<size_t> row =
      menu.GetIndexOfCommandId(kMoveToWorkspaceCommand);
  ASSERT_TRUE(row);
  ui::MenuModel* submenu = menu.GetSubmenuModelAt(*row);
  ASSERT_EQ(submenu->GetItemCount(), 1u);
  EXPECT_NE(submenu->GetLabelAt(0).find(u"Reading"), std::u16string::npos);
  submenu->ActivatedAt(0);

  BrowserWindowInterface* moved_to = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (WorkspaceWindow* partner :
         WorkspaceWindow::From(browser())->Partners()) {
      if (partner->workspace() == reading &&
          partner->window()->GetTabStripModel()->GetIndexOfWebContents(page) !=
              TabStripModel::kNoTab) {
        moved_to = partner->window();
        return true;
      }
    }
    return false;
  }));
  EXPECT_EQ(browser()->tab_strip_model()->GetIndexOfWebContents(page),
            TabStripModel::kNoTab);
  EXPECT_TRUE(WorkspaceWindow::From(moved_to)->hidden());
  EXPECT_EQ(content::EvalJs(page, "document.getElementById('field').value"),
            "half typed");
}

IN_PROC_BROWSER_TEST_F(WorkspaceViewsBrowserTest,
                       MoveToWorkspaceReopensTheAddressInAnotherProfile) {
  Profile& other = MakeProfile();
  const std::string work = AddWorkspace("Work", &other);
  chrome::AddTabAt(browser(), GURL(kNotes), -1, true);
  tabs::TabInterface* tab = browser()->GetActiveTabInterface();
  ASSERT_TRUE(content::WaitForLoadStop(tab->GetContents()));

  ui::SimpleMenuModel menu(nullptr);
  AddMoveToWorkspace(&menu, tab);
  const std::optional<size_t> row =
      menu.GetIndexOfCommandId(kMoveToWorkspaceCommand);
  ASSERT_TRUE(row);
  ui::MenuModel* submenu = menu.GetSubmenuModelAt(*row);
  ASSERT_EQ(submenu->GetItemCount(), 2u);
  EXPECT_EQ(submenu->GetTypeAt(0), ui::MenuModel::TYPE_TITLE);
  submenu->ActivatedAt(1);

  ASSERT_TRUE(base::test::RunUntil([&] {
    for (WorkspaceWindow* partner :
         WorkspaceWindow::From(browser())->Partners()) {
      if (partner->workspace() == work &&
          HasTab(partner->window(), GURL(kNotes))) {
        return !HasTab(browser(), GURL(kNotes));
      }
    }
    return false;
  }));
}

IN_PROC_BROWSER_TEST_F(WorkspaceViewsBrowserTest,
                       NoProfileButtonAndNoProfilePickerAtLaunch) {
  MakeProfile();
  ToolbarView* toolbar =
      BrowserView::GetBrowserViewForBrowser(browser())->toolbar();
  AvatarToolbarButton* avatar = toolbar->avatar_toolbar_button();
  EXPECT_FALSE(avatar && avatar->GetVisible());
  EXPECT_EQ(ProfilePicker::GetStartupMode(),
            StartupProfileMode::kBrowserWindow);
}

}  // namespace
}  // namespace uw
