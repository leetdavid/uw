// Copyright 2026 The uw Authors. All rights reserved.

// Throwaway spike for T252 in docs/plans/shared-tabs.md: copies a workspace's
// tabs into each window that shows it, as PinSync copies pins, and reports
// what that costs and what the rest of Chromium sees. Removed once the
// findings are in docs/research/shared-tabs-spike.md.

#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/process/process_metrics.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "base/timer/elapsed_timer.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/tabs/tabs_api.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/performance_manager/policies/cannot_discard_reason.h"
#include "chrome/browser/performance_manager/policies/discard_eligibility_policy.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/tab_group_sync/tab_group_sync_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabrestore.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/performance_manager/public/graph/page_node.h"
#include "components/performance_manager/public/performance_manager.h"
#include "components/saved_tab_groups/public/tab_group_sync_service.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "components/sessions/core/serialized_user_agent_override.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/sessions/core/tab_restore_types.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/browser/api_test_utils.h"
#include "extensions/common/extension_builder.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "url/gurl.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

using performance_manager::PageNode;
using performance_manager::PerformanceManager;
using performance_manager::policies::CanDiscardResult;
using performance_manager::policies::CannotDiscardReason;
using performance_manager::policies::CannotDiscardReasonToString;
using performance_manager::policies::DiscardEligibilityPolicy;
using tab_groups::TabGroupId;
using tab_groups::TabGroupVisualData;

void Report(const std::string& line) {
  printf("shared-tabs spike: %s\n", line.c_str());
  fflush(stdout);
}

// The browser process's memory as Activity Monitor counts it.
double Megabytes() {
  const auto info =
      base::ProcessMetrics::CreateCurrentProcessMetrics()->GetMemoryInfo();
  return info.has_value() ? info->physical_footprint_bytes / 1048576.0 : 0;
}

void Settle() {
  base::RunLoop().RunUntilIdle();
  base::RunLoop run;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, run.QuitClosure(), base::Milliseconds(500));
  run.Run();
  base::RunLoop().RunUntilIdle();
}

// A copy of `page`'s history with no renderer, as PinSync's placeholders.
std::unique_ptr<content::WebContents> Placeholder(content::WebContents* page) {
  content::WebContents::CreateParams params(page->GetBrowserContext());
  params.initially_hidden = true;
  params.desired_renderer_state =
      content::WebContents::CreateParams::kNoRendererProcess;
  std::unique_ptr<content::WebContents> copy =
      content::WebContents::Create(params);
  copy->GetController().CopyStateFrom(&page->GetController(),
                                      /*needs_reload=*/false);
  return copy;
}

// Appends a placeholder for each tab of `from` to `to`.
void Mirror(TabStripModel* from, TabStripModel* to) {
  for (int i = 0; i < from->count(); ++i) {
    to->InsertWebContentsAt(to->count(), Placeholder(from->GetWebContentsAt(i)),
                            AddTabTypes::ADD_NONE);
  }
}

// Adds `count` unloaded tabs to `window`, each with three pages of history,
// as a restart leaves them.
void AddRestoredTabs(BrowserWindowInterface* window,
                     int count,
                     const std::string& site) {
  for (int i = 0; i < count; ++i) {
    std::vector<sessions::SerializedNavigationEntry> entries(3);
    for (int step = 0; step < 3; ++step) {
      entries[step].set_index(step);
      entries[step].set_virtual_url(GURL(base::StringPrintf(
          "https://%s.example/article/%d/page/%d", site.c_str(), i, step)));
      entries[step].set_title(base::UTF8ToUTF16(
          base::StringPrintf("%s article %d, page %d", site.c_str(), i, step)));
    }
    chrome::AddRestoredTab(
        window, entries, window->GetTabStripModel()->count(),
        /*selected_navigation=*/2, /*extension_app_id=*/std::string(),
        /*group=*/std::nullopt, /*select=*/false, /*pin=*/false,
        base::TimeTicks::Now(), base::Time::Now(),
        /*storage_namespace=*/nullptr, sessions::SerializedUserAgentOverride(),
        /*extra_data=*/{}, /*from_session_restore=*/false,
        /*is_active_browser=*/true);
  }
}

// What Memory Saver would say about unloading `page` now.
std::string MemorySaverSays(content::WebContents* page) {
  base::WeakPtr<PageNode> node =
      PerformanceManager::GetPrimaryPageNodeForWebContents(page);
  if (!node) {
    return "no page node";
  }
  std::vector<CannotDiscardReason> reasons;
  const CanDiscardResult result =
      DiscardEligibilityPolicy::GetFromGraph(node->GetGraph())
          ->CanDiscard(node.get(),
                       DiscardEligibilityPolicy::DiscardReason::PROACTIVE,
                       /*ignore_recent_visibility=*/true, &reasons);
  std::string text = result == CanDiscardResult::kEligible    ? "eligible"
                     : result == CanDiscardResult::kProtected ? "protected"
                                                              : "disallowed";
  for (CannotDiscardReason reason : reasons) {
    text += std::string(" ") + CannotDiscardReasonToString(reason);
  }
  return text;
}

// Keeps the tasks of `to` in step with those of `from`, whose tabs it copies
// in the same order, using only Chromium's own tab group calls.
class TaskMirror {
 public:
  TaskMirror(TabStripModel* from, TabStripModel* to) : from_(from), to_(to) {
    for (int i = 0; i < from->count(); ++i) {
      copies_[from->GetWebContentsAt(i)] = to->GetWebContentsAt(i);
    }
  }

  void Resync() {
    // A task moves as a whole first, so moving its tabs one at a time never
    // splits it.
    for (int i = 0; i < from_->count(); ++i) {
      const std::optional<TabGroupId> source = from_->GetTabGroupForTab(i);
      if (!source || (i > 0 && from_->GetTabGroupForTab(i - 1) == source)) {
        continue;
      }
      const auto copy = groups_.find(*source);
      if (copy == groups_.end() ||
          !to_->group_model()->ContainsTabGroup(copy->second)) {
        continue;
      }
      if (static_cast<int>(to_->group_model()
                               ->GetTabGroup(copy->second)
                               ->ListTabs()
                               .start()) != i) {
        to_->MoveGroupTo(copy->second, i);
      }
    }
    for (int i = 0; i < from_->count(); ++i) {
      const int at =
          to_->GetIndexOfWebContents(copies_.at(from_->GetWebContentsAt(i)));
      if (at != i) {
        to_->MoveWebContentsAt(at, i, /*select_after_move=*/false);
      }
    }
    for (int i = 0; i < from_->count(); ++i) {
      const std::optional<TabGroupId> source = from_->GetTabGroupForTab(i);
      std::optional<TabGroupId> want;
      if (source) {
        const auto found = groups_.find(*source);
        if (found == groups_.end() ||
            !to_->group_model()->ContainsTabGroup(found->second)) {
          groups_.insert_or_assign(*source, to_->AddToNewGroup({i}));
          continue;
        }
        want = found->second;
      }
      if (to_->GetTabGroupForTab(i) == want) {
        continue;
      }
      if (want) {
        to_->AddToExistingGroup({i}, *want);
      } else {
        to_->RemoveFromGroup({i});
      }
    }
    for (const auto& [source, copy] : groups_) {
      if (!from_->group_model()->ContainsTabGroup(source) ||
          !to_->group_model()->ContainsTabGroup(copy)) {
        continue;
      }
      const TabGroupVisualData& visuals =
          *from_->group_model()->GetTabGroup(source)->visual_data();
      if (*to_->group_model()->GetTabGroup(copy)->visual_data() != visuals) {
        to_->ChangeTabGroupVisuals(copy, visuals);
      }
    }
  }

  // What differs between the two windows, or "in step".
  std::string Differences() const {
    std::string out;
    if (from_->count() != to_->count()) {
      out += base::StringPrintf(" %d tabs against %d;", from_->count(),
                                to_->count());
    }
    for (int i = 0; i < from_->count() && i < to_->count(); ++i) {
      if (to_->GetWebContentsAt(i) != copies_.at(from_->GetWebContentsAt(i))) {
        out += base::StringPrintf(" tab %d out of order;", i);
      }
      const std::optional<TabGroupId> source = from_->GetTabGroupForTab(i);
      const std::optional<TabGroupId> copy = to_->GetTabGroupForTab(i);
      if (source.has_value() != copy.has_value() ||
          (source && groups_.at(*source) != *copy)) {
        out += base::StringPrintf(" tab %d in another task;", i);
        continue;
      }
      if (source &&
          *from_->group_model()->GetTabGroup(*source)->visual_data() !=
              *to_->group_model()->GetTabGroup(*copy)->visual_data()) {
        out += base::StringPrintf(" tab %d's task looks different;", i);
      }
    }
    return out.empty() ? "in step" : out;
  }

 private:
  const raw_ptr<TabStripModel> from_;
  const raw_ptr<TabStripModel> to_;
  std::map<content::WebContents*, content::WebContents*> copies_;
  std::map<TabGroupId, TabGroupId> groups_;
};

constexpr char kPlain[] = "<title>Plain</title><p>Nothing runs here.";
// Counts up in its title, as a timer does.
constexpr char kTimer[] =
    "<title>0</title><script>let n = 0;"
    "setInterval(() => document.title = ++n, 1000)</script>";
constexpr char kForm[] = "<title>Form</title><input id=f>";

class SharedTabsSpikeBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    headless::HeadlessModeBrowserTest::SetUpOnMainThread();
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        [](const net::test_server::HttpRequest& request)
            -> std::unique_ptr<net::test_server::HttpResponse> {
          const char* body = request.relative_url == "/timer"  ? kTimer
                             : request.relative_url == "/form" ? kForm
                                                               : kPlain;
          auto response =
              std::make_unique<net::test_server::BasicHttpResponse>();
          response->set_content_type("text/html");
          response->set_content(body);
          return response;
        }));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  content::WebContents* Open(Browser* window,
                             const std::string& path,
                             bool select = false) {
    chrome::AddTabAt(window, embedded_test_server()->GetURL(path), -1, select);
    content::WebContents* page = window->tab_strip_model()->GetWebContentsAt(
        window->tab_strip_model()->count() - 1);
    EXPECT_TRUE(content::WaitForLoadStop(page));
    return page;
  }

  // Waits until `page` has loaded and gone quiet, from when Chromium counts
  // its title changes out of sight as activity, and then for one more change.
  static void WaitUntilIdleAndRetitled(content::WebContents* page) {
    ASSERT_TRUE(base::test::RunUntil([&] {
      base::WeakPtr<PageNode> node =
          PerformanceManager::GetPrimaryPageNodeForWebContents(page);
      return node &&
             node->GetLoadingState() == PageNode::LoadingState::kLoadedIdle;
    }));
    const std::u16string title = page->GetTitle();
    ASSERT_TRUE(
        base::test::RunUntil([&] { return page->GetTitle() != title; }));
    base::RunLoop().RunUntilIdle();
  }
};

// Four windows on one workspace of 100 tabs, each with a hidden partner for a
// second workspace of 100 tabs.
IN_PROC_BROWSER_TEST_F(SharedTabsSpikeBrowserTest, WhatCopiesCost) {
  Profile* profile = browser()->GetProfile();
  const double start = Megabytes();
  AddRestoredTabs(browser(), 100, "work");
  Settle();
  const double one_window = Megabytes();
  Report(base::StringPrintf("100 unloaded tabs in one window: %+.1f MB",
                            one_window - start));

  std::vector<Browser*> work = {browser()};
  for (int i = 0; i < 3; ++i) {
    work.push_back(CreateBrowser(profile));
  }
  Settle();
  const double empty_windows = Megabytes();
  Report(base::StringPrintf("3 more windows with a blank tab each: %+.1f MB",
                            empty_windows - one_window));

  base::ElapsedTimer copying;
  for (size_t i = 1; i < work.size(); ++i) {
    Mirror(browser()->tab_strip_model(), work[i]->tab_strip_model());
  }
  const base::TimeDelta inserted = copying.Elapsed();
  Settle();
  const double copied = Megabytes();
  Report(base::StringPrintf(
      "copying 101 tabs into 3 windows (303 copies): %lld ms to insert, "
      "%+.1f MB, %.1f KB a copy",
      inserted.InMilliseconds(), copied - empty_windows,
      (copied - empty_windows) * 1024 / 303));

  WorkspaceList& list = WorkspaceList::Get();
  const std::string personal = list.Add(
      "Personal", std::string(list.UnusedIcon()), profile->GetBaseName());
  std::vector<BrowserWindowInterface*> partners;
  for (Browser* window : work) {
    WorkspaceWindow::From(window)->WithPartner(
        personal, base::BindOnce(
                      [](std::vector<BrowserWindowInterface*>* partners,
                         BrowserWindowInterface* partner) {
                        partners->push_back(partner);
                      },
                      &partners));
  }
  ASSERT_TRUE(base::test::RunUntil([&] { return partners.size() == 4u; }));
  Settle();
  const double with_partners = Megabytes();
  Report(base::StringPrintf("4 hidden partners with a blank tab each: %+.1f MB",
                            with_partners - copied));

  AddRestoredTabs(partners[0], 100, "personal");
  base::ElapsedTimer partner_copying;
  for (size_t i = 1; i < partners.size(); ++i) {
    Mirror(partners[0]->GetTabStripModel(), partners[i]->GetTabStripModel());
  }
  const base::TimeDelta partner_inserted = partner_copying.Elapsed();
  Settle();
  const double all = Megabytes();
  Report(base::StringPrintf(
      "100 tabs in a hidden partner and 303 copies in 3 more: %lld ms to "
      "insert the copies, %+.1f MB",
      partner_inserted.InMilliseconds(), all - with_partners));
  Report(
      base::StringPrintf("browser process total: %.1f MB, %.1f MB over the "
                         "start, for 8 windows and 808 tab objects",
                         all, all - start));

  // One more tab opened in a window reaches the other three.
  content::WebContents* live = Open(browser(), "/form");
  base::ElapsedTimer one_tab;
  for (size_t i = 1; i < work.size(); ++i) {
    work[i]->tab_strip_model()->InsertWebContentsAt(
        work[i]->tab_strip_model()->count(), Placeholder(live),
        AddTabTypes::ADD_NONE);
  }
  Report(base::StringPrintf("a new tab copied into 3 windows: %.2f ms",
                            one_tab.Elapsed().InMillisecondsF()));

  // Its live page moves to the second window and back, keeping its input.
  ASSERT_TRUE(content::ExecJs(
      live, "document.getElementById('f').value = 'half-typed'"));
  TabStripModel* first = browser()->tab_strip_model();
  TabStripModel* second = work[1]->tab_strip_model();
  base::ElapsedTimer moving;
  std::unique_ptr<content::WebContents> taken =
      first->DiscardWebContents(live, Placeholder(live));
  std::unique_ptr<content::WebContents> left = second->DiscardWebContents(
      second->GetWebContentsAt(second->count() - 1), std::move(taken));
  Report(base::StringPrintf("moving a live page between windows: %.2f ms",
                            moving.Elapsed().InMillisecondsF()));
  EXPECT_EQ("half-typed",
            content::EvalJs(second->GetWebContentsAt(second->count() - 1),
                            "document.getElementById('f').value"));
}

IN_PROC_BROWSER_TEST_F(SharedTabsSpikeBrowserTest, WhatTheRestOfChromiumSees) {
  Profile* profile = browser()->GetProfile();
  content::WebContents* plain = Open(browser(), "/plain");
  content::WebContents* timer = Open(browser(), "/timer");
  Browser* second = CreateBrowser(profile);
  Mirror(browser()->tab_strip_model(), second->tab_strip_model());
  Settle();

  scoped_refptr<const extensions::Extension> extension =
      extensions::ExtensionBuilder("Spike").Build();
  auto query = base::MakeRefCounted<extensions::TabsQueryFunction>();
  query->set_extension(extension);
  const base::ListValue tabs = extensions::api_test_utils::ToList(
      extensions::api_test_utils::RunFunctionAndReturnSingleResult(
          query, "[{}]", profile));
  Report(base::StringPrintf(
      "tabs.query({}) lists %zu tabs; the first window has %d and the second "
      "%d, of which %d are copies",
      tabs.size(), browser()->tab_strip_model()->count(),
      second->tab_strip_model()->count(),
      browser()->tab_strip_model()->count()));
  for (const base::Value& tab : tabs) {
    const base::DictValue& fields = tab.GetDict();
    const std::string* url = fields.FindString("url");
    const std::string* status = fields.FindString("status");
    Report(base::StringPrintf(
        "  window %d: %s, status %s, discarded %s", *fields.FindInt("windowId"),
        url ? url->c_str() : "(no url)", status ? status->c_str() : "?",
        fields.FindBool("discarded").value_or(false) ? "yes" : "no"));
  }

  Report("Memory Saver on a background tab: " + MemorySaverSays(plain));
  WaitUntilIdleAndRetitled(timer);
  Report("Memory Saver on a background tab whose title counts: " +
         MemorySaverSays(timer));
  Report("Memory Saver on a copy: " +
         MemorySaverSays(second->tab_strip_model()->GetWebContentsAt(
             second->tab_strip_model()->count() - 1)));

  // A page held in no window, as PinPages holds a pin's live page. It counts
  // while selected, so only its titles after it leaves the window are seen.
  content::WebContents* shown = Open(browser(), "/timer", /*select=*/true);
  WaitUntilIdleAndRetitled(shown);
  Report("Memory Saver on the selected tab whose title counts: " +
         MemorySaverSays(shown));
  std::unique_ptr<content::WebContents> held =
      browser()->tab_strip_model()->DetachWebContentsAtForInsertion(
          browser()->tab_strip_model()->GetIndexOfWebContents(shown));
  WaitUntilIdleAndRetitled(held.get());
  Report("Memory Saver on that page once in no window: " +
         MemorySaverSays(held.get()));

  sessions::TabRestoreService* restore =
      TabRestoreServiceFactory::GetForProfile(profile);
  CloseBrowserSynchronously(second);
  if (restore->entries().empty()) {
    Report("recently closed: nothing for the closed window");
  } else {
    const sessions::tab_restore::Entry& entry = *restore->entries().front();
    Report(base::StringPrintf(
        "recently closed: a %s entry with %zu tabs for the closed window",
        entry.type == sessions::tab_restore::WINDOW ? "window" : "other",
        entry.type == sessions::tab_restore::WINDOW
            ? static_cast<const sessions::tab_restore::Window&>(entry)
                  .tabs.size()
            : 0u));
  }
}

IN_PROC_BROWSER_TEST_F(SharedTabsSpikeBrowserTest, ATaskStaysInStep) {
  Profile* profile = browser()->GetProfile();
  AddRestoredTabs(browser(), 5, "task");
  TabStripModel* first = browser()->tab_strip_model();
  first->ActivateTabAt(1);
  first->CloseWebContentsAt(0, TabCloseTypes::CLOSE_NONE);
  Browser* window = CreateBrowser(profile);
  TabStripModel* second = window->tab_strip_model();
  Mirror(first, second);
  second->ActivateTabAt(1);
  second->CloseWebContentsAt(0, TabCloseTypes::CLOSE_NONE);
  TaskMirror mirror(first, second);
  tab_groups::TabGroupSyncService* saved =
      tab_groups::TabGroupSyncServiceFactory::GetForProfile(profile);
  const auto step = [&](const std::string& what) {
    mirror.Resync();
    Report(what + ": " + mirror.Differences() +
           base::StringPrintf(", %zu saved tab groups",
                              saved ? saved->ReadAllGroups().size() : 0u));
  };

  const TabGroupId task = first->AddToNewGroup({1, 2});
  first->ChangeTabGroupVisuals(
      task, TabGroupVisualData(u"Trip", tab_groups::TabGroupColorId::kBlue));
  step("a new task named Trip");

  second->ActivateTabAt(1);
  first->ChangeTabGroupVisuals(
      task, TabGroupVisualData(u"Trip", tab_groups::TabGroupColorId::kBlue,
                               /*is_collapsed=*/true));
  step("collapsed while the second window shows a tab in it");
  Report(base::StringPrintf("the second window's selected tab is %s",
                            second->GetTabGroupForTab(second->active_index())
                                ? "still in the collapsed task"
                                : "outside it now"));

  first->ChangeTabGroupVisuals(
      task, TabGroupVisualData(u"Lisbon", tab_groups::TabGroupColorId::kGreen));
  step("renamed, recolored, and expanded");

  first->AddToExistingGroup({4}, task);
  step("a tab joined it");

  first->MoveGroupTo(task, 0);
  step("moved to the top");

  first->MoveWebContentsAt(0, 2, /*select_after_move=*/false);
  step("a tab reordered inside it");

  first->RemoveFromGroup({2});
  step("a tab left it");

  first->RemoveFromGroup({0, 1});
  step("its last tabs left it");
}

}  // namespace
}  // namespace uw
