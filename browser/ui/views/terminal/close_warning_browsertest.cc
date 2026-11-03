// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/terminal/close_warning.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/test/scoped_feature_list.h"
#include "build/build_config.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "ui/base/window_open_disposition.h"
#include "ui/views/controls/label.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "url/gurl.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/agent/fake_chat.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/shared_tree.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/tab_organization/window_screenshot.h"

namespace uw {
namespace {

#if BUILDFLAG(IS_WIN)
constexpr std::u16string_view kQuitTitle = u"Exit uw?";
constexpr std::u16string_view kQuitText =
    u"A chat is still working. Exiting stops it.";
#else
constexpr std::u16string_view kQuitTitle = u"Quit uw?";
constexpr std::u16string_view kQuitText =
    u"A chat is still working. Quitting stops it.";
#endif

// Closing a chat while its agent works asks first, in the busy terminal
// warning's dialog. See docs/plans/agent-chats.md#closing-a-working-chat.
class WorkingChatWarningBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    CloseWarning::AskInTestsForTesting(true);
  }

  void TearDownOnMainThread() override {
    CloseWarning::AskInTestsForTesting(false);
    chats_.clear();
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }
  TabOrganizationController* controller() {
    return TabOrganizationController::From(browser());
  }
  bool IsOpen(content::WebContents* page) {
    return tabs()->GetIndexOfWebContents(page) != TabStripModel::kNoTab;
  }
  void CloseTab(content::WebContents* page) {
    tabs()->CloseWebContentsAt(tabs()->GetIndexOfWebContents(page),
                               TabCloseTypes::CLOSE_USER_GESTURE |
                                   TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  }

  // `url` in a new tab in front.
  content::WebContents* Open(const GURL& url) {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    return tabs()->GetActiveWebContents();
  }
  // A page titled `title` in a new tab.
  content::WebContents* OpenPage(std::string_view title) {
    return Open(
        GURL(base::StrCat({"data:text/html,<title>", title, "</title>"})));
  }
  // A page titled `title` in a new tab, which says it is a chat as
  // uw://chat does.
  content::WebContents* OpenChat(std::string_view title,
                                 ChatWork work = ChatWork::kWorking) {
    content::WebContents* page = OpenPage(title);
    chats_.push_back(std::make_unique<FakeChat>(page, work));
    return page;
  }
  std::optional<tab_groups::TabGroupId> MoveToNewTask(
      const std::vector<content::WebContents*>& pages) {
    std::vector<PageId> ids;
    for (content::WebContents* page : pages) {
      ids.push_back(*controller()->IdFor(tabs()->GetTabForWebContents(page)));
    }
    return controller()->MoveToNewTask(ids, u"Lisbon hotels");
  }

  views::Widget* Dialog() {
    views::Widget* dialog = nullptr;
    EXPECT_TRUE(base::test::RunUntil([&] {
      dialog = CloseWarning::DialogForTesting(browser());
      return dialog != nullptr;
    }));
    return dialog;
  }
  std::u16string TitleOf(views::Widget* dialog) {
    return dialog->widget_delegate()->GetWindowTitle();
  }
  // Whether the dialog shows a label reading `text`.
  bool Shows(views::Widget* dialog, std::u16string_view text) {
    const std::function<bool(views::View*)> find = [&](views::View* view) {
      auto* label = views::AsViewClass<views::Label>(view);
      if (label && label->GetText() == text) {
        return true;
      }
      return std::ranges::any_of(view->children(), find);
    };
    return find(dialog->GetRootView());
  }
  // Presses the close button or Cancel, and waits until the answer has run.
  void Answer(views::Widget* dialog, bool close) {
    views::DialogDelegate* delegate =
        dialog->widget_delegate()->AsDialogDelegate();
    if (close) {
      delegate->AcceptDialog();
    } else {
      delegate->CancelDialog();
    }
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return !CloseWarning::DialogForTesting(browser()); }));
    base::RunLoop().RunUntilIdle();
  }

  std::vector<std::unique_ptr<FakeChat>> chats_;
};

IN_PROC_BROWSER_TEST_F(WorkingChatWarningBrowserTest,
                       ClosingATaskWithAWorkingChatAsksFirst) {
  content::WebContents* page = tabs()->GetActiveWebContents();
  content::WebContents* chat = OpenChat("Find a hotel in Alfama");
  const std::optional<tab_groups::TabGroupId> task = MoveToNewTask({chat});
  ASSERT_TRUE(task);
  tabs()->ActivateTabAt(tabs()->GetIndexOfWebContents(page));
  controller()->CloseTask(*task);
  views::Widget* dialog = Dialog();
  EXPECT_EQ(TitleOf(dialog), u"Close this task?");
  EXPECT_TRUE(Shows(
      dialog, u"A chat in it is still working. Closing the task stops it."));
  EXPECT_TRUE(Shows(dialog, u"Find a hotel in Alfama"));
  EXPECT_TRUE(Shows(dialog, u"Working"));
  SaveWidgetScreenshot(dialog, "working-chat-task");
  Answer(dialog, false);
  EXPECT_TRUE(IsOpen(chat));
  EXPECT_TRUE(tabs()->group_model()->ContainsTabGroup(*task));

  controller()->CloseTask(*task);
  Answer(Dialog(), true);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !tabs()->group_model()->ContainsTabGroup(*task); }));
  EXPECT_TRUE(IsOpen(page));
}

// A chat waiting for approval counts as working. Once it has answered, its
// tab closes without a question.
IN_PROC_BROWSER_TEST_F(WorkingChatWarningBrowserTest,
                       ClosingAWorkingChatsTabAsksFirst) {
  content::WebContents* chat =
      OpenChat("Book an Alfama tour", ChatWork::kWaitsForApproval);
  CloseTab(chat);
  views::Widget* dialog = Dialog();
  EXPECT_EQ(TitleOf(dialog), u"Close this tab?");
  EXPECT_TRUE(
      Shows(dialog, u"This chat is still working. Closing the tab stops it."));
  EXPECT_TRUE(Shows(dialog, u"Waits for approval"));
  SaveWidgetScreenshot(dialog, "working-chat-tab");
  Answer(dialog, false);
  EXPECT_TRUE(IsOpen(chat));

  chats_.back()->set_work(ChatWork::kIdle);
  CloseTab(chat);
  EXPECT_TRUE(base::test::RunUntil([&] { return !IsOpen(chat); }));
  EXPECT_FALSE(CloseWarning::DialogForTesting(browser()));
}

// Only the chat's own tab counts: a page the agent works in closes at once.
IN_PROC_BROWSER_TEST_F(WorkingChatWarningBrowserTest,
                       AnAgentsPageClosesWithoutAsking) {
  content::WebContents* chat = OpenChat("Find a hotel in Alfama");
  content::WebContents* page = OpenPage("Memmo Alfama Hotel");
  CloseTab(page);
  EXPECT_TRUE(base::test::RunUntil([&] { return !IsOpen(page); }));
  EXPECT_FALSE(CloseWarning::DialogForTesting(browser()));
  EXPECT_TRUE(IsOpen(chat));
}

IN_PROC_BROWSER_TEST_F(WorkingChatWarningBrowserTest,
                       ClosingSeveralTabsWithAWorkingChatAsksFirst) {
  content::WebContents* page = tabs()->GetActiveWebContents();
  content::WebContents* chat = OpenChat("Find a hotel in Alfama");
  content::WebContents* hotel = OpenPage("Memmo Alfama Hotel");
  tabs()->ExecuteCloseTabsCommand(base::BindLambdaForTesting([&] {
                                    return std::vector<tabs::TabInterface*>{
                                        tabs()->GetTabForWebContents(chat),
                                        tabs()->GetTabForWebContents(hotel)};
                                  }),
                                  false);
  views::Widget* dialog = Dialog();
  EXPECT_EQ(TitleOf(dialog), u"Close 2 tabs?");
  EXPECT_TRUE(Shows(
      dialog, u"A chat in them is still working. Closing the tabs stops it."));
  Answer(dialog, true);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return !IsOpen(chat) && !IsOpen(hotel); }));
  EXPECT_TRUE(IsOpen(page));
}

IN_PROC_BROWSER_TEST_F(WorkingChatWarningBrowserTest,
                       QuittingAsksAboutAWorkingChat) {
  OpenChat("Find a hotel in Alfama");
  const int count = tabs()->count();
  chrome::CloseAllBrowsersAndQuit();
  views::Widget* dialog = Dialog();
  EXPECT_EQ(TitleOf(dialog), kQuitTitle);
  EXPECT_TRUE(Shows(dialog, kQuitText));
  SaveWidgetScreenshot(dialog, "working-chat-quit");
  Answer(dialog, false);
  EXPECT_TRUE(
      base::test::RunUntil([] { return !browser_shutdown::IsTryingToQuit(); }));
  EXPECT_EQ(tabs()->count(), count);
}

IN_PROC_BROWSER_TEST_F(WorkingChatWarningBrowserTest,
                       ClearLeavesAWorkingChatOpen) {
  content::WebContents* chat = OpenChat("Find a hotel in Alfama");
  content::WebContents* idle = OpenChat("Book an Alfama tour", ChatWork::kIdle);
  chrome::ExecuteCommand(browser(), kClearScratchpad);
  EXPECT_TRUE(base::test::RunUntil([&] { return !IsOpen(idle); }));
  EXPECT_TRUE(IsOpen(chat));
}

// With shared tabs off, closing a window closes its tabs, so uw asks first.
class WorkingChatWarningOffBrowserTest : public WorkingChatWarningBrowserTest {
 public:
  WorkingChatWarningOffBrowserTest() {
    features_.InitAndDisableFeature(kUwSharedTabs);
  }

 private:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(WorkingChatWarningOffBrowserTest,
                       ClosingTheWindowAsksAboutAWorkingChat) {
  OpenChat("Find a hotel in Alfama");
  const int count = tabs()->count();
  chrome::CloseWindow(browser());
  views::Widget* dialog = Dialog();
  EXPECT_EQ(TitleOf(dialog), u"Close this window?");
  EXPECT_TRUE(Shows(
      dialog, u"A chat in it is still working. Closing the window stops it."));
  EXPECT_TRUE(Shows(dialog, u"Scratchpad"));
  SaveWidgetScreenshot(dialog, "working-chat-window");
  Answer(dialog, false);
  EXPECT_EQ(tabs()->count(), count);
}

}  // namespace
}  // namespace uw
