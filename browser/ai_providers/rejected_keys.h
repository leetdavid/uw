// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AI_PROVIDERS_REJECTED_KEYS_H_
#define UW_BROWSER_AI_PROVIDERS_REJECTED_KEYS_H_

#include <set>
#include <string>
#include <string_view>

#include "base/callback_list.h"
#include "base/no_destructor.h"

namespace uw {

// The keys providers rejected, for Setup on the new tab page. A key counts as
// rejected once a request with it gets HTTP 401 or 403, until a later request
// with it works, so a new key starts out fine. Kept as hashes, and only while
// uw runs. See docs/plans/new-tab-page.md.
class RejectedKeys {
 public:
  static RejectedKeys& Get();
  RejectedKeys(const RejectedKeys&) = delete;
  RejectedKeys& operator=(const RejectedKeys&) = delete;

  // What the class keeps of `key`.
  static std::string HashOf(std::string_view key);

  // A request with the key whose hash is `key_hash` worked, or ended with
  // `http_status`.
  void Heard(const std::string& key_hash, bool ok, int http_status);
  bool Rejected(std::string_view key) const;
  bool empty() const { return rejected_.empty(); }
  base::CallbackListSubscription Observe(base::RepeatingClosure callback);

 private:
  friend class base::NoDestructor<RejectedKeys>;
  RejectedKeys();
  ~RejectedKeys();

  std::set<std::string> rejected_;
  base::RepeatingClosureList changed_;
};

}  // namespace uw

#endif  // UW_BROWSER_AI_PROVIDERS_REJECTED_KEYS_H_
