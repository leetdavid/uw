// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/agent/user_tabs.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/barrier_callback.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "ui/base/base_window.h"
#include "uw/browser/agent/run_window_mark.h"
#include "uw/browser/pins/pin_store.h"
#include "uw/browser/pins/pin_sync.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/terminal/terminal_shell.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"
#include "uw/components/tab_organization/page_tree.h"

namespace uw {
namespace {

constexpr char kClosedComeBack[] =
    "Closed tabs come back from History › Recently Closed.";

// One of the user's tabs, with a tab for it in some window.
struct UserTab {
  ListedTab listed;
  // Its page in the tab tree; empty for a pin.
  PageId page;
  // Which of the profile's trees holds it, or -1 for a pin.
  int tree = -1;
  std::string workspace_icon;
  // Its task's color, as the chat draws the task.
  std::string color;
  tabs::TabHandle tab;
};

base::DictValue Error(std::string_view message) {
  return base::DictValue().Set("error", message);
}

bool Reaches(Profile* profile, BrowserWindowInterface* window) {
  return window->GetProfile() == profile &&
         window->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
         !IsRunWindow(window);
}

std::string_view ColorName(tab_groups::TabGroupColorId color) {
  switch (color) {
    case tab_groups::TabGroupColorId::kBlue:
      return "blue";
    case tab_groups::TabGroupColorId::kRed:
      return "red";
    case tab_groups::TabGroupColorId::kYellow:
      return "yellow";
    case tab_groups::TabGroupColorId::kGreen:
      return "green";
    case tab_groups::TabGroupColorId::kPink:
      return "pink";
    case tab_groups::TabGroupColorId::kPurple:
      return "purple";
    case tab_groups::TabGroupColorId::kCyan:
      return "cyan";
    case tab_groups::TabGroupColorId::kOrange:
      return "orange";
    default:
      return "grey";
  }
}

const tab_groups::TabGroupVisualData* VisualsOf(
    const TabOrganizationController& tree,
    const std::string& task) {
  const std::optional<tab_groups::TabGroupId> group = tree.GroupFor(task);
  TabGroupModel* groups = tree.window()->GetTabStripModel()->group_model();
  return group && groups && groups->ContainsTabGroup(*group)
             ? groups->GetTabGroup(*group)->visual_data()
             : nullptr;
}

void Describe(UserTab& tab, content::WebContents* contents) {
  if (!contents) {
    return;
  }
  tab.listed.title = base::UTF16ToUTF8(contents->GetTitle());
  const GURL& url = contents->GetLastCommittedURL().is_empty()
                        ? contents->GetVisibleURL()
                        : contents->GetLastCommittedURL();
  tab.listed.url = url.spec();
  tab.listed.seen = contents->GetLastActiveTime();
}

// The tree the tab of `tab` is in now, which may have gone since it was
// listed.
TabOrganizationController* TreeOf(const UserTab& tab) {
  tabs::TabInterface* known = tab.tab.Get();
  return known ? TabOrganizationController::From(
                     known->GetBrowserWindowInterface())
               : nullptr;
}

// The profile's pins, as the front window shows them.
void AddPins(Profile* profile,
             const std::set<std::string>& selected,
             std::vector<UserTab>& tabs) {
  const PinStore* store = PinStore::For(profile);
  if (!store) {
    return;
  }
  for (const PinStore::Pin& pin : store->pins()) {
    UserTab tab;
    tab.listed.id = pin.id;
    tab.listed.pinned = true;
    tab.listed.url = pin.home.spec();
    tab.listed.selected = selected.contains(pin.id);
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&](BrowserWindowInterface* window) {
          if (!Reaches(profile, window)) {
            return true;
          }
          TabStripModel* model = window->GetTabStripModel();
          for (int i = 0; i < model->IndexOfFirstNonPinnedTab(); ++i) {
            tabs::TabInterface* each = model->GetTabAtIndex(i);
            const PinStore::Pin* its = PinSync::PinFor(each);
            if (!its || its->id != pin.id) {
              continue;
            }
            if (!tab.tab.Get()) {
              tab.tab = each->GetHandle();
            }
            if (!PinSync::IsPlaceholder(each)) {
              Describe(tab, each->GetContents());
              return false;
            }
          }
          return true;
        });
    tabs.push_back(std::move(tab));
  }
}

// Every tab of `profile`: pins first, then each tree, in the switcher's order
// of workspaces. A tree several windows share is listed once.
std::vector<UserTab> ListTabs(Profile* profile, const TabOwners& owners) {
  std::vector<TabOrganizationController*> trees;
  std::set<PageId> selected;
  std::set<std::string> selected_pins;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* window) {
        TabOrganizationController* tree =
            Reaches(profile, window) ? TabOrganizationController::From(window)
                                     : nullptr;
        if (!tree) {
          return true;
        }
        if (!WorkspaceWindow::IsHidden(window)) {
          if (tabs::TabInterface* active =
                  window->GetTabStripModel()->GetActiveTab()) {
            if (const PinStore::Pin* pin = PinSync::PinFor(active)) {
              selected_pins.insert(pin->id);
            } else if (std::optional<PageId> id = tree->IdFor(active)) {
              selected.insert(*id);
            }
          }
        }
        if (std::ranges::none_of(
                trees, [window](TabOrganizationController* other) {
                  return std::ranges::contains(other->Windows(), window);
                })) {
          trees.push_back(tree);
        }
        return true;
      });
  const WorkspaceList& workspaces = WorkspaceList::Get();
  const auto workspace_of = [](const TabOrganizationController* tree) {
    const WorkspaceWindow* window = WorkspaceWindow::From(tree->window());
    return window ? window->workspace() : std::string();
  };
  std::ranges::stable_sort(trees, {},
                           [&](const TabOrganizationController* tree) {
                             return workspaces.IndexOf(workspace_of(tree));
                           });

  std::map<PageId, int> numbers;
  for (const auto& [number, handle] : owners.agent) {
    tabs::TabInterface* tab = handle.Get();
    TabOrganizationController* tree =
        tab ? TabOrganizationController::From(tab->GetBrowserWindowInterface())
            : nullptr;
    if (std::optional<PageId> id = tree ? tree->IdFor(tab) : std::nullopt) {
      numbers[*id] = number;
    }
  }
  std::optional<PageId> chat;
  if (tabs::TabInterface* tab = owners.chat.Get()) {
    if (TabOrganizationController* tree =
            TabOrganizationController::From(tab->GetBrowserWindowInterface())) {
      chat = tree->IdFor(tab);
    }
  }

  std::vector<UserTab> tabs;
  AddPins(profile, selected_pins, tabs);
  std::map<std::string, int> trees_named;
  for (size_t index = 0; index < trees.size(); ++index) {
    const TabOrganizationController& tree = *trees[index];
    const WorkspaceList::Workspace* workspace =
        workspaces.Find(workspace_of(&tree));
    const std::string name = workspace                   ? workspace->name
                             : profile->IsOffTheRecord() ? "Private"
                                                         : "Workspace";
    const int number = ++trees_named[name];
    for (const Page& page : tree.tree().pages()) {
      UserTab tab;
      tab.page = page.id;
      tab.tree = static_cast<int>(index);
      tab.workspace_icon = workspace ? workspace->icon : std::string();
      tab.listed.id = page.id.value();
      tab.listed.workspace = name;
      tab.listed.tree = number;
      tab.listed.task_id = page.task;
      if (!page.task.empty()) {
        tab.listed.task = base::UTF16ToUTF8(tree.TaskTitle(page.task));
        if (const tab_groups::TabGroupVisualData* visuals =
                VisualsOf(tree, page.task)) {
          tab.color = ColorName(visuals->color());
        }
      }
      tab.listed.depth = tree.tree().DepthOf(page.id);
      tab.listed.parent = page.parent.value();
      tab.listed.selected = selected.contains(page.id);
      tab.listed.kept = tree.IsKept(page.id);
      if (const auto found = numbers.find(page.id); found != numbers.end()) {
        tab.listed.agent_tab = found->second;
      }
      tab.listed.chat = chat == page.id;
      if (tabs::TabInterface* each = tree.TabFor(page.id)) {
        tab.tab = each->GetHandle();
      }
      Describe(tab, tree.PageContents(page.id));
      tabs.push_back(std::move(tab));
    }
  }
  return tabs;
}

// Lists the tabs, then asks each terminal whether it runs a command.
void FindTabs(Profile* profile,
              const TabOwners& owners,
              base::OnceCallback<void(std::vector<UserTab>)> done) {
  std::vector<UserTab> tabs = ListTabs(profile, owners);
  std::vector<std::pair<size_t, TerminalShell*>> terminals;
  for (size_t i = 0; i < tabs.size(); ++i) {
    TabOrganizationController* tree = TreeOf(tabs[i]);
    content::WebContents* contents = tree && !tabs[i].page->empty()
                                         ? tree->PageContents(tabs[i].page)
                                         : nullptr;
    if (TerminalShell* shell =
            contents ? TerminalShell::From(contents) : nullptr) {
      terminals.emplace_back(i, shell);
    }
  }
  const auto answered = base::BarrierCallback<std::pair<size_t, bool>>(
      terminals.size(),
      base::BindOnce(
          [](std::vector<UserTab> tabs,
             base::OnceCallback<void(std::vector<UserTab>)> done,
             std::vector<std::pair<size_t, bool>> busy) {
            for (const auto& [index, running] : busy) {
              tabs[index].listed.busy = running;
            }
            std::move(done).Run(std::move(tabs));
          },
          std::move(tabs), std::move(done)));
  // A terminal that closes before its shell answers counts as idle.
  for (const auto& [index, shell] : terminals) {
    shell->GetActivity(mojo::WrapCallbackWithDefaultInvokeIfNotRun(
        base::BindOnce(
            [](base::RepeatingCallback<void(std::pair<size_t, bool>)> answered,
               size_t index, ShellHost::Activity activity) {
              answered.Run({index, !activity.program.empty()});
            },
            answered, index),
        ShellHost::Activity()));
  }
}

std::vector<ListedTab> Listed(const std::vector<UserTab>& tabs) {
  std::vector<ListedTab> listed;
  for (const UserTab& tab : tabs) {
    listed.push_back(tab.listed);
  }
  return listed;
}

// `index` and the tabs nested under it.
std::vector<size_t> BranchOf(const std::vector<UserTab>& tabs, size_t index) {
  std::vector<size_t> branch = {index};
  for (size_t i = index + 1;
       i < tabs.size() && tabs[i].tree == tabs[index].tree &&
       tabs[i].tree >= 0 && tabs[i].listed.depth > tabs[index].listed.depth;
       ++i) {
    branch.push_back(i);
  }
  return branch;
}

std::string TitleOf(const UserTab& tab) {
  return tab.listed.title.empty() ? tab.listed.url : tab.listed.title;
}

std::string Count(size_t count) {
  return base::StrCat(
      {base::NumberToString(count), count == 1 ? " tab" : " tabs"});
}

base::DictValue TaskValue(const UserTab& tab) {
  return base::DictValue()
      .Set("id", ShortId(tab.listed.task_id))
      .Set("name", tab.listed.task)
      .Set("color", tab.color);
}

// What a call acts on.
struct Plan {
  // The tabs it shows, moves, or closes, in the list's order.
  std::vector<size_t> tabs;
  // The roots of the branches it moves or closes, as named.
  std::vector<size_t> roots;
  // A tab of the task it names.
  std::optional<size_t> task;
  std::string new_task;
  std::string name;
};

std::string TextOf(const base::DictValue& args, std::string_view key) {
  const std::string* text = args.FindString(key);
  return text ? std::string(base::TrimWhitespaceASCII(*text, base::TRIM_ALL))
              : std::string();
}

base::expected<Plan, base::DictValue> PlanOf(std::string_view tool,
                                             const base::DictValue& args,
                                             const std::vector<UserTab>& tabs) {
  const std::vector<ListedTab> listed = Listed(tabs);
  const auto refuse = [tool](TabRefusal refusal) {
    return base::unexpected(RefusalResult(tool, refusal));
  };
  Plan plan;
  if (tool == kSelectTool) {
    const base::expected<size_t, TabRefusal> found = FindTab(listed, args);
    if (!found.has_value()) {
      return refuse(found.error());
    }
    plan.tabs = {*found};
    return plan;
  }
  if (tool == kRenameTaskTool || tool == kCloseTaskTool) {
    const base::expected<size_t, TabRefusal> found =
        FindTask(listed, args, "task");
    if (!found.has_value()) {
      return refuse(found.error());
    }
    plan.task = *found;
    if (tool == kRenameTaskTool) {
      plan.name = TextOf(args, "name");
      if (plan.name.empty()) {
        return refuse(
            TabRefusal{"pass the task's new name", tabs[*found].listed.task});
      }
      return plan;
    }
    for (size_t i = 0; i < tabs.size(); ++i) {
      if (tabs[i].tree == tabs[*found].tree &&
          tabs[i].listed.task_id == tabs[*found].listed.task_id) {
        plan.tabs.push_back(i);
      }
    }
  } else {
    const base::expected<std::vector<size_t>, TabRefusal> found =
        FindTabs(listed, args);
    if (!found.has_value()) {
      return refuse(found.error());
    }
    plan.roots = *found;
    std::set<size_t> all;
    for (size_t root : plan.roots) {
      if (tabs[root].listed.pinned) {
        return refuse(TabRefusal{"it's a pinned tab", TitleOf(tabs[root]),
                                 ShortId(tabs[root].listed.id)});
      }
      for (size_t i : BranchOf(tabs, root)) {
        all.insert(i);
      }
    }
    plan.tabs.assign(all.begin(), all.end());
  }
  if (tool == kMoveTool) {
    std::set<int> trees;
    std::set<std::string> workspaces;
    for (size_t root : plan.roots) {
      trees.insert(tabs[root].tree);
      workspaces.insert(tabs[root].listed.workspace);
    }
    if (trees.size() > 1) {
      return refuse(TabRefusal{workspaces.size() > 1
                                   ? "they're in different workspaces"
                                   : "they're in different windows",
                               ""});
    }
    const bool scratchpad = args.FindBool("scratchpad").value_or(false);
    plan.new_task = TextOf(args, "new_task");
    const bool task = !TextOf(args, "task").empty();
    if (scratchpad + !plan.new_task.empty() + task != 1) {
      return refuse(
          TabRefusal{"pass one of task, new_task, or scratchpad", ""});
    }
    if (task) {
      const base::expected<size_t, TabRefusal> found =
          FindTask(listed, args, "task");
      if (!found.has_value()) {
        return refuse(found.error());
      }
      if (tabs[*found].tree != *trees.begin()) {
        return refuse(
            TabRefusal{"it's in another workspace", tabs[*found].listed.task});
      }
      plan.task = *found;
    }
    return plan;
  }
  // Closing.
  for (size_t i : plan.tabs) {
    if (tabs[i].listed.busy) {
      return refuse(TabRefusal{"a command runs there", TitleOf(tabs[i]),
                               ShortId(tabs[i].listed.id)});
    }
    if (tabs[i].listed.chat) {
      return refuse(TabRefusal{"it's this chat's own tab", TitleOf(tabs[i]),
                               ShortId(tabs[i].listed.id)});
    }
  }
  return plan;
}

// Where the tabs of `plan` are, as "in Flights" or "in the Scratchpad", or
// nothing when they are in several places.
std::string WhereOf(const std::vector<UserTab>& tabs, const Plan& plan) {
  std::set<std::pair<int, std::string>> places;
  for (size_t i : plan.tabs) {
    places.insert({tabs[i].tree, tabs[i].listed.task_id});
  }
  if (places.size() != 1) {
    return std::string();
  }
  const UserTab& tab = tabs[plan.tabs.front()];
  return tab.listed.task_id.empty() ? std::string(" in the Scratchpad")
                                    : base::StrCat({" in ", tab.listed.task});
}

TabStep StepOf(std::string_view tool,
               const base::DictValue& args,
               const std::vector<UserTab>& tabs,
               const Plan& plan) {
  TabStep step;
  step.closes = tool == kCloseTool || tool == kCloseTaskTool;
  if (tool == kRenameTaskTool) {
    step.changes_users_tabs = true;
  } else if (tool != kSelectTool) {
    step.changes_users_tabs = std::ranges::any_of(
        plan.tabs, [&tabs](size_t i) { return !tabs[i].listed.agent_tab; });
  }
  const UserTab& first = tabs[plan.task           ? *plan.task
                              : plan.tabs.empty() ? 0
                                                  : plan.tabs.front()];
  step.workspace = first.listed.workspace;
  step.workspace_icon = first.workspace_icon;
  for (size_t i : plan.tabs) {
    step.tabs.push_back(tabs[i].listed);
  }
  if (tool == kCloseTool) {
    step.action =
        base::StrCat({"Close ", Count(plan.tabs.size()), WhereOf(tabs, plan)});
    step.reason = kClosedComeBack;
  } else if (tool == kCloseTaskTool) {
    step.action = base::StrCat({"Close the task ", first.listed.task,
                                " and its ", Count(plan.tabs.size())});
    step.reason = kClosedComeBack;
  } else if (tool == kMoveTool) {
    step.action =
        base::StrCat({"Move ", Count(plan.tabs.size()), " to ",
                      plan.task ? tabs[*plan.task].listed.task
                      : plan.new_task.empty()
                          ? std::string("the Scratchpad")
                          : base::StrCat({"a new task, ", plan.new_task})});
    step.reason = "Ask first asks before every change to your tabs.";
  } else if (tool == kRenameTaskTool) {
    step.task = first.listed.task;
    step.new_name = plan.name;
    step.action =
        base::StrCat({"Rename ", first.listed.task, " to ", plan.name});
    step.reason =
        "The name counts as yours: it gets the name lock, and uw won't rename "
        "the task.";
  }
  return step;
}

base::ListValue Values(const std::vector<UserTab>& tabs,
                       const std::vector<size_t>& which) {
  base::ListValue values;
  for (size_t i : which) {
    values.Append(TabValue(tabs[i].listed));
  }
  return values;
}

// Shows page `id` in `window`'s tab for it, and with `forward`, brings
// the window and uw in front.
void ShowIn(const PageId& id, bool forward, BrowserWindowInterface* window) {
  TabOrganizationController* tree =
      window ? TabOrganizationController::From(window) : nullptr;
  tabs::TabInterface* tab = tree ? tree->TabFor(id) : nullptr;
  if (!tab) {
    return;
  }
  TabStripModel* model = window->GetTabStripModel();
  model->ActivateTabAt(model->GetIndexOfTab(tab));
  if (TabOrganizationController::IsCopy(tab)) {
    tree->BringLivePage(tab);
  }
  if (forward) {
    window->GetWindow()->Show();
    window->GetWindow()->Activate();
  }
}

base::DictValue Select(Profile* profile,
                       const std::vector<UserTab>& tabs,
                       const Plan& plan,
                       bool forward) {
  const UserTab& tab = tabs[plan.tabs.front()];
  base::DictValue result = base::DictValue().Set("tab", TabValue(tab.listed));
  if (tab.listed.pinned) {
    BrowserWindowInterface* front = nullptr;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&](BrowserWindowInterface* window) {
          if (Reaches(profile, window) && !WorkspaceWindow::IsHidden(window)) {
            front = window;
            return false;
          }
          return true;
        });
    TabStripModel* model = front ? front->GetTabStripModel() : nullptr;
    for (int i = 0; model && i < model->IndexOfFirstNonPinnedTab(); ++i) {
      tabs::TabInterface* each = model->GetTabAtIndex(i);
      const PinStore::Pin* pin = PinSync::PinFor(each);
      if (pin && pin->id == tab.listed.id) {
        model->ActivateTabAt(i);
        if (PinSync::IsPlaceholder(each)) {
          PinSync::BringLivePage(each);
        }
        if (forward) {
          front->GetWindow()->Show();
          front->GetWindow()->Activate();
        }
        return result;
      }
    }
    return Error("No window shows the pins.");
  }
  TabOrganizationController* tree = TreeOf(tab);
  if (!tree) {
    return Error("That tab has closed.");
  }
  // The front window that shows its workspace, or else a hidden window's
  // partner, which switches to it.
  BrowserWindowInterface* shown = nullptr;
  BrowserWindowInterface* hidden = nullptr;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* window) {
        if (!std::ranges::contains(tree->Windows(), window)) {
          return true;
        }
        if (!WorkspaceWindow::IsHidden(window)) {
          shown = window;
          return false;
        }
        if (!hidden) {
          hidden = window;
        }
        return true;
      });
  if (shown) {
    ShowIn(tab.page, forward, shown);
    return result;
  }
  WorkspaceWindow* workspace = hidden ? WorkspaceWindow::From(hidden) : nullptr;
  WorkspaceWindow* partner = nullptr;
  for (WorkspaceWindow* each :
       workspace ? workspace->Partners() : std::vector<WorkspaceWindow*>()) {
    if (!WorkspaceWindow::IsHidden(each->window())) {
      partner = each;
    }
  }
  if (!partner ||
      !partner->Switch(workspace->workspace(),
                       base::BindOnce(&ShowIn, tab.page, forward))) {
    return Error("uw can't switch to that workspace now.");
  }
  return result;
}

base::DictValue Act(Profile* profile,
                    std::string_view tool,
                    const std::vector<UserTab>& tabs,
                    const Plan& plan,
                    bool forward) {
  if (tool == kSelectTool) {
    return Select(profile, tabs, plan, forward);
  }
  const UserTab& first = tabs[plan.task           ? *plan.task
                              : plan.tabs.empty() ? 0
                                                  : plan.tabs.front()];
  TabOrganizationController* tree = TreeOf(first);
  if (!tree) {
    return Error("Those tabs have closed.");
  }
  if (tool == kRenameTaskTool || tool == kCloseTaskTool) {
    const std::optional<tab_groups::TabGroupId> group =
        tree->GroupFor(first.listed.task_id);
    const tab_groups::TabGroupVisualData* visuals =
        VisualsOf(*tree, first.listed.task_id);
    if (!group || !visuals) {
      return Error("That task has closed.");
    }
    if (tool == kCloseTaskTool) {
      tree->CloseTask(*group);
      return base::DictValue()
          .Set("task", TaskValue(first))
          .Set("closed", Values(tabs, plan.tabs));
    }
    tree->window()->GetTabStripModel()->ChangeTabGroupVisuals(
        *group, tab_groups::TabGroupVisualData(base::UTF8ToUTF16(plan.name),
                                               visuals->color(),
                                               visuals->is_collapsed()));
    UserTab renamed = first;
    renamed.listed.task = plan.name;
    return base::DictValue()
        .Set("task", TaskValue(renamed))
        .Set("was", first.listed.task);
  }
  std::vector<PageId> roots;
  for (size_t root : plan.roots) {
    roots.push_back(tabs[root].page);
  }
  if (tool == kCloseTool) {
    // Each tree closes its own tabs.
    std::map<TabOrganizationController*, std::vector<PageId>> by_tree;
    for (size_t root : plan.roots) {
      if (TabOrganizationController* its = TreeOf(tabs[root])) {
        by_tree[its].push_back(tabs[root].page);
      }
    }
    for (auto& [its, ids] : by_tree) {
      its->CloseBranches(ids);
    }
    base::DictValue result =
        base::DictValue().Set("closed", Values(tabs, plan.tabs));
    if (const std::string where = WhereOf(tabs, plan);
        !where.empty() && !first.listed.task_id.empty()) {
      result.Set("task", TaskValue(first));
    }
    return result;
  }
  base::DictValue result =
      base::DictValue().Set("moved", Values(tabs, plan.tabs));
  if (plan.task) {
    const UserTab& task = tabs[*plan.task];
    const std::optional<tab_groups::TabGroupId> group =
        tree->GroupFor(task.listed.task_id);
    if (!group || !tree->MoveToTask(roots, *group)) {
      return Error("Those tabs could not move to that task.");
    }
    return std::move(result).Set("task", TaskValue(task));
  }
  if (plan.new_task.empty()) {
    if (!tree->MoveToScratchpad(roots)) {
      return Error("Those tabs could not move to the Scratchpad.");
    }
    return std::move(result).Set("scratchpad", true);
  }
  const std::optional<tab_groups::TabGroupId> group =
      tree->MoveToNewTask(roots, base::UTF8ToUTF16(plan.new_task));
  if (!group) {
    return Error("A new task could not start.");
  }
  UserTab started = first;
  started.listed.task_id = tree->KeyOf(*group);
  started.listed.task = plan.new_task;
  if (const tab_groups::TabGroupVisualData* visuals =
          VisualsOf(*tree, started.listed.task_id)) {
    started.color = ColorName(visuals->color());
  }
  return std::move(result).Set("task", TaskValue(started)).Set("new", true);
}

}  // namespace

void CheckTabStep(Profile* profile,
                  std::string_view tool,
                  const base::DictValue& args,
                  const TabOwners& owners,
                  base::OnceCallback<void(TabStep)> done) {
  if (tool == kTabsTool) {
    std::move(done).Run(TabStep());
    return;
  }
  FindTabs(profile, owners,
           base::BindOnce(
               [](std::string tool, base::DictValue args,
                  base::OnceCallback<void(TabStep)> done,
                  std::vector<UserTab> tabs) {
                 base::expected<Plan, base::DictValue> plan =
                     PlanOf(tool, args, tabs);
                 if (!plan.has_value()) {
                   TabStep refused;
                   refused.refused = std::move(plan.error());
                   std::move(done).Run(std::move(refused));
                   return;
                 }
                 std::move(done).Run(StepOf(tool, args, tabs, *plan));
               },
               std::string(tool), args.Clone(), std::move(done)));
}

void ShowTask(Profile* profile, std::string_view task) {
  FindTabs(
      profile, TabOwners(),
      base::BindOnce(
          [](Profile* profile, std::string task, std::vector<UserTab> tabs) {
            const base::expected<size_t, TabRefusal> found = FindTask(
                Listed(tabs), base::DictValue().Set("task", task), "task");
            if (!found.has_value()) {
              return;
            }
            Plan plan;
            plan.tabs = {*found};
            for (size_t i = 0; i < tabs.size(); ++i) {
              if (tabs[i].tree == tabs[*found].tree &&
                  tabs[i].listed.task_id == tabs[*found].listed.task_id &&
                  tabs[i].listed.seen > tabs[plan.tabs.front()].listed.seen) {
                plan.tabs = {i};
              }
            }
            Select(profile, tabs, plan, /*forward=*/false);
          },
          profile, std::string(task)));
}

void ListUserTabs(Profile* profile,
                  base::OnceCallback<void(std::vector<ListedTab>)> done) {
  FindTabs(
      profile, TabOwners(),
      base::BindOnce(
          [](base::OnceCallback<void(std::vector<ListedTab>)> done,
             std::vector<UserTab> tabs) { std::move(done).Run(Listed(tabs)); },
          std::move(done)));
}

void RunTabTool(Profile* profile,
                std::string_view tool,
                const base::DictValue& args,
                const TabOwners& owners,
                base::OnceCallback<void(base::DictValue)> done,
                bool bring_forward) {
  FindTabs(profile, owners,
           base::BindOnce(
               [](Profile* profile, std::string tool, base::DictValue args,
                  bool forward, base::OnceCallback<void(base::DictValue)> done,
                  std::vector<UserTab> tabs) {
                 if (tool == kTabsTool) {
                   const TabFilter filter = TabFilterFrom(args);
                   std::vector<ListedTab> kept;
                   for (const UserTab& tab : tabs) {
                     if (Matches(tab.listed, filter)) {
                       kept.push_back(tab.listed);
                     }
                   }
                   std::move(done).Run(TabListResult(kept, base::Time::Now()));
                   return;
                 }
                 base::expected<Plan, base::DictValue> plan =
                     PlanOf(tool, args, tabs);
                 std::move(done).Run(
                     plan.has_value() ? Act(profile, tool, tabs, *plan, forward)
                                      : std::move(plan.error()));
               },
               profile, std::string(tool), args.Clone(), bring_forward,
               std::move(done)));
}

}  // namespace uw
