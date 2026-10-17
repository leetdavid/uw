// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_ORGANIZE_CARD_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_ORGANIZE_CARD_H_

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class BrowserWindowInterface;

namespace views {
class BubbleDialogDelegate;
class Label;
}  // namespace views

namespace uw {

class OrganizeBar;

// The Organize menu: a card over the Organize button that says what automatic
// organization is doing, pauses or resumes it, undoes the last move, and
// shows the organizer's activity. See the sidebar menus plan.
class OrganizeCard : public views::View {
  METADATA_HEADER(OrganizeCard, views::View)

 public:
  // Views tests reach by id.
  enum Id {
    kStatusId = 1,
    kPauseHourId,
    kPauseUntilRestartId,
    kPauseUntilResumedId,
    kResumeId,
    kTurnOnId,
    kSetUpId,
    kUndoId,
    kActivityId,
    kSettingsId,
  };

  // The bubble that holds a new card over `anchor`.
  static std::unique_ptr<views::BubbleDialogDelegate> CreateBubble(
      views::View* anchor,
      OrganizeBar* bar,
      BrowserWindowInterface* window);

  OrganizeCard(OrganizeBar* bar, BrowserWindowInterface* window);
  OrganizeCard(const OrganizeCard&) = delete;
  OrganizeCard& operator=(const OrganizeCard&) = delete;
  ~OrganizeCard() override;

  // The status line, such as "Paused, resuming in 42 min".
  std::u16string StatusForTesting() const;

 private:
  // Rows change after the click that changed them has finished, since
  // rebuilding deletes the row that was clicked.
  void ScheduleRebuild();
  void Rebuild();
  void AddStatus();
  void UpdateStatus();
  void AddAction();
  void AddUndo();
  void AddActivity();
  void Run(int command);
  void RunAndClose(int command);
  void OpenProviders();

  const raw_ptr<OrganizeBar> bar_;
  const raw_ptr<BrowserWindowInterface> window_;
  PrefChangeRegistrar prefs_;
  base::CallbackListSubscription tree_subscription_;
  base::RepeatingTimer countdown_;
  raw_ptr<views::Label> status_bold_ = nullptr;
  raw_ptr<views::Label> status_rest_ = nullptr;
  bool rebuild_pending_ = false;
  base::WeakPtrFactory<OrganizeCard> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_ORGANIZE_CARD_H_
