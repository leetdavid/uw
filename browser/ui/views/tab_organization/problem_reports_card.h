// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_PROBLEM_REPORTS_CARD_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_PROBLEM_REPORTS_CARD_H_

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/layout/flex_layout_view.h"
#include "uw/browser/ui/glass/glass_background.h"

namespace views {
class Link;
class MdTextButton;
}  // namespace views

namespace uw {

// "Help fix uw?" at the foot of the tab sidebar, on a release's first run,
// with two equal buttons and nothing preselected. Either answer puts it away
// in every window for good; unanswered, it asks again at the next launch. It
// never takes focus. "What's sent" opens the switch in settings.
class ProblemReportsCard : public views::FlexLayoutView {
  METADATA_HEADER(ProblemReportsCard, views::FlexLayoutView)

 public:
  ProblemReportsCard(GlassOf glass, base::RepeatingClosure open_settings);
  ProblemReportsCard(const ProblemReportsCard&) = delete;
  ProblemReportsCard& operator=(const ProblemReportsCard&) = delete;
  ~ProblemReportsCard() override;

  views::MdTextButton* send_button() { return send_; }
  views::MdTextButton* dont_send_button() { return dont_send_; }

 private:
  // views::View:
  void OnThemeChanged() override;

  void Update();

  raw_ptr<views::MdTextButton> send_ = nullptr;
  raw_ptr<views::MdTextButton> dont_send_ = nullptr;
  raw_ptr<views::Link> whats_sent_ = nullptr;
  base::CallbackListSubscription answer_subscription_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_PROBLEM_REPORTS_CARD_H_
