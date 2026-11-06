// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/key_card.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "ui/base/page_transition_types.h"
#include "ui/views/controls/label.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/tab_organization/floating_sidebar.h"
#include "uw/browser/ui/views/tab_organization/modifier_keys.h"

namespace uw {
namespace {

// The texts of every label under `view` that is drawn.
void TextsUnder(const views::View* view, std::vector<std::u16string>& texts) {
  if (const auto* label = views::AsViewClass<views::Label>(view);
      label && label->GetVisible()) {
    texts.push_back(std::u16string(label->GetText()));
  }
  for (const views::View* child : view->children()) {
    TextsUnder(child, texts);
  }
}

class KeyCardBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    headless::HeadlessModeBrowserTest::SetUpOnMainThread();
    KeyCard::SetDelayForTesting(base::Milliseconds(10));
  }

  void TearDownOnMainThread() override {
    ModifierKeys::Get().SetForTesting(false, false);
    KeyCard::SetDelayForTesting(base::Milliseconds(500));
    headless::HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  TabOrganizationController* controller() {
    return TabOrganizationController::From(browser());
  }
  TabStripModel* tabs() { return browser()->tab_strip_model(); }
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

  // Option and Cmd, or Ctrl and Alt on Windows.
  void HoldBranchKeys() { ModifierKeys::Get().SetForTesting(true, true); }
  // Ctrl and Cmd, or Alt and Shift on Windows.
  void HoldTaskKeys() {
#if BUILDFLAG(IS_MAC)
    ModifierKeys::Get().SetForTesting(false, true, true, false);
#else
    ModifierKeys::Get().SetForTesting(true, false, false, true);
#endif
  }
  void LetGo() { ModifierKeys::Get().SetForTesting(false, false); }

  bool CardShows() {
    views::Widget* widget = KeyCard::WidgetForTesting();
    return widget && widget->IsVisible();
  }

  std::vector<std::u16string> CardTexts() {
    std::vector<std::u16string> texts;
    if (views::Widget* widget = KeyCard::WidgetForTesting()) {
      TextsUnder(widget->widget_delegate()->GetContentsView(), texts);
    }
    return texts;
  }

  bool CardSays(const std::u16string& text) {
    return std::ranges::contains(CardTexts(), text);
  }

  // The numbers drawn on task rows and the Scratchpad's heading.
  std::vector<std::u16string> Numbers() {
    std::vector<std::u16string> numbers;
    CollectNumbers(browser_view(), numbers);
    return numbers;
  }

  static void CollectNumbers(const views::View* view,
                             std::vector<std::u16string>& numbers) {
    if (const auto* number = views::AsViewClass<TaskKeyNumber>(view);
        number && number->IsDrawn()) {
      numbers.push_back(std::u16string(number->GetText()));
    }
    for (const views::View* child : view->children()) {
      CollectNumbers(child, numbers);
    }
  }

  tabs::TabInterface* NewTab() {
    chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
    base::RunLoop().RunUntilIdle();
    return tabs()->GetActiveTab();
  }

  tabs::TabInterface* OpenLinkFrom(tabs::TabInterface* source) {
    NavigateParams params(browser(), GURL("about:blank"),
                          ui::PAGE_TRANSITION_LINK);
    params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    params.source_contents = source->GetContents();
    Navigate(&params);
    base::RunLoop().RunUntilIdle();
    return tabs()->GetTabForWebContents(params.navigated_or_inserted_contents);
  }

  void Select(tabs::TabInterface* tab) {
    tabs()->ActivateTabAt(tabs()->GetIndexOfTab(tab));
  }

  PageId Id(tabs::TabInterface* tab) {
    return controller()->IdFor(tab).value_or(PageId());
  }

  bool Tinted(tabs::TabInterface* tab) {
    return SkColorGetA(KeyScopeTint(browser_view(), tab, std::nullopt)) != 0;
  }
};

IN_PROC_BROWSER_TEST_F(KeyCardBrowserTest,
                       HoldingTheBranchKeysListsThemAndTintsTheBranch) {
  tabs::TabInterface* parent = tabs()->GetTabAtIndex(0);
  tabs::TabInterface* child = OpenLinkFrom(parent);
  tabs::TabInterface* other = NewTab();
  Select(parent);
  ASSERT_FALSE(CardShows());

  HoldBranchKeys();
  ASSERT_TRUE(base::test::RunUntil([&] { return CardShows(); }));
  EXPECT_EQ(KeyCard::ShownIn(browser()), KeyScope::kBranch);
  EXPECT_TRUE(CardSays(u"The tab and the tabs under it"));
  EXPECT_TRUE(CardSays(u"+ 1 under it"));
  EXPECT_TRUE(CardSays(u"Move to Previous, Next Task"));
  EXPECT_TRUE(CardSays(u"Move to Scratchpad"));
  EXPECT_TRUE(CardSays(u"New Child Tab"));
  EXPECT_TRUE(CardSays(u"Close Branch"));
  EXPECT_TRUE(CardSays(u"Undo Tab Organization"));
  // Move to New Task has no key, so the card has no row for it.
  EXPECT_FALSE(CardSays(u"Move to New Task"));
  EXPECT_TRUE(Tinted(parent));
  EXPECT_TRUE(Tinted(child));
  EXPECT_FALSE(Tinted(other));
  SaveWindowScreenshot(browser(), "key-card-branch");
  SaveWidgetScreenshot(KeyCard::WidgetForTesting(), "key-card-branch-card");

  LetGo();
  EXPECT_FALSE(CardShows());
  EXPECT_EQ(KeyCard::ShownIn(browser()), KeyScope::kNone);
  EXPECT_FALSE(Tinted(parent));
}

IN_PROC_BROWSER_TEST_F(KeyCardBrowserTest, HoldingTheTaskKeysNumbersTheTasks) {
  tabs::TabInterface* plan = tabs()->GetTabAtIndex(0);
  tabs::TabInterface* trip = NewTab();
  tabs::TabInterface* loose = NewTab();
  ASSERT_TRUE(controller()->MoveToNewTask({Id(plan)}, u"Plan"));
  ASSERT_TRUE(controller()->MoveToNewTask({Id(trip)}, u"Trip"));
  Select(trip);

  HoldTaskKeys();
  ASSERT_TRUE(base::test::RunUntil([&] { return CardShows(); }));
  EXPECT_EQ(KeyCard::ShownIn(browser()), KeyScope::kTask);
  EXPECT_TRUE(CardSays(u"The task"));
  EXPECT_TRUE(CardSays(u"Trip"));
  EXPECT_TRUE(CardSays(u"New Task"));
  EXPECT_TRUE(CardSays(u"Close Task"));
  EXPECT_TRUE(CardSays(u"Go to Task"));
  EXPECT_TRUE(CardSays(u"Move Up, Down"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    std::vector<std::u16string> numbers = Numbers();
    std::ranges::sort(numbers);
    return numbers == std::vector<std::u16string>{u"0", u"1", u"2"};
  }));
  EXPECT_TRUE(Tinted(trip));
  EXPECT_FALSE(Tinted(plan));
  EXPECT_FALSE(Tinted(loose));
  SaveWindowScreenshot(browser(), "key-card-task");
  SaveWidgetScreenshot(KeyCard::WidgetForTesting(), "key-card-task-card");

  LetGo();
  EXPECT_TRUE(base::test::RunUntil([&] { return Numbers().empty(); }));
}

// With the sidebar hidden, the task card names the tasks beside their keys.
IN_PROC_BROWSER_TEST_F(KeyCardBrowserTest, AHiddenSidebarsCardListsTheTasks) {
  tabs::TabInterface* plan = tabs()->GetTabAtIndex(0);
  NewTab();
  ASSERT_TRUE(controller()->MoveToNewTask({Id(plan)}, u"Plan"));
  chrome::ExecuteCommand(browser(), IDC_TOGGLE_VERTICAL_TABS_COLLAPSE);
  ASSERT_TRUE(base::test::RunUntil([&] { return IsSidebarHidden(browser()); }));

  HoldTaskKeys();
  ASSERT_TRUE(base::test::RunUntil([&] { return CardShows(); }));
  EXPECT_TRUE(CardSays(u"Go to"));
  EXPECT_TRUE(CardSays(u"Plan"));
  EXPECT_FALSE(CardSays(u"Go to Task"));
  SaveWidgetScreenshot(KeyCard::WidgetForTesting(), "key-card-hidden-sidebar");
}

IN_PROC_BROWSER_TEST_F(KeyCardBrowserTest,
                       AKeyPressedHidesItUntilTheKeysAreLetGo) {
  // A key pressed within the delay never shows the card.
  KeyCard::SetDelayForTesting(base::Milliseconds(200));
  HoldBranchKeys();
  ModifierKeys::Get().PressForTesting();
  base::RunLoop loop;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, loop.QuitClosure(), base::Milliseconds(400));
  loop.Run();
  EXPECT_FALSE(CardShows());

  // Once the keys are let go and held again, it shows, and a key pressed
  // then hides it.
  LetGo();
  KeyCard::SetDelayForTesting(base::Milliseconds(10));
  HoldBranchKeys();
  ASSERT_TRUE(base::test::RunUntil([&] { return CardShows(); }));
  ModifierKeys::Get().PressForTesting();
  EXPECT_FALSE(CardShows());
}

IN_PROC_BROWSER_TEST_F(KeyCardBrowserTest, AnotherModifierHidesIt) {
  HoldBranchKeys();
  ASSERT_TRUE(base::test::RunUntil([&] { return CardShows(); }));

  // Shift joining is no scope.
  ModifierKeys::Get().SetForTesting(true, true, false, true);
  EXPECT_FALSE(CardShows());

  // It comes back once the keys are a scope again.
  HoldBranchKeys();
  EXPECT_TRUE(base::test::RunUntil([&] { return CardShows(); }));
}

}  // namespace
}  // namespace uw
