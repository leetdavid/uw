// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/mouse_actions.h"

#import <AppKit/AppKit.h>

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/apple/scoped_objc_class_swizzler.h"
#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/tab/tab_close_button.h"
#include "components/prefs/pref_service.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest_mac.h"
#include "ui/base/hit_test.h"
#include "ui/compositor/layer.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#import "ui/events/test/cocoa_test_event_utils.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/command_bar/command_bar.h"
#include "uw/browser/ui/views/tab_organization/key_hint.h"
#include "uw/browser/ui/views/tab_organization/modifier_keys.h"
#include "uw/browser/ui/views/tab_organization/task_name_field.h"

// The event macOS is handing out, as NSApp.currentEvent reports it.
static NSEvent* g_current_event = nil;

@interface NSApplication (UwMouseActionsTesting)
- (NSEvent*)uw_currentEvent;
@end

@implementation NSApplication (UwMouseActionsTesting)
- (NSEvent*)uw_currentEvent {
  return g_current_event;
}
@end

namespace uw {
namespace {

class MouseActionsBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  // macOS does nothing on a double-click it gets, so a click that wrongly
  // reached it shows as a missing tab rather than a zoomed window.
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
    arguments_ = [defaults volatileDomainForName:NSArgumentDomain];
    NSMutableDictionary* arguments = arguments_
                                         ? [arguments_ mutableCopy]
                                         : [NSMutableDictionary dictionary];
    arguments[@"AppleActionOnDoubleClick"] = @"None";
    SetArguments(arguments);
  }

  void TearDownOnMainThread() override {
    ModifierKeys::Get().SetForTesting(false, false);
    SetArguments(arguments_);
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  void SetArguments(NSDictionary* arguments) {
    NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
    [defaults removeVolatileDomainForName:NSArgumentDomain];
    if (arguments) {
      [defaults setVolatileDomain:arguments forName:NSArgumentDomain];
    }
  }

  // `a` with `b` under it and `c` under `b`, and `d` on its own.
  struct Tree {
    raw_ptr<tabs::TabInterface> a;
    raw_ptr<tabs::TabInterface> b;
    raw_ptr<tabs::TabInterface> c;
    raw_ptr<tabs::TabInterface> d;
  };

  TabOrganizationController* controller() {
    return TabOrganizationController::From(browser());
  }
  TabStripModel* tabs() { return browser()->tab_strip_model(); }
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }
  CommandBar* bar() { return CommandBar::From(browser()); }

  void Settle() { base::RunLoop().RunUntilIdle(); }

  tabs::TabInterface* OpenLinkFrom(tabs::TabInterface* source) {
    NavigateParams params(browser(), GURL("about:blank"),
                          ui::PAGE_TRANSITION_LINK);
    params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    params.source_contents = source->GetContents();
    Navigate(&params);
    Settle();
    return tabs()->GetTabForWebContents(params.navigated_or_inserted_contents);
  }

  Tree OpenTree() {
    Tree tree;
    tree.a = tabs()->GetTabAtIndex(0);
    tree.b = OpenLinkFrom(tree.a);
    tree.c = OpenLinkFrom(tree.b);
    chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
    Settle();
    tree.d = tabs()->GetActiveTab();
    return tree;
  }

  tab_groups::TabGroupId TaskOf(std::vector<tabs::TabInterface*> pages) {
    std::vector<PageId> ids;
    for (tabs::TabInterface* page : pages) {
      ids.push_back(*controller()->IdFor(page));
    }
    std::optional<tab_groups::TabGroupId> task =
        controller()->MoveToNewTask(ids, u"Plan");
    Settle();
    return *task;
  }

  int Depth(tabs::TabInterface* tab) { return controller()->DepthFor(tab); }

  // The views of class T on screen.
  template <typename T>
  std::vector<T*> Shown() {
    views::Widget* widget =
        BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
    widget->LayoutRootViewIfNecessary();
    std::vector<T*> found;
    const std::function<void(views::View*)> visit = [&](views::View* view) {
      if (!view->IsDrawn()) {
        return;
      }
      if (T* match = views::AsViewClass<T>(view)) {
        found.push_back(match);
      }
      for (views::View* child : view->children()) {
        visit(child);
      }
    };
    visit(widget->GetRootView());
    return found;
  }

  // The close button on the row of `tab`.
  views::Button* CloseButtonOf(tabs::TabInterface* tab) {
    for (TabView* row : Shown<TabView>()) {
      if (row->GetTabInterface() == tab) {
        for (views::View* child : row->children()) {
          if (auto* button = views::AsViewClass<TabCloseButton>(child)) {
            return button;
          }
        }
      }
    }
    return nullptr;
  }

  // The row of `tab`, and the key hint on it.
  TabView* TabRowOf(tabs::TabInterface* tab) {
    for (TabView* row : Shown<TabView>()) {
      if (row->GetTabInterface() == tab) {
        return row;
      }
    }
    return nullptr;
  }
  KeyHint* HintOf(tabs::TabInterface* tab) {
    TabView* row = TabRowOf(tab);
    for (views::View* child : row ? row->children() : views::View::Views()) {
      if (auto* hint = views::AsViewClass<KeyHint>(child)) {
        return hint;
      }
    }
    return nullptr;
  }
  KeyHint::Shown ShownOn(tabs::TabInterface* tab) {
    KeyHint* hint = HintOf(tab);
    return hint ? hint->shown() : KeyHint::Shown::kNone;
  }
  // The opacities of the parts of the row of `tab`, a part without a layer
  // at 1.
  std::set<float> OpacitiesOn(tabs::TabInterface* tab) {
    std::set<float> opacities;
    for (views::View* part : TabRowOf(tab)->children()) {
      if (!views::IsViewClass<KeyHint>(part)) {
        opacities.insert(part->layer() ? part->layer()->opacity() : 1.0f);
      }
    }
    return opacities;
  }
  // The pointer on the close button of `tab`, or on the rest of its row.
  void PointAtClose(tabs::TabInterface* tab) {
    CloseButtonOf(tab)->SetState(views::Button::STATE_HOVERED);
  }
  void PointAtRow(tabs::TabInterface* tab) {
    static_cast<views::View*>(TabRowOf(tab))
        ->OnMouseEntered(ui::MouseEvent(
            ui::EventType::kMouseEntered, gfx::Point(), gfx::Point(),
            ui::EventTimeForNow(), ui::EF_NONE, ui::EF_NONE));
  }
  void HoldOption(bool held) { ModifierKeys::Get().SetForTesting(held, false); }

  // The row of `task`.
  views::View* RowOf(const tab_groups::TabGroupId& task) {
    for (TaskNameField* field : AllOf<TaskNameField>()) {
      if (field->group() == task && field->parent()->IsDrawn()) {
        return field->parent();
      }
    }
    return nullptr;
  }

  // Shown<T>, hidden ones too.
  template <typename T>
  std::vector<T*> AllOf() {
    std::vector<T*> found;
    const std::function<void(views::View*)> visit = [&](views::View* view) {
      if (T* match = views::AsViewClass<T>(view)) {
        found.push_back(match);
      }
      for (views::View* child : view->children()) {
        visit(child);
      }
    };
    visit(BrowserView::GetBrowserViewForBrowser(browser())
              ->GetWidget()
              ->GetRootView());
    return found;
  }

  TaskRowNewTabButton* PlusOn(views::View* row) {
    for (views::View* child : row->children()) {
      if (auto* plus = views::AsViewClass<TaskRowNewTabButton>(child)) {
        return plus;
      }
    }
    return nullptr;
  }

  void OptionClick(views::Button* button) {
    ui::MouseEvent click(ui::EventType::kMouseReleased, gfx::Point(),
                         gfx::Point(), ui::EventTimeForNow(),
                         ui::EF_LEFT_MOUSE_BUTTON | ui::EF_ALT_DOWN,
                         ui::EF_LEFT_MOUSE_BUTTON);
    views::test::ButtonTestApi(button).NotifyClick(click);
  }

  void Click(views::Button* button) {
    ui::MouseEvent click(ui::EventType::kMouseReleased, gfx::Point(),
                         gfx::Point(), ui::EventTimeForNow(),
                         ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
    views::test::ButtonTestApi(button).NotifyClick(click);
  }

  // A middle-click on the middle of `view`, through the window.
  // A middle-click on the middle of `view`, through the window, once the
  // tab strip has stopped moving it.
  void MiddleClick(views::View* view) {
    views::Widget* widget = view->GetWidget();
    gfx::Point point;
    ASSERT_TRUE(base::test::RunUntil([&] {
      widget->LayoutRootViewIfNecessary();
      point = view->GetLocalBounds().CenterPoint();
      views::View::ConvertPointToWidget(view, &point);
      return view->Contains(
          widget->GetRootView()->GetEventHandlerForPoint(point));
    }));
    for (ui::EventType type :
         {ui::EventType::kMousePressed, ui::EventType::kMouseReleased}) {
      ui::MouseEvent event(type, point, point, ui::EventTimeForNow(),
                           ui::EF_MIDDLE_MOUSE_BUTTON,
                           ui::EF_MIDDLE_MOUSE_BUTTON);
      widget->OnMouseEvent(&event);
    }
    Settle();
  }

  // The middle of the empty space below the sidebar's tabs, in the window.
  gfx::Point BelowTheTabs() {
    std::vector<views::View*> space;
    for (views::View* view : Shown<views::View>()) {
      if (std::string_view(view->GetClassName()) ==
          "VerticalTabStripBottomContainer") {
        space.push_back(view);
      }
    }
    CHECK_EQ(space.size(), 1u);
    gfx::Point point = space[0]->GetLocalBounds().CenterPoint();
    views::View::ConvertPointToWidget(space[0], &point);
    return point;
  }

  NSWindow* Window() {
    return browser()->GetWindow()->GetNativeWindow().GetNativeNSWindow();
  }

  // `point` in the window, in macOS's coordinates.
  NSPoint InWindow(const gfx::Point& point) {
    NSView* content = Window().contentView;
    return [content
        convertPoint:NSMakePoint(point.x(), NSHeight(content.frame) - point.y())
              toView:nil];
  }

  // Makes NSApp.currentEvent report `event`, as it does for the event macOS
  // is handing out.
  class ScopedCurrentEvent {
   public:
    explicit ScopedCurrentEvent(NSEvent* event)
        : swizzler_([NSApplication class],
                    @selector(currentEvent),
                    @selector(uw_currentEvent)) {
      g_current_event = event;
    }
    ~ScopedCurrentEvent() { g_current_event = nil; }

   private:
    base::apple::ScopedObjCClassSwizzler swizzler_;
  };

  NSEvent* SecondClick(const gfx::Point& point, NSEventType type) {
    return cocoa_test_event_utils::MouseEventAtPointInWindow(
        InWindow(point), type, Window(), /*clickCount=*/2);
  }

  // The second click of a double-click at `point`, as macOS hands it out.
  // The first click of a pair drags the window. A window off screen never
  // passes a press on to its views, so the press goes to the view macOS's hit
  // test names, which a test checks. `press` false sends only the release:
  // a press macOS keeps would start a window drag, which waits for real mouse
  // events.
  void SecondClickAt(const gfx::Point& point, bool press = true) {
    std::vector<NSEventType> types = {NSEventTypeLeftMouseUp};
    if (press) {
      types.insert(types.begin(), NSEventTypeLeftMouseDown);
    }
    for (NSEventType type : types) {
      NSEvent* event = SecondClick(point, type);
      ScopedCurrentEvent current(event);
      if (type == NSEventTypeLeftMouseDown) {
        [Window().contentView mouseDown:event];
      } else {
        [Window() sendEvent:event];
      }
    }
  }

  // What macOS's hit test finds at `point`: nil for space that drags the
  // window.
  NSView* HitTest(const gfx::Point& point) {
    NSView* content = Window().contentView;
    return [content hitTest:[content.superview convertPoint:InWindow(point)
                                                   fromView:nil]];
  }

  int NonClientComponentAt(const gfx::Point& point) {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->GetWidget()
        ->GetNonClientComponent(point);
  }

  // Waits long enough for posted clicks to be handed out.
  void WaitForClicks() {
    base::RunLoop run;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run.QuitClosure(), base::Milliseconds(500));
    run.Run();
  }

 private:
  NSDictionary* arguments_ = nil;
};

IN_PROC_BROWSER_TEST_F(MouseActionsBrowserTest,
                       OptionClickOnACloseButtonClosesTheBranch) {
  const Tree tree = OpenTree();
  OptionClick(CloseButtonOf(tree.a));
  EXPECT_TRUE(base::test::RunUntil([&] { return tabs()->count() == 1; }));
  EXPECT_EQ(tabs()->GetTabAtIndex(0), tree.d.get());
}

IN_PROC_BROWSER_TEST_F(MouseActionsBrowserTest,
                       OptionClickOnATabWithNothingUnderItClosesIt) {
  const Tree tree = OpenTree();
  OptionClick(CloseButtonOf(tree.c));
  EXPECT_TRUE(base::test::RunUntil([&] { return tabs()->count() == 3; }));
  EXPECT_EQ(Depth(tree.b), 1);
}

IN_PROC_BROWSER_TEST_F(MouseActionsBrowserTest,
                       WithItsSwitchOffOptionClickClosesTheTabAlone) {
  prefs()->SetBoolean(kOptionClickClosesBranchPref, false);
  const Tree tree = OpenTree();
  OptionClick(CloseButtonOf(tree.a));
  EXPECT_TRUE(base::test::RunUntil([&] { return tabs()->count() == 3; }));
  EXPECT_EQ(Depth(tree.b), 0);
  EXPECT_EQ(Depth(tree.c), 1);
}

IN_PROC_BROWSER_TEST_F(MouseActionsBrowserTest,
                       MiddleClickOnATaskRowClosesTheTask) {
  const Tree tree = OpenTree();
  const tab_groups::TabGroupId task = TaskOf({tree.a});
  MiddleClick(RowOf(task));
  EXPECT_TRUE(base::test::RunUntil([&] { return tabs()->count() == 1; }));
  EXPECT_FALSE(tabs()->group_model()->ContainsTabGroup(task));
  EXPECT_EQ(tabs()->GetTabAtIndex(0), tree.d.get());
}

IN_PROC_BROWSER_TEST_F(MouseActionsBrowserTest,
                       MiddleClickOnTheOnlyTaskLeavesABlankTab) {
  const tab_groups::TabGroupId task = TaskOf({tabs()->GetTabAtIndex(0)});
  ASSERT_EQ(tabs()->count(), 1);
  MiddleClick(RowOf(task));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return tabs()->count() == 1 &&
           !tabs()->group_model()->ContainsTabGroup(task);
  }));
  EXPECT_FALSE(tabs()->GetTabAtIndex(0)->GetGroup());
}

IN_PROC_BROWSER_TEST_F(MouseActionsBrowserTest,
                       WithItsSwitchOffMiddleClickLeavesTheTask) {
  prefs()->SetBoolean(kMiddleClickClosesTaskPref, false);
  const Tree tree = OpenTree();
  const tab_groups::TabGroupId task = TaskOf({tree.a});
  MiddleClick(RowOf(task));
  EXPECT_EQ(tabs()->count(), 4);
  EXPECT_TRUE(tabs()->group_model()->ContainsTabGroup(task));
}

IN_PROC_BROWSER_TEST_F(MouseActionsBrowserTest,
                       TheTaskRowPlusShowsWithTheMenuButton) {
  const Tree tree = OpenTree();
  const tab_groups::TabGroupId task = TaskOf({tree.a});
  views::View* row = RowOf(task);
  TaskRowNewTabButton* plus = PlusOn(row);
  ASSERT_TRUE(plus);
  EXPECT_FALSE(plus->GetVisible());

  // A focused row shows its menu button, as the pointer does. Rows take
  // focus once the keyboard can reach them.
  row->GetFocusManager()->SetKeyboardAccessible(true);
  row->RequestFocus();
  ASSERT_TRUE(row->HasFocus());
  EXPECT_TRUE(plus->GetVisible());
  // Right before the menu button.
  EXPECT_EQ(row->children()[row->GetIndexOf(plus).value() + 1]->GetProperty(
                views::kElementIdentifierKey),
            kTabGroupEditorBubbleButtonElementId);
  EXPECT_EQ(plus->GetTooltipText(), u"New tab in task");
  SaveWindowScreenshot(browser(), "task-row-plus");

  prefs()->SetBoolean(kTaskRowNewTabPref, false);
  tree.d->GetContents()->Focus();
  row->RequestFocus();
  EXPECT_FALSE(plus->GetVisible());
}

IN_PROC_BROWSER_TEST_F(MouseActionsBrowserTest,
                       TheTaskRowPlusOpensTheCommandBar) {
  const Tree tree = OpenTree();
  const tab_groups::TabGroupId task = TaskOf({tree.a});
  const int count = tabs()->count();
  Click(PlusOn(RowOf(task)));
  EXPECT_TRUE(bar()->IsShowing());
  EXPECT_EQ(tabs()->count(), count);
}

IN_PROC_BROWSER_TEST_F(MouseActionsBrowserTest,
                       WithoutTheCommandBarThePlusOpensATabAtTheTaskEnd) {
  prefs()->SetBoolean(kCommandBarEnabledPref, false);
  const Tree tree = OpenTree();
  const tab_groups::TabGroupId task = TaskOf({tree.a});
  tabs()->ActivateTabAt(tabs()->GetIndexOfTab(tree.d));
  Settle();

  Click(PlusOn(RowOf(task)));
  Settle();
  tabs::TabInterface* opened = tabs()->GetActiveTab();
  EXPECT_EQ(tabs()->count(), 5);
  EXPECT_EQ(opened->GetGroup(), task);
  EXPECT_EQ(Depth(opened), 0);
  EXPECT_EQ(tabs()->GetIndexOfTab(opened),
            static_cast<int>(
                tabs()->group_model()->GetTabGroup(task)->ListTabs().end()) -
                1);
}

IN_PROC_BROWSER_TEST_F(MouseActionsBrowserTest,
                       ThePlusOnACollapsedTaskOpensTheTask) {
  prefs()->SetBoolean(kCommandBarEnabledPref, false);
  const Tree tree = OpenTree();
  const tab_groups::TabGroupId task = TaskOf({tree.a});
  tabs()->ActivateTabAt(tabs()->GetIndexOfTab(tree.d));
  const tab_groups::TabGroupVisualData* visuals =
      tabs()->group_model()->GetTabGroup(task)->visual_data();
  tabs()->ChangeTabGroupVisuals(
      task, tab_groups::TabGroupVisualData(visuals->title(), visuals->color(),
                                           /*is_collapsed=*/true));
  Settle();

  Click(PlusOn(RowOf(task)));
  Settle();
  EXPECT_FALSE(tabs()->IsGroupCollapsed(task));
  EXPECT_EQ(tabs()->GetActiveTab()->GetGroup(), task);
}

// Option on a parent's close button tints the rows an Option-click would
// close, and names them in the tooltip. See
// docs/plans/keyboard-and-mouse.md#key-hints.
IN_PROC_BROWSER_TEST_F(MouseActionsBrowserTest,
                       OptionOnAParentsCloseButtonTintsItsBranch) {
  const Tree tree = OpenTree();
  PointAtClose(tree.a);
  views::Button* close = CloseButtonOf(tree.a);
  EXPECT_EQ(close->GetTooltipText(),
            u"Close tab\nOption-click to close the 2 tabs under it too");
  EXPECT_EQ(ShownOn(tree.a), KeyHint::Shown::kNone);

  HoldOption(true);
  for (tabs::TabInterface* tab : {tree.a.get(), tree.b.get(), tree.c.get()}) {
    EXPECT_EQ(ShownOn(tab), KeyHint::Shown::kCloses);
  }
  EXPECT_EQ(ShownOn(tree.d), KeyHint::Shown::kNone);
  EXPECT_EQ(close->GetTooltipText(), u"Close this tab and the 2 tabs under it");
  EXPECT_NE(CloseButtonColor(TabRowOf(tree.a), SK_ColorBLACK), SK_ColorBLACK);
  SaveWindowScreenshot(browser(), "key-hint-close");

  // Letting go takes it away at once, with no pointer move.
  HoldOption(false);
  EXPECT_EQ(ShownOn(tree.b), KeyHint::Shown::kNone);
  EXPECT_EQ(CloseButtonColor(TabRowOf(tree.a), SK_ColorBLACK), SK_ColorBLACK);

  // So does switching to another app with Option still down.
  HoldOption(true);
  EXPECT_EQ(ShownOn(tree.b), KeyHint::Shown::kCloses);
  [NSNotificationCenter.defaultCenter
      postNotificationName:NSApplicationDidResignActiveNotification
                    object:NSApp];
  EXPECT_FALSE(ModifierKeys::Get().option());
  EXPECT_EQ(ShownOn(tree.b), KeyHint::Shown::kNone);
}

// A tab with nothing under it, or the switch off, shows no hint.
IN_PROC_BROWSER_TEST_F(MouseActionsBrowserTest,
                       ALoneTabOrTheSwitchOffShowsNoHint) {
  const Tree tree = OpenTree();
  HoldOption(true);
  PointAtClose(tree.d);
  EXPECT_EQ(ShownOn(tree.d), KeyHint::Shown::kNone);
  EXPECT_EQ(CloseButtonOf(tree.d)->GetTooltipText(), u"Close tab");

  prefs()->SetBoolean(kOptionClickClosesBranchPref, false);
  PointAtClose(tree.a);
  EXPECT_EQ(ShownOn(tree.a), KeyHint::Shown::kNone);
  EXPECT_EQ(ShownOn(tree.b), KeyHint::Shown::kNone);
  EXPECT_EQ(CloseButtonOf(tree.a)->GetTooltipText(), u"Close tab");
}

// Option on the rest of a parent's row fades the tabs a drag would leave
// behind, and an Option-drag keeps them faded until the drop.
IN_PROC_BROWSER_TEST_F(MouseActionsBrowserTest,
                       OptionOnAParentsRowFadesTheTabsThatStay) {
  const Tree tree = OpenTree();
  PointAtRow(tree.a);
  HoldOption(true);
  EXPECT_EQ(ShownOn(tree.a), KeyHint::Shown::kNone);
  EXPECT_EQ(ShownOn(tree.b), KeyHint::Shown::kStays);
  EXPECT_EQ(ShownOn(tree.c), KeyHint::Shown::kStays);
  EXPECT_EQ(OpacitiesOn(tree.b), std::set<float>{0.5f});
  EXPECT_EQ(OpacitiesOn(tree.a), std::set<float>{1.0f});
  views::View* row = TabRowOf(tree.a);
  views::View* tooltip =
      row->GetTooltipHandlerForPoint(row->GetLocalBounds().CenterPoint());
  ASSERT_TRUE(tooltip);
  EXPECT_EQ(tooltip->GetRenderedTooltipText({}),
            u"Drag to move this tab alone.\nThe 2 tabs under it stay.");
  SaveWindowScreenshot(browser(), "key-hint-stays");

  HoldOption(false);
  EXPECT_EQ(ShownOn(tree.b), KeyHint::Shown::kNone);
  EXPECT_EQ(HintOf(tree.a)->GetTooltipText(), u"");
  HoldOption(true);

  controller()->BeginTabDrag(/*alone=*/true);
  HoldOption(false);
  EXPECT_EQ(ShownOn(tree.b), KeyHint::Shown::kStays);
  controller()->EndTabDrag();
  Settle();
  EXPECT_EQ(ShownOn(tree.b), KeyHint::Shown::kNone);
  EXPECT_EQ(OpacitiesOn(tree.b), std::set<float>{1.0f});
}

IN_PROC_BROWSER_TEST_F(MouseActionsBrowserTest,
                       ADoubleClickBelowTheTabsOpensTheCommandBar) {
  const gfx::Point below = BelowTheTabs();
  // A click there drags the window.
  EXPECT_EQ(NonClientComponentAt(below), HTCAPTION);
  EXPECT_FALSE(HitTest(below));
  {
    // Its second click is the views'.
    ScopedCurrentEvent current(SecondClick(below, NSEventTypeLeftMouseDown));
    EXPECT_EQ(NonClientComponentAt(below), HTCLIENT);
    EXPECT_NSEQ(HitTest(below), Window().contentView);
  }
  const int count = tabs()->count();

  SecondClickAt(below);
  EXPECT_TRUE(base::test::RunUntil([&] { return bar()->IsShowing(); }));
  EXPECT_EQ(tabs()->count(), count);
}

IN_PROC_BROWSER_TEST_F(MouseActionsBrowserTest,
                       WithoutTheCommandBarADoubleClickBelowTheTabsOpensATab) {
  prefs()->SetBoolean(kCommandBarEnabledPref, false);
  const int count = tabs()->count();
  SecondClickAt(BelowTheTabs());
  EXPECT_TRUE(
      base::test::RunUntil([&] { return tabs()->count() == count + 1; }));
}

IN_PROC_BROWSER_TEST_F(MouseActionsBrowserTest,
                       WithItsSwitchOffADoubleClickBelowTheTabsIsTheWindows) {
  prefs()->SetBoolean(kDoubleClickOpensTabPref, false);
  prefs()->SetBoolean(kCommandBarEnabledPref, false);
  const int count = tabs()->count();
  SecondClickAt(BelowTheTabs(), /*press=*/false);
  WaitForClicks();
  EXPECT_EQ(tabs()->count(), count);
}

}  // namespace
}  // namespace uw
