// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_AGENT_SCHEDULE_TOOL_H_
#define UW_COMPONENTS_AGENT_SCHEDULE_TOOL_H_

#include <string>

#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "uw/components/agent/scheduled_chat.h"

namespace uw {

// The tool a chat's agent fills in the schedule card with, when the user asks
// in words for something to happen on a schedule. Only the user saves the
// card. See docs/product.md, "Scheduled chats".
inline constexpr char kScheduleTool[] = "schedule";

// The schedule tool, as a model is told about it.
base::DictValue ScheduleToolSchema();

// `card`, as the schedule card would open, filled in from the arguments of a
// schedule call made at `now`, or what is wrong with them, to tell the model.
base::expected<ScheduledChat, std::string> FillScheduleCard(
    const base::DictValue& args,
    ScheduledChat card,
    base::Time now);

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_SCHEDULE_TOOL_H_
