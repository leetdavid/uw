// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_TAB_ORGANIZATION_ORGANIZER_LOG_H_
#define UW_BROWSER_TAB_ORGANIZATION_ORGANIZER_LOG_H_

#include <string>
#include <string_view>

#include "base/time/time.h"
#include "base/values.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/remote_model_client.h"

namespace uw {

// Starts uw with a record of every request the organizer makes:
// --uw-organizer-log=/path/to/file. The file holds tab titles, URLs, and page
// excerpts, so nothing is written without the switch.
inline constexpr char kOrganizerLogSwitch[] = "uw-organizer-log";

// One request to a model and its answer.
struct OrganizerExchange {
  OrganizerExchange();
  OrganizerExchange(OrganizerExchange&&);
  OrganizerExchange& operator=(OrganizerExchange&&);
  ~OrganizerExchange();

  std::string model;
  GURL url;
  std::string body;
  base::TimeTicks sent_at;
  base::TimeDelta took;
  RemoteModelClient::Outcome answer;
};

// Appends `exchange` to the file named by --uw-organizer-log as one line of
// JSON. `purpose` is "placement", "accepted", or "name". `result` says what
// uw did with the answer. `details` are further fields of the line.
void LogOrganizerExchange(std::string_view purpose,
                          const OrganizerExchange& exchange,
                          std::string_view result,
                          base::DictValue details = base::DictValue());

}  // namespace uw

#endif  // UW_BROWSER_TAB_ORGANIZATION_ORGANIZER_LOG_H_
