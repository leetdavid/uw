// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AGENT_TRIGGER_PROCESS_H_
#define UW_BROWSER_AGENT_TRIGGER_PROCESS_H_

#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/threading/sequence_bound.h"

namespace uw {

// The command of a trigger, running out of sight: started in `folder` with
// the environment of the user's terminal, or through their login shell while
// uw has not read it, with nothing on its input. What it prints, on either
// output, comes back a line at a time. Destroying it ends the command and
// everything it started.
class TriggerProcess {
 public:
  using OnLine = base::RepeatingCallback<void(std::string)>;
  // Gets its exit code once it ends, or -1 when it could not start.
  using OnEnd = base::OnceCallback<void(int)>;

  TriggerProcess(const std::string& command,
                 const base::FilePath& folder,
                 OnLine on_line,
                 OnEnd on_end);
  TriggerProcess(const TriggerProcess&) = delete;
  TriggerProcess& operator=(const TriggerProcess&) = delete;
  ~TriggerProcess();

 private:
  class Pipe;

  base::SequenceBound<Pipe> pipe_;
};

}  // namespace uw

#endif  // UW_BROWSER_AGENT_TRIGGER_PROCESS_H_
