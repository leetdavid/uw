// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/terminal/close_warning.h"

#include <memory>
#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "uw/browser/ui/vector_icons/vector_icons.h"
#include "uw/browser/ui/views/tab_organization/key_hint.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

bool g_ask_in_tests = false;

constexpr int kListRadius = 10;
constexpr int kRowHeight = 34;
constexpr int kIconSize = 16;

std::vector<content::WebContents*> PagesOf(BrowserWindowInterface* window) {
  std::vector<content::WebContents*> pages;
  TabStripModel* tabs = window->GetTabStripModel();
  for (int i = 0; i < tabs->count(); ++i) {
    pages.push_back(tabs->GetWebContentsAt(i));
  }
  return pages;
}

std::u16string Count(size_t count,
                     std::u16string_view one,
                     std::u16string_view many) {
  return count == 1 ? std::u16string(one)
                    : base::StrCat({base::NumberToString16(count), many});
}

}  // namespace

CloseWarning::CloseWarning(BrowserWindowInterface* window)
    : CloseGuard(window->GetUnownedUserDataHost()), window_(window) {}

CloseWarning::~CloseWarning() {
  if (done_) {
    std::move(done_).Run(false);
  }
}

// static
void CloseWarning::AskInTestsForTesting(bool ask) {
  g_ask_in_tests = ask;
}

// static
views::Widget* CloseWarning::DialogForTesting(BrowserWindowInterface* window) {
  auto* warning = static_cast<CloseWarning*>(
      CloseGuard::Get(window->GetUnownedUserDataHost()));
  return warning ? warning->dialog_.get() : nullptr;
}

bool CloseWarning::HoldTabs(const std::vector<content::WebContents*>& pages,
                            TabsDone done) {
  if (releasing_ || !CanAsk() || !HasTerminal(pages)) {
    return false;
  }
  if (asking_) {
    return true;
  }
  std::vector<base::WeakPtr<content::WebContents>> held;
  for (content::WebContents* page : pages) {
    held.push_back(page->GetWeakPtr());
  }
  Ask(pages.size() == 1 ? Closing::kTab : Closing::kTabs, pages.size(), pages,
      std::nullopt,
      base::BindOnce(
          [](std::vector<base::WeakPtr<content::WebContents>> held,
             TabsDone done, bool close) {
            std::vector<content::WebContents*> open;
            for (const base::WeakPtr<content::WebContents>& page : held) {
              if (page) {
                open.push_back(page.get());
              }
            }
            std::move(done).Run(close, std::move(open));
          },
          std::move(held), std::move(done)));
  return true;
}

bool CloseWarning::HoldTask(const tab_groups::TabGroupId& task,
                            base::OnceClosure close) {
  TabStripModel* tabs = window_->GetTabStripModel();
  if (releasing_ || !CanAsk() || !tabs->group_model() ||
      !tabs->group_model()->ContainsTabGroup(task)) {
    return false;
  }
  std::vector<content::WebContents*> pages;
  for (int index :
       tabs->group_model()->GetTabGroup(task)->ListTabs().ToIntVector()) {
    pages.push_back(tabs->GetWebContentsAt(index));
  }
  if (!HasTerminal(pages)) {
    return false;
  }
  if (asking_) {
    return true;
  }
  Ask(Closing::kTask, pages.size(), pages, task,
      base::BindOnce(
          [](base::OnceClosure close, bool agreed) {
            if (agreed) {
              std::move(close).Run();
            }
          },
          std::move(close)));
  return true;
}

bool CloseWarning::HoldWindow() {
  if (std::exchange(window_agreed_, false) || !CanAsk() ||
      browser_shutdown::IsTryingToQuit() ||
      WorkspaceWindow::IsHidden(window_)) {
    return false;
  }
  // The hidden windows of the window's other workspaces close with it.
  std::vector<content::WebContents*> pages = PagesOf(window_);
  if (WorkspaceWindow* self = WorkspaceWindow::From(window_)) {
    for (WorkspaceWindow* partner : self->Partners()) {
      std::vector<content::WebContents*> more = PagesOf(partner->window());
      pages.insert(pages.end(), more.begin(), more.end());
    }
  }
  if (!HasTerminal(pages)) {
    return false;
  }
  if (asking_) {
    return true;
  }
  Ask(Closing::kWindow, pages.size(), pages, std::nullopt,
      base::BindOnce(
          [](base::WeakPtr<CloseWarning> self, bool agreed) {
            if (self && agreed) {
              self->window_agreed_ = true;
              chrome::CloseWindow(self->window_);
            }
          },
          weak_factory_.GetWeakPtr()));
  return true;
}

bool CloseWarning::HoldQuit(base::OnceCallback<void(bool quit)> done) {
  if (!CanAsk()) {
    return false;
  }
  std::vector<content::WebContents*> pages;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&pages](BrowserWindowInterface* window) {
        if (window->GetType() == BrowserWindowInterface::TYPE_NORMAL) {
          std::vector<content::WebContents*> more = PagesOf(window);
          pages.insert(pages.end(), more.begin(), more.end());
        }
        return true;
      });
  if (!HasTerminal(pages)) {
    return false;
  }
  // Quitting waits for an answer, so one asked during another question is a
  // no.
  if (asking_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(done), false));
    return true;
  }
  Ask(Closing::kQuit, pages.size(), pages, std::nullopt, std::move(done));
  return true;
}

bool CloseWarning::CanAsk() const {
  return g_ask_in_tests ||
         !base::CommandLine::ForCurrentProcess()->HasSwitch("test-type");
}

void CloseWarning::Ask(Closing closing,
                       size_t count,
                       const std::vector<content::WebContents*>& pages,
                       std::optional<tab_groups::TabGroupId> task,
                       base::OnceCallback<void(bool)> done) {
  asking_ = true;
  closing_ = closing;
  count_ = count;
  task_ = task;
  done_ = std::move(done);
  FindBusyTerminals(
      pages, base::BindOnce(&CloseWarning::Show, weak_factory_.GetWeakPtr()));
}

void CloseWarning::Show(std::vector<BusyTerminal> busy) {
  if (busy.empty()) {
    Finish(true);
    return;
  }
  // The task's rows stay tinted while it asks, as under the × of its row.
  if (task_) {
    SetTaskCloseHint(true, window_, *task_, true);
  }

  const bool one = busy.size() == 1;
  const std::u16string running =
      Count(busy.size(), u"A program is", u" programs are");
  const std::u16string it = one ? u"it" : u"them";
  std::u16string title;
  std::u16string text;
  std::u16string button;
  switch (closing_) {
    case Closing::kTab:
      title = u"Close this tab?";
      text =
          u"A program is still running in this terminal. Closing the tab "
          u"stops it.";
      button = u"Close Tab";
      break;
    case Closing::kTabs:
      title =
          base::StrCat({u"Close ", base::NumberToString16(count_), u" tabs?"});
      text = base::StrCat({running, u" still running in ",
                           one ? u"one of them" : u"them",
                           u". Closing the tabs stops ", it, u"."});
      button = u"Close Tabs";
      break;
    case Closing::kTask:
      title = u"Close this task?";
      text = base::StrCat({running, u" still running in its ",
                           one ? u"terminal" : u"terminals",
                           u". Closing the task stops ", it, u"."});
      button = u"Close Task";
      break;
    case Closing::kWindow:
      title = u"Close this window?";
      text = base::StrCat({running, u" still running in its ",
                           one ? u"terminal" : u"terminals",
                           u". Closing the window stops ", it, u"."});
      button = u"Close Window";
      break;
    case Closing::kQuit:
      title = u"Quit uw?";
      text =
          base::StrCat({running, u" still running. Quitting stops ", it, u"."});
      button = u"Quit uw";
      break;
  }

  std::unique_ptr<ui::DialogModel> model =
      ui::DialogModel::Builder()
          .SetTitle(title)
          .AddParagraph(ui::DialogModelLabel(text))
          .AddCustomField(
              std::make_unique<views::BubbleDialogModelHost::CustomView>(
                  List(busy), views::BubbleDialogModelHost::FieldType::kText))
          .AddOkButton(base::BindOnce(&CloseWarning::Answered,
                                      weak_factory_.GetWeakPtr(), true),
                       ui::DialogModel::Button::Params().SetLabel(button))
          .AddCancelButton(
              base::BindOnce(&CloseWarning::Answered,
                             weak_factory_.GetWeakPtr(), false),
              ui::DialogModel::Button::Params().SetLabel(u"Cancel").SetStyle(
                  ui::ButtonStyle::kDefault))
          .SetDialogDestroyingCallback(base::BindOnce(
              &CloseWarning::Answered, weak_factory_.GetWeakPtr(), false))
          .Build();
  dialog_ = constrained_window::ShowBrowserModal(
      std::move(model), window_->GetWindow()->GetNativeWindow());
  views::MdTextButton* close =
      dialog_->widget_delegate()->AsDialogDelegate()->GetOkButton();
  close->SetBgColorIdOverride(ui::kColorSysError);
  close->SetEnabledTextColors(ui::kColorSysOnError);
}

std::unique_ptr<views::View> CloseWarning::List(
    const std::vector<BusyTerminal>& busy) {
  auto list = std::make_unique<views::BoxLayoutView>();
  list->SetOrientation(views::BoxLayout::Orientation::kVertical);
  list->SetInsideBorderInsets(gfx::Insets(3));
  list->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysNeutralContainer, kListRadius));
  for (const BusyTerminal& terminal : busy) {
    auto* row = list->AddChildView(std::make_unique<views::BoxLayoutView>());
    row->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    row->SetMinimumCrossAxisSize(kRowHeight);
    row->SetInsideBorderInsets(gfx::Insets::VH(0, 10));
    row->SetBetweenChildSpacing(10);
    row->AddChildView(
        std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
            kTerminalIcon, ui::kColorSysOnSurfaceSubtle, kIconSize)));
    auto* name = row->AddChildView(std::make_unique<views::Label>(
        terminal.page->GetTitle(), views::style::CONTEXT_DIALOG_BODY_TEXT,
        views::style::STYLE_PRIMARY));
    name->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    name->SetElideBehavior(gfx::ELIDE_TAIL);
    const std::u16string place = PlaceOf(terminal.page.get());
    if (!place.empty()) {
      row->AddChildView(std::make_unique<views::Label>(
          place, views::style::CONTEXT_DIALOG_BODY_TEXT,
          views::style::STYLE_SECONDARY));
    }
    row->SetFlexForView(row->AddChildView(std::make_unique<views::View>()), 1);
    auto* program = row->AddChildView(std::make_unique<views::Label>(
        base::UTF8ToUTF16(terminal.program),
        views::style::CONTEXT_DIALOG_BODY_TEXT, views::style::STYLE_SECONDARY));
    program->SetFontList(gfx::FontList("Menlo, 12px"));
  }
  return list;
}

std::u16string CloseWarning::PlaceOf(content::WebContents* page) const {
  tabs::TabInterface* tab = tabs::TabInterface::GetFromContents(page);
  BrowserWindowInterface* window =
      tab ? tab->GetBrowserWindowInterface() : nullptr;
  if (!window) {
    return std::u16string();
  }
  if (closing_ == Closing::kWindow) {
    const std::optional<tab_groups::TabGroupId> task = tab->GetGroup();
    if (!task) {
      return u"Scratchpad";
    }
    return window->GetTabStripModel()
        ->group_model()
        ->GetTabGroup(*task)
        ->visual_data()
        ->title();
  }
  if (closing_ == Closing::kQuit) {
    const WorkspaceWindow* shown = WorkspaceWindow::From(window);
    const WorkspaceList::Workspace* workspace =
        shown ? WorkspaceList::Get().Find(shown->workspace()) : nullptr;
    if (workspace) {
      return base::UTF8ToUTF16(
          base::StrCat({workspace->icon, " ", workspace->name}));
    }
  }
  return std::u16string();
}

void CloseWarning::Answered(bool close) {
  if (!dialog_) {
    return;
  }
  dialog_ = nullptr;
  // The dialog is still closing.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&CloseWarning::Finish, weak_factory_.GetWeakPtr(), close));
}

void CloseWarning::Finish(bool close) {
  if (!asking_) {
    return;
  }
  asking_ = false;
  if (task_) {
    SetTaskCloseHint(true, window_, *task_, false);
    task_.reset();
  }
  // The close may end this window, and this with it.
  base::WeakPtr<CloseWarning> self = weak_factory_.GetWeakPtr();
  releasing_ = close;
  std::move(done_).Run(close);
  if (self) {
    releasing_ = false;
  }
}

}  // namespace uw
