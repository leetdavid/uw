// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/pins/home_url_card.h"

#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/scoped_observation.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/grit/generated_resources.h"
#include "components/strings/grit/components_strings.h"
#include "components/tabs/public/tab_interface.h"
#include "components/url_formatter/elide_url.h"
#include "components/url_formatter/url_fixer.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"
#include "url/gurl.h"
#include "uw/browser/pins/pin_sync.h"
#include "uw/browser/ui/look/menu_card.h"

namespace uw {
namespace {

constexpr int kWidth = 320;
constexpr int kGap = 8;

// The open card. One opens at a time.
std::unique_ptr<views::Widget>& OpenCard() {
  static base::NoDestructor<std::unique_ptr<views::Widget>> card;
  return *card;
}

void ForgetClosedCard() {
  if (OpenCard() && OpenCard()->IsClosed()) {
    OpenCard().reset();
  }
}

// The bubble of the card. It deletes itself once its widget is gone.
class HomeUrlBubble : public views::BubbleDialogDelegate,
                      public views::WidgetObserver {
 public:
  explicit HomeUrlBubble(views::View* tile)
      : BubbleDialogDelegate(tile, views::BubbleBorder::TOP_LEFT) {}

  void Watch(views::Widget* widget) { observation_.Observe(widget); }

  // views::WidgetObserver:
  void OnWidgetClosing(views::Widget* widget) override {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&ForgetClosedCard));
  }

  void OnWidgetDestroyed(views::Widget* widget) override {
    observation_.Reset();
    // The tile's window outlives the card, and would pass its theme changes
    // on to the gone card until this deletes.
    SetAnchorView(nullptr);
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, this);
  }

 private:
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{
      this};
};

void Save(tabs::TabHandle tab, views::Textfield* field) {
  const GURL home =
      url_formatter::FixupURL(base::UTF16ToUTF8(field->GetText()));
  if (const tabs::TabInterface* pinned = tab.Get()) {
    PinSync::SetHome(pinned, home);
  }
}

}  // namespace

// static
views::Widget* HomeUrlCard::Show(const tabs::TabInterface* tab,
                                 views::View* tile) {
  const PinStore::Pin* pin = PinSync::PinFor(tab);
  if (!tile || !pin) {
    return nullptr;
  }
  auto* bubble = new HomeUrlBubble(tile);
  bubble->SetTitle(l10n_util::GetStringFUTF16(
      IDS_UW_HOME_URL_TITLE,
      url_formatter::FormatUrlForDisplayOmitSchemePathAndTrivialSubdomains(
          pin->home)));
  bubble->SetButtonLabel(ui::mojom::DialogButton::kOk,
                         l10n_util::GetStringUTF16(IDS_SAVE));
  bubble->set_fixed_width(kWidth);
  bubble->set_corner_radius(kMenuCardRadius);
  HomeUrlCard* card =
      bubble->SetContentsView(std::make_unique<HomeUrlCard>(pin->home));
  card->field()->GetViewAccessibility().SetName(bubble->GetWindowTitle());
  bubble->SetInitiallyFocusedView(card->field());
  bubble->SetAcceptCallback(base::BindOnce(&Save, tab->GetHandle(),
                                           base::Unretained(card->field())));
  OpenCard() = views::BubbleDialogDelegate::CreateBubble(bubble);
  bubble->Watch(OpenCard().get());
  OpenCard()->Show();
  return OpenCard().get();
}

HomeUrlCard::HomeUrlCard(const GURL& home) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), kGap));
  field_ = AddChildView(std::make_unique<views::Textfield>());
  field_->SetText(base::UTF8ToUTF16(home.spec()));
  field_->SelectAll(/*reversed=*/false);
  views::Label* hint = AddChildView(std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_UW_HOME_URL_HINT),
      views::style::CONTEXT_LABEL, views::style::STYLE_SECONDARY));
  hint->SetMultiLine(true);
  hint->SetHorizontalAlignment(gfx::ALIGN_LEFT);
}

HomeUrlCard::~HomeUrlCard() = default;

BEGIN_METADATA(HomeUrlCard)
END_METADATA

}  // namespace uw
