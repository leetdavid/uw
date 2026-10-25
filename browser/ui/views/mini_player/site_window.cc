// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/site_window.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/task/single_thread_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"    // nogncheck
#include "chrome/browser/ui/views/frame/browser_widget.h"  // nogncheck
#include "chrome/browser/ui/views/frame/picture_in_picture_browser_frame_view.h"  // nogncheck
#include "chrome/grit/generated_resources.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/document_picture_in_picture_window_controller.h"
#include "content/public/browser/picture_in_picture_window_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/view.h"
#include "ui/views/view_targeter.h"
#include "ui/views/view_targeter_delegate.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/ui/glass/glass.h"
#include "uw/browser/ui/vector_icons/vector_icons.h"
#include "uw/browser/ui/views/mini_player/mini_player_controls.h"
#include "uw/browser/ui/views/mini_player/mini_player_dock.h"
#include "uw/browser/ui/views/mini_player/mini_player_placement.h"

namespace uw {
namespace {

constexpr int kCornerRadius = 10;
constexpr int kButton = 24;
constexpr int kButtonIcon = 14;
constexpr int kButtonInset = 8;
constexpr int kScrimHeight = 52;
constexpr base::TimeDelta kPromptCheck = base::Milliseconds(500);

std::unique_ptr<SiteWindow>& CurrentSlot() {
  static base::NoDestructor<std::unique_ptr<SiteWindow>> slot;
  return *slot;
}

// Over a docked site's page: a faint ring, as around a docked video, and
// under the pointer close and back to tab over a short scrim, as on a video.
class PageControls : public views::View, public views::ViewTargeterDelegate {
  METADATA_HEADER(PageControls, views::View)

 public:
  PageControls(SiteWindow* site_window, ControlsGlassOf glass) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetEventTargeter(std::make_unique<views::ViewTargeter>(this));
    close_ = AddChildView(std::make_unique<MiniPlayerButton>(
        base::BindRepeating(&SiteWindow::Close, base::Unretained(site_window)),
        kMiniPlayerCloseIcon,
        l10n_util::GetStringUTF16(IDS_PICTURE_IN_PICTURE_CLOSE_CONTROL_TEXT),
        glass));
    back_to_tab_ = AddChildView(std::make_unique<MiniPlayerButton>(
        base::BindRepeating(&SiteWindow::BackToTab,
                            base::Unretained(site_window)),
        kMiniPlayerBackToTabIcon,
        l10n_util::GetStringUTF16(
            IDS_PICTURE_IN_PICTURE_BACK_TO_TAB_CONTROL_TEXT),
        glass));
    shows_back_to_tab_ = site_window->ShowsBackToTab();
    for (MiniPlayerButton* button : {close_.get(), back_to_tab_.get()}) {
      button->SetSizes(kButton, kButtonIcon);
    }
  }

  void Show(bool shown) {
    shown_ = shown;
    close_->SetVisible(shown);
    back_to_tab_->SetVisible(shown && shows_back_to_tab_);
    for (MiniPlayerButton* button : {close_.get(), back_to_tab_.get()}) {
      button->UpdateGlass();
    }
    SchedulePaint();
  }

  bool shown() const { return shown_; }

  // views::View:
  void Layout(PassKey) override {
    close_->SetBounds(kButtonInset, kButtonInset, kButton, kButton);
    back_to_tab_->SetBounds(width() - kButtonInset - kButton, kButtonInset,
                            kButton, kButton);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    // The sidebar's text color, faint.
    const SkColor text =
        GetColorProvider()->GetColor(kColorTabForegroundInactiveFrameActive);
    cc::PaintFlags ring;
    ring.setAntiAlias(true);
    ring.setStyle(cc::PaintFlags::kStroke_Style);
    ring.setStrokeWidth(1);
    ring.setColor(SkColorSetA(text, 0x1F));
    gfx::RectF edge(GetLocalBounds());
    edge.Inset(0.5f);
    canvas->DrawRoundRect(edge, kCornerRadius - 0.5f, ring);
    if (!shown_) {
      return;
    }
    cc::PaintFlags scrim;
    const SkPoint points[] = {SkPoint::Make(0, 0),
                              SkPoint::Make(0, kScrimHeight)};
    const SkColor4f colors[] = {
        SkColor4f::FromColor(SkColorSetA(SK_ColorBLACK, 0x80)),
        SkColors::kTransparent};
    scrim.setShader(cc::PaintShader::MakeLinearGradient(points, colors, nullptr,
                                                        2, SkTileMode::kClamp));
    canvas->DrawRect(gfx::Rect(0, 0, width(), kScrimHeight), scrim);
  }

  // views::ViewTargeterDelegate: the page under it takes every click but the
  // buttons'.
  bool DoesIntersectRect(const views::View* target,
                         const gfx::Rect& rect) const override {
    return shown_ && (close_->bounds().Intersects(rect) ||
                      (back_to_tab_->GetVisible() &&
                       back_to_tab_->bounds().Intersects(rect)));
  }

 private:
  raw_ptr<MiniPlayerButton> close_ = nullptr;
  raw_ptr<MiniPlayerButton> back_to_tab_ = nullptr;
  bool shows_back_to_tab_ = true;
  bool shown_ = false;
};

BEGIN_METADATA(PageControls)
END_METADATA

// A site's page docked in the sidebar: the live page with rounded corners and
// a faint ring, and its controls under the pointer.
class DockedPage : public views::View {
  METADATA_HEADER(DockedPage, views::View)

 public:
  DockedPage(SiteWindow* site_window,
             content::WebContents* page,
             MiniPlayerDock* dock) {
    SetNotifyEnterExitOnChild(true);
    web_view_ = AddChildView(
        std::make_unique<views::WebView>(page->GetBrowserContext()));
    web_view_->SetWebContents(page);
    // The page's own layer is clipped to the rounded corners.
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(kCornerRadius));
    layer()->SetIsFastRoundedCorner(true);
    layer()->SetMasksToBounds(true);
    controls_ = AddChildView(std::make_unique<PageControls>(
        site_window, base::BindRepeating(
                         [](MiniPlayerDock* dock) {
                           return ElementGlassOfWindow(dock->window());
                         },
                         base::Unretained(dock))));
    controls_->Show(false);
  }

  ~DockedPage() override { web_view_->SetWebContents(nullptr); }

  bool controls_shown() const { return controls_->shown(); }

  // views::View:
  void Layout(PassKey) override {
    web_view_->SetBoundsRect(GetLocalBounds());
    controls_->SetBoundsRect(GetLocalBounds());
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    controls_->Show(true);
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    controls_->Show(false);
  }

 private:
  raw_ptr<views::WebView> web_view_ = nullptr;
  raw_ptr<PageControls> controls_ = nullptr;
};

BEGIN_METADATA(DockedPage)
END_METADATA

}  // namespace

// Follows the sites' windows Chromium opens: one at a time, the newest.
class SiteWindowWatcher : public PictureInPictureWindowManager::Observer {
 public:
  static SiteWindowWatcher& Get() {
    static base::NoDestructor<SiteWindowWatcher> watcher;
    return *watcher;
  }

  // PictureInPictureWindowManager::Observer:
  void OnEnterPictureInPicture() override {
    // Chromium puts the page in its window after it says so.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&SiteWindowWatcher::Follow, weak_factory_.GetWeakPtr()));
  }

  // Lets `gone` go, once the task that said so ends.
  void Forget(SiteWindow* gone) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<SiteWindow> gone) {
                         if (gone && CurrentSlot().get() == gone.get()) {
                           CurrentSlot().reset();
                         }
                       },
                       gone->weak_factory_.GetWeakPtr()));
  }

 private:
  friend class base::NoDestructor<SiteWindowWatcher>;

  SiteWindowWatcher() {
    PictureInPictureWindowManager::GetInstance()->AddObserver(this);
  }
  ~SiteWindowWatcher() override = default;

  void Follow() {
    PictureInPictureWindowManager* manager =
        PictureInPictureWindowManager::GetInstance();
    content::WebContents* page = manager->GetChildWebContents();
    std::unique_ptr<SiteWindow>& current = CurrentSlot();
    if (!page || (current && current->page() == page)) {
      return;
    }
    tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(page);
    BrowserWindowInterface* window =
        tab ? tab->GetBrowserWindowInterface() : nullptr;
    if (!window ||
        window->GetType() != BrowserWindowInterface::TYPE_PICTURE_IN_PICTURE) {
      return;
    }
    current.reset();
    current = base::WrapUnique(
        new SiteWindow(manager->GetWebContents(), page, window));
  }

  base::WeakPtrFactory<SiteWindowWatcher> weak_factory_{this};
};

// static
void SiteWindow::Watch() {
  SiteWindowWatcher::Get();
}

// static
SiteWindow* SiteWindow::Current() {
  return CurrentSlot().get();
}

SiteWindow::SiteWindow(content::WebContents* opener,
                       content::WebContents* page,
                       BrowserWindowInterface* window)
    : content::WebContentsObserver(page),
      opener_(opener),
      page_(page),
      window_(window),
      asked_(page->GetContainerBounds().size()) {
  own_window_observation_.Observe(own_widget());
  placement_ = std::make_unique<MiniPlayerPlacement>(
      own_widget(),
      base::BindRepeating(&SiteWindow::Place, base::Unretained(this)));
  // Chromium brings a new site's window to the front, which is not the user
  // clicking it.
  placement_->UpdateNew();
}

SiteWindow::~SiteWindow() {
  // Going while its page lives, as when a newer site's window takes its
  // place: the page goes back to its own window, which Chromium closes.
  if (dock_ || held_page_) {
    Float();
  }
}

bool SiteWindow::ShowsBackToTab() const {
  BrowserView* view = BrowserView::GetBrowserViewForNativeWindow(
      window_->GetWindow()->GetNativeWindow());
  const std::optional<blink::mojom::PictureInPictureWindowOptions> options =
      view ? view->GetDocumentPictureInPictureOptions() : std::nullopt;
  return !options || !options->disallow_return_to_opener;
}

void SiteWindow::Close() {
  if (content::DocumentPictureInPictureWindowController* window_controller =
          controller()) {
    window_controller->Close(/*should_pause_video=*/false);
  }
}

void SiteWindow::BackToTab() {
  if (content::DocumentPictureInPictureWindowController* window_controller =
          controller()) {
    window_controller->FocusInitiator();
    window_controller->Close(/*should_pause_video=*/false);
  }
}

void SiteWindow::CloseContents(content::WebContents* source) {
  // The site closed its page while it was docked. It goes, and so does its
  // own window, which holds a blank page.
  Undock();
  views::Widget* widget = own_widget();
  std::unique_ptr<content::WebContents> page = std::move(held_page_);
  page.reset();
  if (widget) {
    widget->Close();
  }
}

void SiteWindow::SetContentsBounds(content::WebContents* source,
                                   const gfx::Rect& bounds) {
  asked_ = bounds.size();
  if (asked_.width() <= kMaxDockedSide && asked_.height() <= kMaxDockedSide) {
    if (dock_) {
      dock_->SetSiteWindowShape(asked_);
    }
    return;
  }
  // Grown past the limit, it floats at the size asked for, from now on.
  grew_too_big_ = true;
  Float();
  if (content::WebContentsDelegate* own = page_->GetDelegate()) {
    own->SetContentsBounds(page_, bounds);
  }
}

void SiteWindow::ActivateContents(content::WebContents* contents) {
  if (dock_ && dock_->GetWidget()) {
    dock_->GetWidget()->Activate();
  }
}

content::KeyboardEventProcessingResult SiteWindow::PreHandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  content::WebContentsDelegate* delegate = DockDelegate();
  return delegate ? delegate->PreHandleKeyboardEvent(source, event)
                  : content::KeyboardEventProcessingResult::NOT_HANDLED;
}

bool SiteWindow::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  content::WebContentsDelegate* delegate = DockDelegate();
  return delegate && delegate->HandleKeyboardEvent(source, event);
}

void SiteWindow::WebContentsDestroyed() {
  SiteWindowWatcher::Get().Forget(this);
}

void SiteWindow::OnWidgetDestroying(views::Widget* widget) {
  own_window_gone_ = true;
  own_window_observation_.Reset();
  prompt_check_.Stop();
  placement_.reset();
  Undock();
  SiteWindowWatcher::Get().Forget(this);
}

void SiteWindow::Place(MiniPlayerDock* dock) {
  if (own_window_gone_) {
    return;
  }
  if (MustFloat()) {
    dock = nullptr;
  }
  if (AsksTheUser()) {
    if (!prompt_check_.IsRunning()) {
      prompt_check_.Start(
          FROM_HERE, kPromptCheck,
          base::BindRepeating(&MiniPlayerPlacement::Update,
                              base::Unretained(placement_.get())));
    }
  } else {
    prompt_check_.Stop();
  }
  if (dock == dock_) {
    return;
  }
  if (!dock) {
    Float();
    return;
  }
  if (dock_) {
    dock_->ReleaseSiteWindow();
  }
  if (!held_page_) {
    // Out of its own window, which hides and keeps a blank page meanwhile.
    TabStripModel* tabs = window_->GetTabStripModel();
    held_page_ = tabs->DiscardWebContents(
        page_, content::WebContents::Create(content::WebContents::CreateParams(
                   page_->GetBrowserContext())));
    held_page_->SetDelegate(this);
    window_->GetWindow()->Hide();
  }
  dock_ = dock;
  dock->TakeSiteWindow(std::make_unique<DockedPage>(this, page_, dock), asked_);
  dock->set_on_site_window_destroying(base::BindOnce(
      [](SiteWindow* self) {
        self->dock_ = nullptr;
        if (self->placement_) {
          self->placement_->set_current(nullptr);
          self->placement_->ScheduleUpdate();
        }
      },
      base::Unretained(this)));
  placement_->set_current(dock);
}

void SiteWindow::Undock() {
  if (!dock_) {
    return;
  }
  dock_->ReleaseSiteWindow();
  dock_ = nullptr;
  if (placement_) {
    placement_->set_current(nullptr);
  }
}

void SiteWindow::Float() {
  Undock();
  if (!held_page_ || own_window_gone_) {
    return;
  }
  held_page_->SetDelegate(nullptr);
  TabStripModel* tabs = window_->GetTabStripModel();
  tabs->DiscardWebContents(tabs->GetWebContentsAt(0), std::move(held_page_));
  window_->GetWindow()->ShowInactive();
}

bool SiteWindow::MustFloat() const {
  return grew_too_big_ || asked_.width() > kMaxDockedSide ||
         asked_.height() > kMaxDockedSide || AsksTheUser();
}

bool SiteWindow::AsksTheUser() const {
  if (own_window_gone_) {
    return false;
  }
  BrowserView* view = BrowserView::GetBrowserViewForNativeWindow(
      window_->GetWindow()->GetNativeWindow());
  auto* frame = view ? views::AsViewClass<PictureInPictureBrowserFrameView>(
                           view->browser_widget()->GetFrameView())
                     : nullptr;
  return frame && frame->IsOverlayViewVisible();
}

content::WebContentsDelegate* SiteWindow::DockDelegate() {
  content::WebContents* active =
      dock_ ? dock_->window()->GetTabStripModel()->GetActiveWebContents()
            : nullptr;
  return active ? active->GetDelegate() : nullptr;
}

content::DocumentPictureInPictureWindowController* SiteWindow::controller() {
  return content::PictureInPictureWindowController::
      GetOrCreateDocumentPictureInPictureController(opener_);
}

views::Widget* SiteWindow::own_widget() {
  if (own_window_gone_) {
    return nullptr;
  }
  return views::Widget::GetWidgetForNativeWindow(
      window_->GetWindow()->GetNativeWindow());
}

}  // namespace uw
