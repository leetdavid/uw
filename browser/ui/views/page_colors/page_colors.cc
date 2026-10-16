// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/page_colors/page_colors.h"

#include "base/functional/bind.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "content/public/browser/web_contents.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/tween.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/view.h"
#include "uw/browser/ui/look/page_color.h"

namespace uw {

PageColors::PageColors(BrowserView* view) : view_(view) {
  views::View* top = view_->top_container();
  if (!top->layer()) {
    top->SetPaintToLayer();
    top->layer()->SetFillsBoundsOpaquely(false);
  }
  TabStripModel* tabs = view_->browser()->tab_strip_model();
  tabs->AddObserver(this);
  Observe(tabs->GetActiveWebContents());
  Update();
}

PageColors::~PageColors() {
  SetPageColor(view_, std::nullopt);
}

void PageColors::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  content::WebContents* active = tab_strip_model->GetActiveWebContents();
  if (active == web_contents()) {
    return;
  }
  Observe(active);
  Update();
}

void PageColors::OnBackgroundColorChanged() {
  Update();
}

void PageColors::Update() {
  std::optional<SkColor> color =
      web_contents() ? web_contents()->GetBackgroundColor() : std::nullopt;
  if (color && SkColorGetA(*color) != SK_AlphaOPAQUE) {
    color.reset();
  }
  if (color == color_) {
    return;
  }
  color_ = color;
  SetPageColor(view_, color_);
  if (!view_->GetWidget()) {
    return;
  }
  CrossFade();
  view_->browser_widget()->UserChangedTheme(
      BrowserThemeChangeType::kWebAppTheme);
}

void PageColors::CrossFade() {
  views::View* top = view_->top_container();
  fading_.reset();
  if (!top->layer()) {
    // Leaving fullscreen took the layer away, so this change shows at once.
    top->SetPaintToLayer();
    top->layer()->SetFillsBoundsOpaquely(false);
    return;
  }
  fading_ = top->RecreateLayer();
  const ui::Layer* layer = fading_.get();
  views::AnimationBuilder()
      .OnEnded(base::BindOnce(&PageColors::EndFade, weak_factory_.GetWeakPtr(),
                              layer))
      .OnAborted(base::BindOnce(&PageColors::EndFade,
                                weak_factory_.GetWeakPtr(), layer))
      .Once()
      .SetDuration(kFade)
      .SetOpacity(fading_.get(), 0.f, gfx::Tween::EASE_OUT);
}

void PageColors::EndFade(const ui::Layer* layer) {
  if (fading_.get() == layer) {
    fading_.reset();
  }
}

}  // namespace uw
