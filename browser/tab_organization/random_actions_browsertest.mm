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
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/bind.h"
#include "base/test/insecure_random_generator.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
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
#include "ui/base/test/ui_controls.h"
#include "ui/base/window_open_disposition.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/test/cocoa_test_event_utils.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
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
// `--uw-random-show-windows` logs every window's tabs after each step.
constexpr char kShowSwitch[] = "uw-random-show-windows";

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
    // With a split open the sidebar stops following its tree's order (T407).
    // Until that is fixed a run makes no splits.
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

  void TearDownOnMainThread() override {
    profile_ = nullptr;
    headless::HeadlessModeBrowserTest::TearDownOnMainThread();
  }

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
      ShowWindows();
    }
  }

  void Start(uint64_t seed) {
    seed_ = seed;
    random_.ReseedForTesting(seed);
    profile_ = browser()->GetProfile();
    show_ = base::CommandLine::ForCurrentProcess()->HasSwitch(kShowSwitch);
  }

  void ShowWindows() {
    if (show_) {
      LOG(WARNING) << "random walk, after step " << step_ << ":\n" << Layouts();
    }
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

  // A mouse button held on a row. The press and the release go through the
  // window, which sends them on as it would a real mouse's.
  struct Held {
    base::WeakPtr<views::Widget> widget;
    gfx::Point at;
    gfx::Point on_screen;
  };

  static Held Press(views::View* row) {
    Held held{row->GetWidget()->GetWeakPtr(),
              row->GetLocalBounds().CenterPoint(),
              row->GetBoundsInScreen().CenterPoint()};
    views::View::ConvertPointToWidget(row, &held.at);
    ui::MouseEvent press(ui::EventType::kMousePressed, held.at, held.on_screen,
                         ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                         ui::EF_LEFT_MOUSE_BUTTON);
    held.widget->OnMouseEvent(&press);
    return held;
  }

  static void LetGo(const Held& held) {
    if (!held.widget) {
      return;
    }
    ui::MouseEvent release(ui::EventType::kMouseReleased, held.at,
                           held.on_screen, ui::EventTimeForNow(),
                           ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
    held.widget->OnMouseEvent(&release);
  }

  // A press on a row that is held while something else happens, then let go.
  // With the mouse down a person can still press keys, a page can close
  // itself, and the organizer can file a tab it is free to move.
  void HoldARow(Browser* window) {
    const std::vector<views::View*> rows = Rows(window);
    if (rows.empty()) {
      return;
    }
    Note("presses a row and holds it");
    const Held held = Press(rows[Pick(rows.size())]);

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
    // Any key ends a drag, so every other step lets go with no key pressed
    // and leaves the ending to the row.
    if (held.widget && step_ % 2 == 1) {
      Note("presses Cmd");
      [NSApp sendEvent:cocoa_test_event_utils::SynthesizeKeyEvent(
                           held.widget->GetNativeWindow().GetNativeNSWindow(),
                           /*keyDown=*/true, ui::VKEY_COMMAND,
                           NSEventModifierFlagCommand)];
    }
    Note("lets go of the row");
    LetGo(held);
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
      if (organizer->dragging()) {
        return "a window takes a tab as dragged, with no mouse button down";
      }
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
      text +=
          "window on workspace " +
          (workspace ? workspace->workspace() : std::string("?")) +
          (TabOrganizationController::From(window)->dragging() ? ", dragging"
                                                               : "") +
          ":\n";
      for (const std::string& row : Layout(window)) {
        text += "  " + row + "\n";
      }
      TabStripModel* tabs = window->tab_strip_model();
      for (const tab_groups::TabGroupId& group :
           tabs->group_model()->ListTabGroups()) {
        text += base::StringPrintf(
            "  task \"%s\": tabs %s\n",
            base::UTF16ToUTF8(
                tabs->group_model()->GetTabGroup(group)->visual_data()->title())
                .c_str(),
            tabs->group_model()
                ->GetTabGroup(group)
                ->ListTabs()
                .ToString()
                .c_str());
      }
      text += "  selected:";
      for (size_t index :
           tabs->selection_model().GetListSelectionModel().selected_indices()) {
        text += " " + base::NumberToString(index);
      }
      text += "\n";
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
  bool show_ = false;
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

// Drags rows in the sidebar with a real mouse, to places and at speeds picked
// from a seed, while keys are pressed and tabs close. A headless window takes
// no drags, so this needs a real one. Run it with `pnpm run window-tests`.
class RandomDragsBrowserTest : public RandomWalkBrowserTest {
 protected:
  // One thing a drag does, which calls its argument when it is done. A drag
  // that takes a tab out of its window runs a loop of its own until the drop,
  // so each starts when the one before is done instead of the test waiting.
  using Stage = base::OnceCallback<void(base::OnceClosure)>;

  static void Play(std::vector<Stage> stages,
                   size_t next,
                   base::TimeDelta gap,
                   size_t* reached,
                   base::OnceClosure done) {
    *reached = next;
    if (next == stages.size()) {
      std::move(done).Run();
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(
            [](std::vector<Stage> stages, size_t next, base::TimeDelta gap,
               size_t* reached, base::OnceClosure done) {
              Stage stage = std::move(stages[next]);
              std::move(stage).Run(base::BindOnce(&Play, std::move(stages),
                                                  next + 1, gap, reached,
                                                  std::move(done)));
            },
            std::move(stages), next, gap, reached, std::move(done)),
        gap);
  }

  // Runs `loop` until it quits, and fails after 20 seconds: a real-window
  // run in the VM has no time limit of its own on a RunLoop.
  void RunOrFail(base::RunLoop& loop, const std::string& what) {
    bool late = false;
    base::OneShotTimer limit;
    limit.Start(FROM_HERE, base::Seconds(20), base::BindLambdaForTesting([&] {
                  late = true;
                  loop.Quit();
                }));
    loop.Run();
    if (late) {
      ADD_FAILURE() << "a drag never got past " << what << "\n"
                    << Layouts() << Story();
    }
  }

  static Stage MoveTo(const gfx::Point& point) {
    return base::BindOnce(
        [](gfx::Point point, base::OnceClosure done) {
          ui_controls::SendMouseMoveNotifyWhenDone(point.x(), point.y(),
                                                   std::move(done));
        },
        point);
  }

  // A tree to drag in: tabs opened from one another, and two tasks.
  void OpenATree() {
    Browser* window = browser();
    TabStripModel* tabs = window->tab_strip_model();
    for (int i = 0; i < 7; ++i) {
      tabs->ActivateTabAt(static_cast<int>(Pick(tabs->count())));
      OpenALink(window);
      base::RunLoop().RunUntilIdle();
    }
    TabOrganizationController* organizer =
        TabOrganizationController::From(window);
    for (const char16_t* task : {u"Plan", u"Trip"}) {
      const std::optional<PageId> id = organizer->IdFor(
          tabs->GetTabAtIndex(static_cast<int>(Pick(tabs->count()))));
      if (id) {
        Note("moves a tab to a new task");
        organizer->MoveToNewTask({*id}, task);
      }
    }
    Settle();
  }

  // The rows of `window` a real mouse can reach: those on the screen, clear
  // of the menu bar and the Dock, where a press goes to another app.
  static std::vector<views::View*> RowsInReach(Browser* window) {
    const gfx::Rect reach = BrowserView::GetBrowserViewForBrowser(window)
                                ->GetWidget()
                                ->GetWorkAreaBoundsInScreen();
    std::vector<views::View*> rows = Rows(window);
    std::erase_if(rows, [&](views::View* row) {
      return !reach.Contains(row->GetBoundsInScreen().CenterPoint());
    });
    return rows;
  }

  void DragARow() {
    const std::vector<Browser*> windows = Windows();
    Browser* window = windows[Pick(windows.size())];
    window->GetWindow()->Activate();
    Wait(base::Milliseconds(400));
    const std::vector<views::View*> rows = RowsInReach(window);
    if (rows.empty()) {
      return;
    }
    const gfx::Point from =
        rows[Pick(rows.size())]->GetBoundsInScreen().CenterPoint();

    gfx::Point to;
    std::string where;
    const size_t kind = Pick(20);
    if (kind < 3) {
      to = gfx::Point(from.x() + static_cast<int>(Pick(9)) - 4,
                      from.y() + 5 + static_cast<int>(Pick(8)));
      where = "a few points";
    } else if (kind < 6 && windows.size() < kMostWindows) {
      const gfx::Rect bounds = BrowserView::GetBrowserViewForBrowser(window)
                                   ->GetWidget()
                                   ->GetWindowBoundsInScreen();
      to = gfx::Point(bounds.right() + 60 + static_cast<int>(Pick(120)),
                      bounds.y() + 200 + static_cast<int>(Pick(200)));
      where = "out of the window";
    } else if (kind < 8) {
      const gfx::Rect last = rows.back()->GetBoundsInScreen();
      to =
          gfx::Point(from.x(), last.bottom() + 20 + static_cast<int>(Pick(40)));
      where = "below the tabs";
    } else {
      const std::vector<views::View*> targets =
          RowsInReach(windows[Pick(windows.size())]);
      if (targets.empty()) {
        return;
      }
      const gfx::Rect onto = targets[Pick(targets.size())]->GetBoundsInScreen();
      constexpr std::array<int, 7> kSideways = {-48, -24, 0, 0, 0, 24, 48};
      const std::array<int, 3> ys = {onto.y() + 3, onto.CenterPoint().y(),
                                     onto.bottom() - 3};
      to =
          gfx::Point(onto.CenterPoint().x() + kSideways[Pick(kSideways.size())],
                     ys[Pick(ys.size())]);
      where = "onto a row";
    }

    gfx::Rect reach = BrowserView::GetBrowserViewForBrowser(window)
                          ->GetWidget()
                          ->GetWorkAreaBoundsInScreen();
    reach.Inset(4);
    to.SetToMax(reach.origin());
    to.SetToMin(gfx::Point(reach.right(), reach.bottom()));

    constexpr std::array<int, 6> kModifiers = {
        ui_controls::kNoAccelerator, ui_controls::kNoAccelerator,
        ui_controls::kNoAccelerator, ui_controls::kNoAccelerator,
        ui_controls::kAlt,           ui_controls::kCommand};
    const int modifiers = kModifiers[Pick(kModifiers.size())];
    const int moves = 2 + static_cast<int>(Pick(14));
    const base::TimeDelta gap =
        base::Milliseconds(8 + static_cast<int>(Pick(24)));

    std::vector<Stage> stages;
    for (int move = 1; move <= moves; ++move) {
      stages.push_back(
          MoveTo(gfx::Point(from.x() + (to.x() - from.x()) * move / moves,
                            from.y() + (to.y() - from.y()) * move / moves)));
    }
    std::string meanwhile;
    if (Chance(30)) {
      const uint64_t which = random_.RandUint64();
      const size_t at = Pick(stages.size() + 1);
      switch (Pick(3)) {
        case 0:
          meanwhile = ", pressing Escape on the way";
          stages.insert(
              stages.begin() + at,
              base::BindOnce(
                  [](gfx::NativeWindow native, base::OnceClosure done) {
                    ui_controls::SendKeyPressNotifyWhenDone(
                        native, ui::VKEY_ESCAPE, /*control=*/false,
                        /*shift=*/false, /*alt=*/false, /*command=*/false,
                        std::move(done));
                  },
                  window->GetWindow()->GetNativeWindow()));
          break;
        case 1:
          meanwhile = ", pressing Cmd on the way";
          stages.insert(
              stages.begin() + at,
              base::BindOnce(
                  [](gfx::NativeWindow native, base::OnceClosure done) {
                    [NSApp sendEvent:cocoa_test_event_utils::SynthesizeKeyEvent(
                                         native.GetNativeNSWindow(),
                                         /*keyDown=*/true, ui::VKEY_COMMAND,
                                         NSEventModifierFlagCommand)];
                    std::move(done).Run();
                  },
                  window->GetWindow()->GetNativeWindow()));
          break;
        default:
          meanwhile = ", while a tab closes itself";
          stages.insert(stages.begin() + at,
                        base::BindOnce(&RandomDragsBrowserTest::CloseATab,
                                       base::Unretained(this), which));
      }
    }
    stages.push_back(base::BindOnce(
        [](int modifiers, base::OnceClosure done) {
          ui_controls::SendMouseEventsNotifyWhenDone(
              ui_controls::LEFT, ui_controls::UP, std::move(done), modifiers);
        },
        modifiers));

    Note(base::StringPrintf(
        "drags a row from %s %s to %s%s%s, in %d moves %d ms apart",
        from.ToString().c_str(), where.c_str(), to.ToString().c_str(),
        modifiers == ui_controls::kAlt       ? " with Option"
        : modifiers == ui_controls::kCommand ? " with Cmd"
                                             : "",
        meanwhile.c_str(), moves, static_cast<int>(gap.InMilliseconds())));
    ui_controls::EnableUIControls();
    base::RunLoop moved;
    ASSERT_TRUE(ui_controls::SendMouseMoveNotifyWhenDone(from.x(), from.y(),
                                                         moved.QuitClosure()));
    RunOrFail(moved, "the move to the row");
    base::RunLoop pressed;
    ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
        ui_controls::LEFT, ui_controls::DOWN, pressed.QuitClosure(),
        modifiers));
    RunOrFail(pressed, "the press");
    if (HasFailure()) {
      return;
    }
    base::RunLoop dropped;
    const size_t count = stages.size();
    size_t reached = 0;
    Play(std::move(stages), 0, gap, &reached, dropped.QuitClosure());
    RunOrFail(dropped,
              base::StringPrintf("part %zu of %zu on the way", reached, count));
    if (HasFailure()) {
      return;
    }
    Wait(base::Milliseconds(800));
  }

  // A page closes its own tab, as window.close() does.
  void CloseATab(uint64_t which, base::OnceClosure done) {
    const std::vector<Browser*> windows = Windows();
    if (!windows.empty()) {
      TabStripModel* tabs = windows[which % windows.size()]->tab_strip_model();
      if (tabs->count() > 1) {
        tabs->CloseWebContentsAt(static_cast<int>(which % tabs->count()),
                                 TabCloseTypes::CLOSE_NONE);
      }
    }
    std::move(done).Run();
  }
};

// The sidebar survives a run of drags, with the animations a desktop shows,
// and the windows stay in step. `--uw-random-seed` and `--uw-random-steps`
// pick the run.
IN_PROC_BROWSER_TEST_F(RandomDragsBrowserTest,
                       DISABLED_TheSidebarSurvivesDrags) {
  const auto animated = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  Start(SwitchOr(kSeedSwitch, 1));
  BrowserView::GetBrowserViewForBrowser(browser())->GetWidget()->SetBounds(
      gfx::Rect(0, 40, 520, 620));
  OpenATree();
  const int drags = static_cast<int>(SwitchOr(kStepsSwitch, 10));
  for (step_ = 1; step_ <= drags && !HasFailure(); ++step_) {
    if (Windows().empty()) {
      break;
    }
    DragARow();
    Settle();
    ShowWindows();
  }
  if (HasFailure()) {
    LOG(ERROR) << Story();
  }
}

}  // namespace
}  // namespace uw
