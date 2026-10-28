// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/tab_organization/page_tree.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <utility>

namespace uw {
namespace {

bool Has(const std::vector<PageId>& ids, const PageId& id) {
  return std::ranges::find(ids, id) != ids.end();
}

bool IsSet(const PageId& id) {
  return !id.value().empty();
}

}  // namespace

PageTree::PageTree() = default;

PageTree::PageTree(std::vector<Page> pages) : pages_(std::move(pages)) {
  Normalize();
}

PageTree::PageTree(const PageTree&) = default;
PageTree& PageTree::operator=(const PageTree&) = default;
PageTree::PageTree(PageTree&&) = default;
PageTree& PageTree::operator=(PageTree&&) = default;
PageTree::~PageTree() = default;

const Page* PageTree::Find(const PageId& id) const {
  auto it = std::ranges::find(pages_, id, &Page::id);
  return it == pages_.end() ? nullptr : &*it;
}

std::vector<PageId> PageTree::BranchOf(const PageId& id) const {
  std::vector<PageId> branch;
  if (!Find(id)) {
    return branch;
  }
  branch.push_back(id);
  for (const Page& page : pages_) {
    if (IsSet(page.parent) && Has(branch, page.parent)) {
      branch.push_back(page.id);
    }
  }
  return branch;
}

int PageTree::DepthOf(const PageId& id) const {
  int depth = 0;
  for (const Page* page = Find(id); page && IsSet(page->parent);
       page = Find(page->parent)) {
    ++depth;
  }
  return depth;
}

PageId PageTree::TopOf(const PageId& id) const {
  const Page* page = Find(id);
  while (page && IsSet(page->parent)) {
    page = Find(page->parent);
  }
  return page ? page->id : PageId();
}

std::vector<std::string> PageTree::Tasks() const {
  std::vector<std::string> tasks;
  for (const Page& page : pages_) {
    if (!page.task.empty() &&
        std::ranges::find(tasks, page.task) == tasks.end()) {
      tasks.push_back(page.task);
    }
  }
  return tasks;
}

const Page* PageTree::PreviousSibling(const PageId& id) const {
  const Page* page = Find(id);
  const Page* previous = nullptr;
  for (const Page& other : pages_) {
    if (!page || &other == page) {
      break;
    }
    if (other.task == page->task && other.parent == page->parent) {
      previous = &other;
    }
  }
  return page ? previous : nullptr;
}

const Page* PageTree::NextSibling(const PageId& id) const {
  const Page* page = Find(id);
  bool after = false;
  for (const Page& other : pages_) {
    if (&other == page) {
      after = true;
    } else if (after && other.task == page->task &&
               other.parent == page->parent) {
      return &other;
    }
  }
  return nullptr;
}

void PageTree::Adopt(const std::vector<Page>& strip,
                     const std::set<PageId>& moved,
                     const Drop& drop) {
  const PageTree earlier = *this;
  const PageTree& before = drop.before ? *drop.before : earlier;
  const auto dropped = [&](const PageId& id) {
    return moved.contains(id) || !earlier.Find(id);
  };
  const auto ancestors = [&](const PageId& id) {
    const PageTree& tree = dropped(id) ? before : earlier;
    std::vector<PageId> chain;
    for (const Page* page = tree.Find(id); page && IsSet(page->parent);
         page = tree.Find(page->parent)) {
      chain.push_back(page->parent);
    }
    return chain;
  };

  // A dropped run between two pages of a collapsed branch moves after that
  // branch, where its rows show.
  std::vector<Page> order = strip;
  for (size_t i = 1; i < order.size(); ++i) {
    if (!dropped(order[i].id) || dropped(order[i - 1].id)) {
      continue;
    }
    size_t end = i;
    while (end < order.size() && dropped(order[end].id)) {
      ++end;
    }
    if (end == order.size()) {
      break;
    }
    std::vector<PageId> above = ancestors(order[i - 1].id);
    above.insert(above.begin(), order[i - 1].id);
    PageId hider;
    for (const PageId& id : above) {
      if (drop.collapsed.contains(id) && Has(ancestors(order[end].id), id)) {
        hider = id;
      }
    }
    if (!IsSet(hider)) {
      continue;
    }
    size_t after = end;
    while (after < order.size() && !dropped(order[after].id) &&
           Has(ancestors(order[after].id), hider)) {
      ++after;
    }
    std::rotate(order.begin() + i, order.begin() + end, order.begin() + after);
    i = after - 1;
  }

  std::map<PageId, std::string> task_of;
  for (const Page& entry : order) {
    task_of[entry.id] = entry.task;
  }
  // The ancestor chain of the last page placed, outermost first.
  std::vector<PageId> open;
  const auto is_open_in = [&](const PageId& id, const std::string& task) {
    return Has(open, id) && task_of[id] == task;
  };
  const auto nearest_open_ancestor = [&](const Page& entry) {
    for (const PageId& ancestor : ancestors(entry.id)) {
      if (is_open_in(ancestor, entry.task)) {
        return ancestor;
      }
    }
    return PageId();
  };
  const auto level_under = [&](const PageId& parent) {
    if (!IsSet(parent)) {
      return 0;
    }
    return static_cast<int>(std::ranges::find(open, parent) - open.begin()) + 1;
  };
  // The parent of the first page of a dropped run.
  const auto place = [&](size_t i) {
    const Page& entry = order[i];
    std::vector<PageId> chain;
    if (!open.empty() && task_of[open.back()] == entry.task) {
      chain = open;
    }
    int low = 0;
    for (size_t j = i + 1; j < order.size(); ++j) {
      if (dropped(order[j].id)) {
        continue;
      }
      if (order[j].task == entry.task) {
        low = level_under(nearest_open_ancestor(order[j]));
      }
      break;
    }
    int high = static_cast<int>(chain.size());
    for (size_t level = 0; level < chain.size(); ++level) {
      if (drop.collapsed.contains(chain[level])) {
        high = static_cast<int>(level);
        break;
      }
    }
    int height = 0;
    for (const PageId& id : before.BranchOf(entry.id)) {
      if (dropped(id)) {
        height =
            std::max(height, before.DepthOf(id) - before.DepthOf(entry.id));
      }
    }
    high = std::max(low, std::min(high, kMaxShownDepth - height));
    const int kept = std::max(low, level_under(nearest_open_ancestor(entry)));
    const int level = std::clamp(kept + drop.levels, low, high);
    return level == 0 ? PageId() : chain[level - 1];
  };

  std::vector<Page> adopted;
  PageId run_parent;
  std::string run_task;
  bool in_run = false;
  for (size_t i = 0; i < order.size(); ++i) {
    const Page& entry = order[i];
    PageId parent;
    if (!dropped(entry.id)) {
      in_run = false;
      parent = nearest_open_ancestor(entry);
    } else {
      bool carried = false;
      for (const PageId& ancestor : ancestors(entry.id)) {
        if (dropped(ancestor) && is_open_in(ancestor, entry.task)) {
          parent = ancestor;
          carried = true;
          break;
        }
      }
      if (!carried) {
        if (!in_run || run_task != entry.task ||
            (IsSet(run_parent) && !is_open_in(run_parent, entry.task))) {
          run_parent = place(i);
          run_task = entry.task;
          in_run = true;
        }
        parent = run_parent;
      }
    }
    while (!open.empty() && open.back() != parent) {
      open.pop_back();
    }
    open.push_back(entry.id);
    adopted.push_back({entry.id, entry.task, parent});
  }
  pages_ = std::move(adopted);
  Normalize();
}

bool PageTree::OpenBeside(const PageId& id, const PageId& active) {
  const Page* beside = Find(active);
  const std::vector<PageId> above = BranchOf(active);
  if (!Find(id) || !beside || Has(above, id)) {
    return false;
  }
  const std::string task = beside->task;
  const PageId parent = beside->parent;
  std::vector<Page> branch = Detach(id);
  Attach(std::move(branch), task, parent, EndOf(above));
  return true;
}

bool PageTree::OpenUnder(const PageId& id, const PageId& parent) {
  const Page* target = Find(parent);
  if (!Find(id) || !target || Has(BranchOf(id), parent)) {
    return false;
  }
  const std::string task = target->task;
  std::vector<Page> branch = Detach(id);
  Attach(std::move(branch), task, parent, EndOf({parent}));
  return true;
}

bool PageTree::SetParent(const PageId& id, const PageId& parent) {
  const Page* target = Find(parent);
  if (!Find(id) || !target || Has(BranchOf(id), parent)) {
    return false;
  }
  const std::string task = target->task;
  const std::vector<PageId> above = BranchOf(parent);
  std::vector<Page> branch = Detach(id);
  Attach(std::move(branch), task, parent, EndOf(above));
  return true;
}

bool PageTree::MoveToTask(const PageId& id, const std::string& task) {
  if (!Find(id) || (!task.empty() && !HasTask(task))) {
    return false;
  }
  size_t origin = 0;
  std::vector<Page> branch = Detach(id, &origin);
  Attach(std::move(branch), task, PageId(),
         task.empty() || HasTask(task) ? EndOfTask(task) : origin);
  return true;
}

bool PageTree::MoveToNewTask(const PageId& id, const std::string& task) {
  if (!Find(id) || task.empty() || HasTask(task)) {
    return false;
  }
  std::vector<Page> branch = Detach(id);
  Attach(std::move(branch), task, PageId(), pages_.size());
  return true;
}

bool PageTree::Nest(const PageId& id) {
  const Page* previous = PreviousSibling(id);
  if (!previous) {
    return false;
  }
  const std::string task = previous->task;
  const PageId parent = previous->id;
  const std::vector<PageId> above = BranchOf(parent);
  std::vector<Page> branch = Detach(id);
  Attach(std::move(branch), task, parent, EndOf(above));
  return true;
}

bool PageTree::Outdent(const PageId& id) {
  const Page* page = Find(id);
  const Page* parent =
      page && IsSet(page->parent) ? Find(page->parent) : nullptr;
  if (!parent) {
    return false;
  }
  const std::string task = parent->task;
  const PageId grandparent = parent->parent;
  const std::vector<PageId> above = BranchOf(parent->id);
  std::vector<Page> branch = Detach(id);
  Attach(std::move(branch), task, grandparent, EndOf(above));
  return true;
}

bool PageTree::MoveUp(const PageId& id) {
  const Page* previous = PreviousSibling(id);
  if (!previous) {
    return false;
  }
  const PageId above = previous->id;
  const Page page = *Find(id);
  std::vector<Page> branch = Detach(id);
  const size_t index = static_cast<size_t>(
      std::ranges::find(pages_, above, &Page::id) - pages_.begin());
  Attach(std::move(branch), page.task, page.parent, index);
  return true;
}

bool PageTree::MoveDown(const PageId& id) {
  const Page* next = NextSibling(id);
  if (!next) {
    return false;
  }
  const std::vector<PageId> below = BranchOf(next->id);
  const Page page = *Find(id);
  std::vector<Page> branch = Detach(id);
  Attach(std::move(branch), page.task, page.parent, EndOf(below));
  return true;
}

void PageTree::Restore(const PageTree& earlier) {
  const auto surviving = [&](PageId id) {
    while (IsSet(id) && !Find(id)) {
      const Page* gone = earlier.Find(id);
      id = gone ? gone->parent : PageId();
    }
    return id;
  };
  const auto rank_in_earlier = [&earlier](const PageId& id) {
    return std::distance(earlier.pages_.begin(),
                         std::ranges::find(earlier.pages_, id, &Page::id));
  };

  std::vector<std::pair<double, Page>> keyed;
  std::map<PageId, std::string> restored_task;
  double rank = -1;
  for (const Page& page : pages_) {
    Page placed = page;
    if (const Page* before = earlier.Find(page.id)) {
      rank = static_cast<double>(rank_in_earlier(page.id));
      placed.task = before->task;
      placed.parent = surviving(before->parent);
      keyed.emplace_back(rank, placed);
    } else {
      auto parent_task = restored_task.find(page.parent);
      if (parent_task != restored_task.end()) {
        placed.task = parent_task->second;
      } else if (!earlier.HasTask(page.task)) {
        placed.task.clear();
      }
      keyed.emplace_back(rank + 0.5, placed);
    }
    restored_task[placed.id] = placed.task;
  }
  std::ranges::stable_sort(keyed, {}, &std::pair<double, Page>::first);
  pages_.clear();
  for (auto& [key, page] : keyed) {
    pages_.push_back(std::move(page));
  }
  Normalize();
}

bool PageTree::HasTask(const std::string& task) const {
  return !task.empty() &&
         std::ranges::find(pages_, task, &Page::task) != pages_.end();
}

size_t PageTree::EndOfTask(const std::string& task) const {
  size_t end = 0;
  for (size_t i = 0; i < pages_.size(); ++i) {
    if (pages_[i].task == task) {
      end = i + 1;
    }
  }
  return end;
}

size_t PageTree::EndOf(const std::vector<PageId>& ids) const {
  size_t end = 0;
  for (size_t i = 0; i < pages_.size(); ++i) {
    if (Has(ids, pages_[i].id)) {
      end = i + 1;
    }
  }
  return end;
}

std::vector<Page> PageTree::Detach(const PageId& id, size_t* origin) {
  const std::vector<PageId> ids = BranchOf(id);
  std::vector<Page> branch;
  std::vector<Page> rest;
  for (Page& page : pages_) {
    if (Has(ids, page.id)) {
      if (branch.empty() && origin) {
        *origin = rest.size();
      }
      branch.push_back(std::move(page));
    } else {
      rest.push_back(std::move(page));
    }
  }
  pages_ = std::move(rest);
  return branch;
}

void PageTree::Attach(std::vector<Page> branch,
                      const std::string& task,
                      const PageId& parent,
                      size_t index) {
  for (Page& page : branch) {
    page.task = task;
  }
  branch.front().parent = parent;
  pages_.insert(pages_.begin() + std::min(index, pages_.size()),
                std::make_move_iterator(branch.begin()),
                std::make_move_iterator(branch.end()));
  Normalize();
}

void PageTree::Normalize() {
  for (Page& page : pages_) {
    const Page* parent = IsSet(page.parent) ? Find(page.parent) : nullptr;
    if (!parent || parent == &page || parent->task != page.task) {
      page.parent = PageId();
    }
  }
  for (Page& page : pages_) {
    size_t steps = 0;
    for (const Page* up = &page; up && IsSet(up->parent);
         up = Find(up->parent)) {
      if (++steps > pages_.size()) {
        page.parent = PageId();
        break;
      }
    }
  }

  std::vector<Page> ordered;
  ordered.reserve(pages_.size());
  const std::function<void(const std::string&, const PageId&)> emit =
      [&](const std::string& task, const PageId& parent) {
        for (const Page& page : pages_) {
          if (page.task == task && page.parent == parent) {
            ordered.push_back(page);
            emit(task, page.id);
          }
        }
      };
  for (const std::string& task : Tasks()) {
    emit(task, PageId());
  }
  emit(std::string(), PageId());
  pages_ = std::move(ordered);
}

}  // namespace uw
