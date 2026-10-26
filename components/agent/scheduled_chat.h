// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_AGENT_SCHEDULED_CHAT_H_
#define UW_COMPONENTS_AGENT_SCHEDULED_CHAT_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "base/values.h"
#include "url/gurl.h"
#include "uw/components/agent/run_report.h"
#include "uw/components/agent/schedule.h"

namespace uw {

// Counted failures in a row that pause a schedule.
inline constexpr int kFailuresBeforePause = 3;

// A chat's schedule, as its schedule card sets it, and what uw keeps about
// its runs. See docs/product.md, "Scheduled chats".
struct ScheduledChat {
  Schedule schedule;
  // What each run is asked.
  std::string request;
  // For a watched page, its address, and what the watch card's chip asked
  // for, such as "Price drops below $500", which is empty when the user wrote
  // the request in their own words. Both empty for any other scheduled chat.
  std::string page;
  std::string watch_for;
  // Regular updates tell the user after every run. Change alerts tell them
  // only when a run finds what they asked to hear about, or after every run
  // with `even_when_nothing_changes`.
  bool regular_updates = false;
  bool even_when_nothing_changes = false;
  // The model runs use: a provider's id, a model, and an effort. Empty for
  // the chat's own model.
  std::string provider_id;
  std::string model;
  std::string effort;
  // "ask_first", "auto", or "full_auto".
  std::string approval_mode = "auto";
  bool paused = false;
  bool muted = false;
  // The trigger the schedule keeps, saved from the chat's running one: its
  // command, "end" or "lines", and the folder it runs in. Empty for none.
  std::string trigger;
  std::string trigger_wake = "end";
  std::string trigger_folder;

  // When it last ran, or was due.
  base::Time last;
  // Failures in a row that count toward the pause.
  int failures = 0;
  // Whether the latest run failed, counted or not, so that a streak of
  // failures notifies once.
  bool failing = false;
  // What went wrong with the latest run, while runs fail.
  std::string last_failure;
  // Why uw paused it after kFailuresBeforePause failures.
  std::string pause_reason;
  // The latest kPastRuns reports, oldest first.
  std::vector<PastRun> recent;
  // The values its runs track, in the order runs first reported them.
  std::vector<TrackedName> tracked;

  bool operator==(const ScheduledChat&) const;
};

// The address that `url` matches a watched page by: `url` without the part
// after # and without common tracking parameters, such as utm_source and ref.
GURL WatchedAddress(const GURL& url);

base::DictValue ScheduledChatToValue(const ScheduledChat& chat);
std::optional<ScheduledChat> ScheduledChatFromValue(
    const base::DictValue& value);

// Gives `report`'s values the names and units of `chat`'s tracked values
// they match, ignoring case and treating spaces and dashes as underscores,
// and tracks the names it does not know yet, so that the first run names each
// value and later runs reuse the name. A unit a value leaves out comes from
// its tracked value.
void NameValues(ScheduledChat& chat, RunReport& report);

// How a run ended.
enum class RunEnd { kReported, kFailed, kModelUnreachable, kStopped };

// What a run's end means for the user.
struct RunEffect {
  // What the notification says under the chat's title, such as "Change
  // alert", and its text. Empty `what` for no notification.
  std::string what;
  std::string text;
  // Whether the run counts toward the pause.
  bool counted = false;
  // Whether this run paused the schedule.
  bool paused = false;
};

// Records in `chat` a run that ended at `when` as `end`, with `report` when it
// reported and `error` when it did not, and says what the user hears. A report
// that names a site wanting a sign-in is a counted failure.
RunEffect AfterRun(ScheduledChat& chat,
                   RunEnd end,
                   const RunReport& report,
                   std::string_view error,
                   base::Time when);

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_SCHEDULED_CHAT_H_
