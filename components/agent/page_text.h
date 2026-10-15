// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_AGENT_PAGE_TEXT_H_
#define UW_COMPONENTS_AGENT_PAGE_TEXT_H_

#include <cstddef>
#include <string>
#include <vector>

namespace optimization_guide::proto {
class AnnotatedPageContent;
}

namespace uw {

// A link, field, or button, as Chromium's agent engine names it.
struct PageElement {
  int node_id = 0;
  std::string document;
  // How the reading names it, such as `button "Send"`.
  std::string name;
  // Clicking it submits a form.
  bool submits = false;
  // Pressing Enter in it submits a form.
  bool enter_submits = false;
  // What typing into it fills when that is "a password" or "payment
  // details". Empty for any other field.
  std::string fills;

  bool operator==(const PageElement&) const = default;
};

// A page as the agent reads it: one line per block of text, and one line per
// link, field, and button, numbered from 1 in page order. Element n is
// `elements[n - 1]`.
struct PageText {
  std::string text;
  std::vector<PageElement> elements;
};

// Describes `page` in at most about `max_length` bytes. What does not fit is
// left out, and the text says so.
PageText DescribePage(
    const optimization_guide::proto::AnnotatedPageContent& page,
    size_t max_length);

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_PAGE_TEXT_H_
