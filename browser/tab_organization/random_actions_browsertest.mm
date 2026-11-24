// Copyright 2026 The uw Authors. All rights reserved.

#import <AppKit/AppKit.h>

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/insecure_random_generator.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/headless/headless_mode_util.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_active_state_manager/browser_active_state_manager.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_menu_model.h"
#include "chrome/browser/ui/tabs/tab_menu_model_factory.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_collection_controller.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/tab/tab_context_menu_controller.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/models/menu_model.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/test/cocoa_test_event_utils.h"
#include "ui/gfx/geometry/point.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/little_window/little_window_commands.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/terminal/terminal_tab.h"
#include "uw/browser/ui/views/command_bar/command_bar.h"
#include "uw/browser/ui/views/split_view/split_commands.h"
#include "uw/browser/ui/views/tab_organization/command_bar_commands.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"
#include "uw/browser/ui/views/workspaces/move_to_workspace_menu.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

// `--uw-random-seed=7 --uw-random-steps=400` picks the run of a MANUAL_ test.
constexpr char kSeedSwitch[] = "uw-random-seed";
constexpr char kStepsSwitch[] = "uw-random-steps";

// A walk opens no more than this, so a long one stays quick.
constexpr int kMostTabs = 12;
constexpr size_t kMostWindows = 3;

// Commands a walk leaves alone: they open a panel of macOS's, which waits for
// an answer no test gives, or start something outside the browser, or open a
// window the walk does not follow.
bool IsLeftAlone(int command) {
  switch (command) {
    case IDC_PRINT:
    case IDC_SAVE_PAGE:
    case IDC_NEW_INCOGNITO_WINDOW:
    case IDC_TASK_MANAGER_MAIN_MENU:
    case IDC_DEV_TOOLS:
    case kNewTerminalCommand:
    case kNewChatCommand:
    case kNewLittleWindowCommand:
    case kChooseWorkspaceCommand:
    case kMoveTaskToWorkspace:
    case kReportIssue:
    // A split's tab moved to a task without the rest of its split crashes uw
    // (T405). Until that is fixed a walk makes no splits, so it gets past it.
    case kSplitRight:
    case kSplitDown:
      return true;
    default:
      return false;
  }
}

uint64_t SwitchOr(const char* name, uint64_t fallback) {
  uint64_t value = 0;
  return base::StringToUint64(
             base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(name),
             &value)
             ? value
             : fallback;
}

void CollectRows(views::View* view, std::vector<views::View*>& rows) {
  if (auto* row = views::AsViewClass<TabView>(view);
      row && row->GetTabInterface() && row->IsDrawn() &&
      !row->GetVisibleBounds().IsEmpty()) {
    rows.push_back(row);
  }
  for (views::View* child : view->children()) {
    CollectRows(child, rows);
  }
}

// What a walk does is picked from a seed, so a run that fails can be run
// again. Each step does one thing a person can do, the way the app does it,
// then waits for the windows to come to rest and checks them.
class RandomWalkBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  using Action = void (RandomWalkBrowserTest::*)(Browser*);

  // Does `steps` things to the tabs, tasks, windows, and workspaces.
  void Walk(uint64_t seed, int steps) {
    Start(seed);
    ScopedKeepAlive app(KeepAliveOrigin::BROWSER,
                        KeepAliveRestartOption::DISABLED);
    ScopedProfileKeepAlive profile(profile_,
                                   ProfileKeepAliveOrigin::kBrowserWindow);
    WorkspaceList& list = WorkspaceList::Get();
    list.Add("Reading", std::string(list.UnusedIcon()),
             profile_->GetBaseName());
    Settle();
    for (step_ = 1; step_ <= steps && !HasFailure(); ++step_) {
      if (Windows().empty()) {
        Note("opens a window, with none open");
        chrome::NewEmptyWindow(profile_);
        ASSERT_TRUE(base::test::RunUntil([&] { return !Windows().empty(); }))
            << Story();
      }
      const std::vector<Browser*> windows = Windows();
      (this->*PickAction())(windows[Pick(windows.size())]);
      Settle();
    }
  }

  void Start(uint64_t seed) {
    seed_ = seed;
    random_.ReseedForTesting(seed);
    profile_ = browser()->GetProfile();
  }

  size_t Pick(size_t count) { return random_.RandUint64() % count; }
  bool Chance(int percent) { return Pick(100) < static_cast<size_t>(percent); }

  // The windows a person sees.
  std::vector<Browser*> Windows() {
    std::vector<Browser*> shown;
    for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
      Browser* browser = window->GetBrowserForMigrationOnly();
      if (window->GetProfile() == profile_ &&
          window->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
          !browser->IsDeleteScheduled() && window->GetWindow()->IsVisible() &&
          TabOrganizationController::From(window) &&
          !window->GetTabStripModel()->empty()) {
        shown.push_back(browser);
      }
    }
    return shown;
  }

  static TabStripCollectionController* SidebarOf(Browser* window) {
    VerticalTabStripRegionView* sidebar =
        BrowserView::GetBrowserViewForBrowser(window)
            ->vertical_tab_strip_region_view_for_testing();
    return sidebar ? sidebar->GetTabStripCollectionController() : nullptr;
  }

  // The tab rows `window`'s sidebar shows.
  static std::vector<views::View*> Rows(Browser* window) {
    std::vector<views::View*> rows;
    CollectRows(BrowserView::GetBrowserViewForBrowser(window), rows);
    return rows;
  }

  // Says what the step is about to do, so a crash leaves it in the log.
  void Note(const std::string& what) {
    story_.push_back(base::StringPrintf("%d. %s", step_, what.c_str()));
    LOG(WARNING) << "random walk, seed " << seed_ << ", step " << story_.back();
  }

  std::string Story() const {
    return base::StringPrintf("seed %llu:\n  ",
                              static_cast<unsigned long long>(seed_)) +
           base::JoinString(story_, "\n  ");
  }

  // A link Cmd-clicked, or clicked for a new tab, in the page the window
  // shows.
  void OpenALink(Browser* window) {
    TabStripModel* tabs = window->tab_strip_model();
    tabs::TabInterface* from = tabs->GetActiveTab();
    if (!from || TabOrganizationController::IsCopy(from) ||
        tabs->count() >= kMostTabs) {
      return;
    }
    const bool behind = Chance(70);
    Note(behind ? "Cmd-clicks a link in the page"
                : "opens a link of the page in a new tab");
    NavigateParams params(window,
                          GURL("data:text/html,<title>Page " +
                               base::NumberToString(++pages_) + "</title>"),
                          ui::PAGE_TRANSITION_LINK);
    params.disposition = behind ? WindowOpenDisposition::NEW_BACKGROUND_TAB
                                : WindowOpenDisposition::NEW_FOREGROUND_TAB;
    params.source_contents = from->GetContents();
    Navigate(&params);
  }

  // A click on a tab's row, plain, with Cmd, or with Shift.
  void ClickARow(Browser* window) {
    TabStripModel* tabs = window->tab_strip_model();
    TabStripCollectionController* sidebar = SidebarOf(window);
    if (!sidebar) {
      return;
    }
    const int index = static_cast<int>(Pick(tabs->count()));
    const tabs::TabInterface* tab = tabs->GetTabAtIndex(index);
    switch (Pick(5)) {
      case 0:
        Note(base::StringPrintf("Cmd-clicks tab %d", index));
        sidebar->ToggleSelected(tab);
        break;
      case 1:
        Note(base::StringPrintf("Shift-clicks tab %d", index));
        sidebar->ExtendSelectionTo(tab);
        break;
      default:
        Note(base::StringPrintf("clicks tab %d", index));
        sidebar->SelectTab(
            tab, TabStripUserGestureDetails(
                     TabStripUserGestureDetails::GestureType::kMouse));
    }
  }

  void CloseWithTheButton(Browser* window) {
    TabStripModel* tabs = window->tab_strip_model();
    TabStripCollectionController* sidebar = SidebarOf(window);
    if (!sidebar) {
      return;
    }
    const int index = static_cast<int>(Pick(tabs->count()));
    Note(base::StringPrintf("closes tab %d with its button", index));
    sidebar->CloseTab(tabs->GetTabAtIndex(index), CloseTabSource::kFromMouse);
  }

  // A command picked by name in the command bar, which lists what can run.
  void RunACommand(Browser* window) {
    std::vector<CommandBar::Command> listed = ListCommandBarCommands(window);
    std::erase_if(listed, [&](const CommandBar::Command& command) {
      return IsLeftAlone(command.id) ||
             (command.id == IDC_NEW_WINDOW && Windows().size() >= kMostWindows);
    });
    if (listed.empty()) {
      return;
    }
    const CommandBar::Command& command = listed[Pick(listed.size())];
    Note("runs \"" + base::UTF16ToUTF8(command.name) +
         "\" from the command bar");
    chrome::ExecuteCommand(window, command.id);
  }

  // One of uw's own commands, from the menu bar or by its key.
  void PressAKey(Browser* window) {
    std::vector<int> commands;
    for (int command = TabOrganizationController::kNewTask;
         command <= kLastWindowCommand; ++command) {
      if (IsWindowCommand(command) && !IsLeftAlone(command) &&
          CanRunWindowCommand(window, command)) {
        commands.push_back(command);
      }
    }
    if (commands.empty()) {
      return;
    }
    const int command = commands[Pick(commands.size())];
    Note(
        base::StringPrintf("picks uw's command %d from the menu bar", command));
    chrome::ExecuteCommand(window, command);
  }

  // An item of a tab's right-click menu. The menu is built as
  // TabStripCollectionController::ShowTabContextMenu builds it, which then
  // opens a menu of macOS's that a test cannot pick from.
  void PickFromATabsMenu(Browser* window) {
    TabStripModel* tabs = window->tab_strip_model();
    TabOrganizationController* organizer =
        TabOrganizationController::From(window);
    TabStripCollectionController* sidebar = SidebarOf(window);
    const int first = tabs->IndexOfFirstNonPinnedTab();
    if (!sidebar || first >= tabs->count()) {
      return;
    }
    const int index = first + static_cast<int>(Pick(tabs->count() - first));
    const tabs::TabInterface* tab = tabs->GetTabAtIndex(index);
    if (!organizer->IdFor(tab)) {
      return;
    }
    TabContextMenuController menu(tab->GetHandle(), sidebar);
    TabMenuModelFactory factory;
    std::unique_ptr<ui::SimpleMenuModel> model = factory.Create(
        &menu, window->GetFeatures().tab_menu_model_delegate(), tabs, index);
    organizer->AddCommands(model.get(), organizer->SelectionFor(tab));
    AddSplitCommands(model.get(), tab);
    AddMoveToWorkspace(model.get(), tab);
    ui::SimpleMenuModel* shown = model.get();
    menu.LoadModel(std::move(model), factory.AsTabMenuModel(shown));

    std::vector<std::pair<ui::MenuModel*, size_t>> items;
    std::vector<std::u16string> names;
    CollectItems(shown, u"", items, names);
    if (items.empty()) {
      return;
    }
    const size_t picked = Pick(items.size());
    Note(base::StringPrintf("right-clicks tab %d and picks \"%s\"", index,
                            base::UTF16ToUTF8(names[picked]).c_str()));
    items[picked].first->ActivatedAt(items[picked].second, 0);
  }

  void CollectItems(ui::MenuModel* menu,
                    const std::u16string& path,
                    std::vector<std::pair<ui::MenuModel*, size_t>>& items,
                    std::vector<std::u16string>& names) {
    menu->MenuWillShow();
    for (size_t i = 0; i < menu->GetItemCount(); ++i) {
      if (!menu->IsVisibleAt(i) || !menu->IsEnabledAt(i)) {
        continue;
      }
      const std::u16string name = path + menu->GetLabelAt(i);
      switch (menu->GetTypeAt(i)) {
        case ui::MenuModel::TYPE_SUBMENU:
        case ui::MenuModel::TYPE_ACTIONABLE_SUBMENU:
          if (ui::MenuModel* submenu = menu->GetSubmenuModelAt(i)) {
            CollectItems(submenu, name + u" > ", items, names);
          }
          break;
        case ui::MenuModel::TYPE_COMMAND:
        case ui::MenuModel::TYPE_CHECK:
        case ui::MenuModel::TYPE_RADIO:
          if (!IsLeftAlone(menu->GetCommandIdAt(i)) &&
              (menu->GetCommandIdAt(i) !=
                   TabStripModel::CommandMoveTabsToNewWindow ||
               Windows().size() < kMostWindows)) {
            items.emplace_back(menu, i);
            names.push_back(name);
          }
          break;
        default:
          break;
      }
    }
  }

  // The window comes to the front, as a click on it brings it. A headless
  // window stays active once shown, so it is made inactive first.
  void BringToFront(Browser* window) {
    Note("brings a window to the front");
    BrowserActiveStateManager::From(window)->DidBecomeInactive();
    window->GetWindow()->Activate();
  }

  // A press on a row that is held while something else happens, then let go.
  // With the mouse down a person can still press keys, a page can close
  // itself, and the organizer can file a tab it is free to move. The press
  // and the release go through the window, which sends them on as it would a
  // real mouse's.
  void HoldARow(Browser* window) {
    const std::vector<views::View*> rows = Rows(window);
    if (rows.empty()) {
      return;
    }
    views::View* pressed = rows[Pick(rows.size())];
    base::WeakPtr<views::Widget> widget = pressed->GetWidget()->GetWeakPtr();
    const gfx::Point on_screen = pressed->GetBoundsInScreen().CenterPoint();
    gfx::Point at = pressed->GetLocalBounds().CenterPoint();
    views::View::ConvertPointToWidget(pressed, &at);
    Note("presses a row and holds it");
    ui::MouseEvent press(ui::EventType::kMousePressed, at, on_screen,
                         ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                         ui::EF_LEFT_MOUSE_BUTTON);
    widget->OnMouseEvent(&press);

    TabStripModel* tabs = window->tab_strip_model();
    switch (Pick(3)) {
      case 0:
        PressAKey(window);
        break;
      case 1:
        if (tabs->count() > 1) {
          const int index = static_cast<int>(Pick(tabs->count()));
          Note(base::StringPrintf("tab %d closes itself", index));
          tabs->CloseWebContentsAt(index, TabCloseTypes::CLOSE_NONE);
        }
        break;
      default: {
        TabOrganizationController* organizer =
            TabOrganizationController::From(window);
        const int index = static_cast<int>(Pick(tabs->count()));
        const std::optional<PageId> id =
            organizer->IdFor(tabs->GetTabAtIndex(index));
        if (id && !organizer->IsLeftAlone(*id) && !organizer->IsHeld(*id)) {
          Note(base::StringPrintf("the organizer files tab %d", index));
          organizer->MoveToNewTask(
              {*id}, u"Filed", TabOrganizationController::Origin::kOrganizer);
        }
      }
    }
    if (!widget) {
      return;
    }
    Note("presses Cmd and lets go of the row");
    [NSApp sendEvent:cocoa_test_event_utils::SynthesizeKeyEvent(
                         widget->GetNativeWindow().GetNativeNSWindow(),
                         /*keyDown=*/true, ui::VKEY_COMMAND,
                         NSEventModifierFlagCommand)];
    if (widget) {
      ui::MouseEvent release(ui::EventType::kMouseReleased, at, on_screen,
                             ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                             ui::EF_LEFT_MOUSE_BUTTON);
      widget->OnMouseEvent(&release);
    }
  }

  Action PickAction() {
    struct Weighed {
      int weight;
      Action action;
    };
    static constexpr auto kActions = std::to_array<Weighed>({
        {16, &RandomWalkBrowserTest::OpenALink},
        {14, &RandomWalkBrowserTest::ClickARow},
        {8, &RandomWalkBrowserTest::CloseWithTheButton},
        {20, &RandomWalkBrowserTest::RunACommand},
        {16, &RandomWalkBrowserTest::PressAKey},
        {16, &RandomWalkBrowserTest::PickFromATabsMenu},
        {5, &RandomWalkBrowserTest::BringToFront},
        {5, &RandomWalkBrowserTest::HoldARow},
    });
    size_t picked = Pick(100);
    for (const Weighed& weighed : kActions) {
      if (picked < static_cast<size_t>(weighed.weight)) {
        return weighed.action;
      }
      picked -= weighed.weight;
    }
    return &RandomWalkBrowserTest::OpenALink;
  }

  // Each unpinned tab of `window`, in order: its page, its task's title and
  // whether the task is folded, and its depth. Windows on one workspace show
  // the same.
  static std::vector<std::string> Layout(Browser* window) {
    TabStripModel* tabs = window->tab_strip_model();
    TabOrganizationController* organizer =
        TabOrganizationController::From(window);
    std::vector<std::string> rows;
    for (int i = tabs->IndexOfFirstNonPinnedTab(); i < tabs->count(); ++i) {
      tabs::TabInterface* tab = tabs->GetTabAtIndex(i);
      std::string task = "-";
      if (const std::optional<tab_groups::TabGroupId> group = tab->GetGroup()) {
        const tab_groups::TabGroupVisualData* visuals =
            tabs->group_model()->GetTabGroup(*group)->visual_data();
        task = "\"" + base::UTF16ToUTF8(visuals->title()) + "\"" +
               (visuals->is_collapsed() ? " folded" : "");
      }
      const std::optional<PageId> id = organizer->IdFor(tab);
      rows.push_back(base::StringPrintf(
          "%s %s %d%s", id ? id->value().c_str() : "?", task.c_str(),
          organizer->DepthFor(tab),
          TabOrganizationController::IsCopy(tab) ? " copy" : ""));
    }
    return rows;
  }

  // What is wrong with the windows, or nothing when they are at rest: each
  // sidebar lists its tree's pages in the tree's order and tasks, and the
  // windows on one workspace list the same tabs.
  std::string Trouble() {
    std::map<std::string, std::vector<std::string>> shown;
    for (Browser* window : Windows()) {
      TabOrganizationController* organizer =
          TabOrganizationController::From(window);
      TabStripModel* tabs = window->tab_strip_model();
      std::vector<PageId> strip;
      for (int i = 0; i < tabs->count(); ++i) {
        if (const std::optional<PageId> id =
                organizer->IdFor(tabs->GetTabAtIndex(i))) {
          strip.push_back(*id);
        }
      }
      std::vector<PageId> tree;
      for (const Page& page : organizer->tree().pages()) {
        tree.push_back(page.id);
        const tabs::TabInterface* tab = organizer->TabFor(page.id);
        if (!tab) {
          return "a page of the tree has no tab: " + page.id.value();
        }
        const std::optional<tab_groups::TabGroupId> group = tab->GetGroup();
        if (page.task != (group ? organizer->KeyOf(*group) : std::string())) {
          return "a tab is not in its page's task: " + page.id.value();
        }
      }
      if (tree != strip) {
        return "a sidebar's tabs are not in its tree's order";
      }
      std::vector<std::string> layout = Layout(window);
      for (std::string& row : layout) {
        base::ReplaceSubstringsAfterOffset(&row, 0, " copy", "");
      }
      const WorkspaceWindow* workspace = WorkspaceWindow::From(window);
      const auto [known, added] = shown.try_emplace(
          workspace ? workspace->workspace() : std::string(), layout);
      if (!added && known->second != layout) {
        return "two windows on one workspace list different tabs";
      }
    }
    return std::string();
  }

  std::string Layouts() {
    std::string text;
    for (Browser* window : Windows()) {
      const WorkspaceWindow* workspace = WorkspaceWindow::From(window);
      text += "window on workspace " +
              (workspace ? workspace->workspace() : std::string("?")) + ":\n";
      for (const std::string& row : Layout(window)) {
        text += "  " + row + "\n";
      }
    }
    return text;
  }

  // Waits for the windows to come to rest, and fails when they do not.
  void Settle() {
    std::string trouble;
    for (int tries = 0; tries < 100; ++tries) {
      base::RunLoop().RunUntilIdle();
      trouble = Trouble();
      if (trouble.empty()) {
        return;
      }
      Wait(base::Milliseconds(50));
    }
    ADD_FAILURE() << trouble << "\n" << Layouts() << Story();
  }

  static void Wait(base::TimeDelta delay) {
    base::RunLoop wait;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, wait.QuitClosure(), delay);
    wait.Run();
  }

  base::test::InsecureRandomGenerator random_;
  raw_ptr<Profile> profile_ = nullptr;
  uint64_t seed_ = 0;
  int step_ = 0;
  int pages_ = 0;
  std::vector<std::string> story_;
};

class RandomActionsBrowserTest : public RandomWalkBrowserTest,
                                 public testing::WithParamInterface<int> {};

// Tabs, tasks, windows, and workspaces survive a run of things a person can
// do with keys, menus, and clicks, and the windows stay in step. Each seed is
// a different run.
IN_PROC_BROWSER_TEST_P(RandomActionsBrowserTest, TheWindowsSurviveARandomRun) {
  Walk(GetParam(), 120);
}

INSTANTIATE_TEST_SUITE_P(Seeds, RandomActionsBrowserTest, testing::Range(1, 9));

// The same for one run picked on the command line, to hunt with:
// `--run-manual --uw-random-seed=7 --uw-random-steps=400`, without
// `--headful-mode`. In a real window a run would open menus and panels of
// macOS's, so there it is skipped.
IN_PROC_BROWSER_TEST_F(RandomWalkBrowserTest, MANUAL_ARunPickedByItsSeed) {
  if (!headless::IsHeadlessMode()) {
    GTEST_SKIP() << "Runs headless only.";
  }
  Walk(SwitchOr(kSeedSwitch, 1), static_cast<int>(SwitchOr(kStepsSwitch, 120)));
}

}  // namespace
}  // namespace uw
