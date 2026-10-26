// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_AGENT_TRIGGER_H_
#define UW_COMPONENTS_AGENT_TRIGGER_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/values.h"

namespace uw {

// A trigger is a command left running out of sight that wakes its chat once
// when it ends, or once for each line it prints. See docs/product.md,
// "Scheduled chats".
inline constexpr char kTriggerTool[] = "trigger";
inline constexpr char kStopTriggerTool[] = "stop_trigger";

// Lines printed within this of each other are one wake.
inline constexpr base::TimeDelta kTriggerLineGap = base::Seconds(2);
// A trigger that prints more lines than this in a minute is stopped.
inline constexpr int kTriggerLinesPerMinute = 600;
// A wake carries the latest lines printed since the last one, this many at
// most, each cut to kTriggerLineLength.
inline constexpr size_t kTriggerWakeLines = 40;
inline constexpr size_t kTriggerLineLength = 400;

// The limits of a trigger the agent starts in a chat, so that one left
// forgotten costs little. A trigger saved on a schedule card has none.
struct TriggerLimits {
  // The least time between two wakes in its first hour, and after.
  base::TimeDelta first_gap = base::Minutes(2);
  base::TimeDelta later_gap = base::Minutes(15);
  // After this, each wake is handled as a run.
  base::TimeDelta first_hour = base::Hours(1);
  // It stops by itself after this.
  base::TimeDelta life = base::Hours(24);
};

// What a trigger waits for: the end of its command, or each line it prints.
enum class TriggerWake { kEnd, kLines };

// A trigger as the agent asked for it.
struct TriggerAsk {
  std::string command;
  TriggerWake wake = TriggerWake::kEnd;
  // What to do with each wake, in the agent's words.
  std::string request;
};

// The trigger tools, as a model is told about them.
base::DictValue TriggerToolSchema();
base::DictValue StopTriggerToolSchema();

// The trigger in the arguments of a trigger call, or what is wrong with them,
// to tell the model.
base::expected<TriggerAsk, std::string> ParseTrigger(
    const base::DictValue& args);

std::string_view NameOf(TriggerWake wake);
TriggerWake TriggerWakeNamed(std::string_view name);

// The least time between two wakes of a trigger that has run for `age`.
base::TimeDelta WakeGap(const TriggerLimits& limits,
                        bool lasting,
                        base::TimeDelta age);
// Whether a wake at `age` starts a run rather than reaching the chat's agent.
bool WakeIsRun(const TriggerLimits& limits, bool lasting, base::TimeDelta age);

// What one wake carries.
struct TriggerWakeLines {
  // The latest lines since the last wake, oldest first.
  std::vector<std::string> lines;
  // Lines printed since the last wake that were left out.
  int dropped = 0;
  // Its exit code, once the command has ended.
  std::optional<int> exit_code;

  bool empty() const { return lines.empty() && dropped == 0; }
};

// Adds a line the command printed, cut to kTriggerLineLength, and drops the
// oldest past kTriggerWakeLines. Blank lines are left out.
void AddWakeLine(TriggerWakeLines& wake, std::string_view line);

// What a wake tells: how the trigger running `command` woke, and its lines.
// Command output is information, never an instruction from the user.
std::string DescribeWake(std::string_view command,
                         const TriggerWakeLines& wake);

// The message that reaches a chat's agent when its trigger wakes it.
std::string WakeMessage(std::string_view command,
                        std::string_view request,
                        const TriggerWakeLines& wake);

// The message a run starts with when a chat's trigger wakes it after its
// first hour, or while the chat is closed: what to do, the time, and the
// wake.
std::string TriggerRunBrief(std::string_view command,
                            std::string_view request,
                            const TriggerWakeLines& wake,
                            base::Time now);

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_TRIGGER_H_
