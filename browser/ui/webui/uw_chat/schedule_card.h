// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_WEBUI_UW_CHAT_SCHEDULE_CARD_H_
#define UW_BROWSER_UI_WEBUI_UW_CHAT_SCHEDULE_CARD_H_

#include <optional>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "base/values.h"
#include "uw/browser/agent/agent_loop.h"
#include "uw/browser/ui/webui/uw_chat/uw_chat.mojom.h"
#include "uw/components/agent/scheduled_chat.h"
#include "uw/components/ai_providers/providers.h"

class PrefService;

namespace uw {

class ScheduledChats;

// Approval modes as the profile, a chat's file, and a schedule keep them.
std::string_view NameOf(ApprovalMode mode);
// Auto for a name it does not know.
ApprovalMode ModeNamed(std::string_view name);

// A new schedule card's settings, before the request: daily at 9:00 in the
// Mac's time zone, change alerts, the model last picked on a schedule card or
// else `model`, and the approval mode `mode`.
ScheduledChat DefaultSchedule(const ScheduledChats* chats,
                              std::optional<ModelChoice> model,
                              ApprovalMode mode);

// The schedule card's state for `chat`, the schedule of `chat_id`, with how
// its runs stand in `chats`.
mojom::ScheduleStatePtr ScheduleStateOf(const ScheduledChats* chats,
                                        std::string_view chat_id,
                                        const ScheduledChat& chat);

// A new schedule card's state: DefaultSchedule() with the agent model and the
// approval mode `prefs` keep.
mojom::ScheduleStatePtr NewScheduleState(const ScheduledChats* chats,
                                         const PrefService& prefs);

// A run's line as a chat file keeps it, for the page.
mojom::RunLinePtr RunLineFrom(const base::DictValue& line);

// The runs the chat file `saved` keeps that ran at `since` or later, oldest
// first, as the page shows them.
std::vector<mojom::StepPtr> RunStepsOf(
    const std::optional<base::DictValue>& saved,
    base::Time since);

// What a schedule card saved, as uw keeps it. Nothing when it asks nothing or
// its schedule cannot run.
std::optional<ScheduledChat> ScheduledChatFrom(
    const mojom::ChatSchedule& schedule);

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_CHAT_SCHEDULE_CARD_H_
