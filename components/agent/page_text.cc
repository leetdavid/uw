// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/page_text.h"

#include <map>
#include <string_view>
#include <utility>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "components/optimization_guide/proto/features/common_quality_data.pb.h"

namespace uw {
namespace {

namespace proto = optimization_guide::proto;

constexpr size_t kMaxLabelLength = 80;
constexpr size_t kMaxOptions = 20;

bool IsButton(proto::FormControlType type) {
  switch (type) {
    case proto::FORM_CONTROL_TYPE_BUTTON_BUTTON:
    case proto::FORM_CONTROL_TYPE_BUTTON_SUBMIT:
    case proto::FORM_CONTROL_TYPE_BUTTON_RESET:
    case proto::FORM_CONTROL_TYPE_BUTTON_POPOVER:
    case proto::FORM_CONTROL_TYPE_INPUT_BUTTON:
    case proto::FORM_CONTROL_TYPE_INPUT_IMAGE:
    case proto::FORM_CONTROL_TYPE_INPUT_RESET:
    case proto::FORM_CONTROL_TYPE_INPUT_SUBMIT:
      return true;
    default:
      return false;
  }
}

bool IsSubmit(proto::FormControlType type) {
  return type == proto::FORM_CONTROL_TYPE_BUTTON_SUBMIT ||
         type == proto::FORM_CONTROL_TYPE_INPUT_SUBMIT ||
         type == proto::FORM_CONTROL_TYPE_INPUT_IMAGE;
}

// What typing into a field fills, when the user must approve it.
std::string_view Fills(const proto::FormControlData& control) {
  if (control.form_control_type() == proto::FORM_CONTROL_TYPE_INPUT_PASSWORD) {
    return "a password";
  }
  for (int type : control.coarse_autofill_field_type()) {
    if (type == proto::COARSE_AUTOFILL_FIELD_TYPE_CREDIT_CARD) {
      return "payment details";
    }
  }
  return "";
}

// What the agent is told a form control is. Empty for a control it cannot
// use, which is then read as plain content.
std::string_view Kind(proto::FormControlType type) {
  if (IsButton(type)) {
    return "button";
  }
  switch (type) {
    case proto::FORM_CONTROL_TYPE_FIELDSET:
    case proto::FORM_CONTROL_TYPE_INPUT_HIDDEN:
    case proto::FORM_CONTROL_TYPE_OUTPUT:
      return "";
    case proto::FORM_CONTROL_TYPE_INPUT_CHECKBOX:
      return "checkbox";
    case proto::FORM_CONTROL_TYPE_INPUT_RADIO:
      return "radio button";
    case proto::FORM_CONTROL_TYPE_SELECT_ONE:
    case proto::FORM_CONTROL_TYPE_SELECT_MULTIPLE:
      return "dropdown";
    case proto::FORM_CONTROL_TYPE_TEXT_AREA:
      return "text area";
    case proto::FORM_CONTROL_TYPE_CONTENT_EDITABLE:
      return "editable text";
    case proto::FORM_CONTROL_TYPE_INPUT_PASSWORD:
      return "password field";
    case proto::FORM_CONTROL_TYPE_INPUT_EMAIL:
      return "email field";
    case proto::FORM_CONTROL_TYPE_INPUT_NUMBER:
      return "number field";
    case proto::FORM_CONTROL_TYPE_INPUT_SEARCH:
      return "search field";
    case proto::FORM_CONTROL_TYPE_INPUT_TELEPHONE:
      return "phone field";
    case proto::FORM_CONTROL_TYPE_INPUT_URL:
      return "address field";
    case proto::FORM_CONTROL_TYPE_INPUT_DATE:
      return "date field";
    case proto::FORM_CONTROL_TYPE_INPUT_FILE:
      return "file field";
    default:
      return "text field";
  }
}

bool IsElement(const proto::ContentAttributes& node) {
  switch (node.attribute_type()) {
    case proto::CONTENT_ATTRIBUTE_ANCHOR:
      return true;
    case proto::CONTENT_ATTRIBUTE_FORM_CONTROL:
      return !Kind(node.form_control_data().form_control_type()).empty();
    default:
      return node.interaction_info().clickability_reasons_size() > 0;
  }
}

bool HasElementBelow(const proto::ContentNode& node) {
  for (const proto::ContentNode& child : node.children_nodes()) {
    if (IsElement(child.content_attributes()) || HasElementBelow(child)) {
      return true;
    }
  }
  return false;
}

void CollectText(const proto::ContentNode& node, std::string& text) {
  const proto::ContentAttributes& attributes = node.content_attributes();
  if (attributes.has_text_data()) {
    base::StrAppend(&text, {" ", attributes.text_data().text_content()});
  }
  for (const proto::ContentNode& child : node.children_nodes()) {
    CollectText(child, text);
  }
}

std::string Flatten(std::string text, size_t max_length) {
  text = base::CollapseWhitespaceASCII(text, true);
  if (text.size() > max_length) {
    base::TruncateUTF8ToByteSize(text, max_length, &text);
    text += "…";
  }
  return text;
}

// Notes the text of each <label> under the node id of the field it names.
void CollectLabels(const proto::ContentNode& node,
                   std::map<int, std::string>& labels) {
  const proto::ContentAttributes& attributes = node.content_attributes();
  if (attributes.has_label_for_dom_node_id()) {
    CollectText(node, labels[attributes.label_for_dom_node_id()]);
  }
  for (const proto::ContentNode& child : node.children_nodes()) {
    CollectLabels(child, labels);
  }
}

class Writer {
 public:
  Writer(const proto::ContentNode& root, size_t max_length)
      : max_length_(max_length) {
    CollectLabels(root, labels_);
  }

  void Walk(const proto::ContentNode& node, const std::string& document) {
    const proto::ContentAttributes& attributes = node.content_attributes();
    if (full_ || attributes.is_ad_related()) {
      return;
    }
    if (attributes.attribute_type() == proto::CONTENT_ATTRIBUTE_IFRAME) {
      // A frame from another site is left out by Chromium; its children are
      // empty.
      const proto::IframeData& frame = attributes.iframe_data();
      if (frame.has_frame_data()) {
        WalkChildren(
            node, frame.frame_data().document_identifier().serialized_token());
      }
      return;
    }
    const bool below = HasElementBelow(node);
    const bool control =
        attributes.attribute_type() == proto::CONTENT_ATTRIBUTE_ANCHOR ||
        attributes.attribute_type() == proto::CONTENT_ATTRIBUTE_FORM_CONTROL;
    // A clickable area holding links or fields is read through them.
    if (IsElement(attributes) && (control || !below)) {
      Element(node, document);
      return;
    }
    if (!below && IsBlock(attributes.attribute_type())) {
      std::string text;
      CollectText(node, text);
      text = Flatten(text, max_length_);
      if (!text.empty()) {
        Line(attributes.attribute_type() == proto::CONTENT_ATTRIBUTE_HEADING
                 ? "# " + text
                 : text);
      }
      return;
    }
    const bool form =
        attributes.attribute_type() == proto::CONTENT_ATTRIBUTE_FORM;
    forms_ += form;
    WalkChildren(node, document);
    forms_ -= form;
  }

  PageText Take() && { return std::move(page_); }

 private:
  static bool IsBlock(proto::ContentAttributeType type) {
    switch (type) {
      case proto::CONTENT_ATTRIBUTE_PARAGRAPH:
      case proto::CONTENT_ATTRIBUTE_HEADING:
      case proto::CONTENT_ATTRIBUTE_TEXT:
      case proto::CONTENT_ATTRIBUTE_LIST_ITEM:
        return true;
      default:
        return false;
    }
  }

  void WalkChildren(const proto::ContentNode& node,
                    const std::string& document) {
    for (const proto::ContentNode& child : node.children_nodes()) {
      Walk(child, document);
    }
  }

  void Element(const proto::ContentNode& node, const std::string& document) {
    const proto::ContentAttributes& attributes = node.content_attributes();
    PageElement element{attributes.common_ancestor_dom_node_id(), document};
    std::string more;
    if (attributes.attribute_type() == proto::CONTENT_ATTRIBUTE_ANCHOR) {
      element.name = base::StrCat({"link \"", Label(node), "\""});
      if (!attributes.anchor_data().url().empty()) {
        more = base::StrCat({" to ", attributes.anchor_data().url()});
      }
    } else if (attributes.attribute_type() ==
               proto::CONTENT_ATTRIBUTE_FORM_CONTROL) {
      const proto::FormControlData& control = attributes.form_control_data();
      element.name = base::StrCat(
          {Kind(control.form_control_type()), " \"", Label(node), "\""});
      more = Value(control);
      element.submits = forms_ > 0 && IsSubmit(control.form_control_type());
      element.fills = Fills(control);
    } else {
      element.name = base::StrCat({"clickable \"", Label(node), "\""});
    }
    page_.elements.push_back(element);
    Line(base::StrCat({"[", base::NumberToString(page_.elements.size()), "] ",
                       element.name, more}));
  }

  // The words that name a link, field, or button.
  std::string Label(const proto::ContentNode& node) const {
    const proto::ContentAttributes& attributes = node.content_attributes();
    std::string label = attributes.label();
    if (label.empty()) {
      CollectText(node, label);
    }
    if (label.empty() && attributes.has_form_control_data()) {
      const auto named = labels_.find(attributes.common_ancestor_dom_node_id());
      const proto::FormControlData& control = attributes.form_control_data();
      label = named != labels_.end()          ? named->second
              : control.placeholder().empty() ? control.field_name()
                                              : control.placeholder();
    }
    return Flatten(label, kMaxLabelLength);
  }

  static std::string Value(const proto::FormControlData& control) {
    switch (control.form_control_type()) {
      case proto::FORM_CONTROL_TYPE_INPUT_PASSWORD:
        return control.field_value().empty() ? "" : " (filled in)";
      case proto::FORM_CONTROL_TYPE_INPUT_CHECKBOX:
      case proto::FORM_CONTROL_TYPE_INPUT_RADIO:
        return control.is_checked() ? " (checked)" : "";
      case proto::FORM_CONTROL_TYPE_SELECT_ONE:
      case proto::FORM_CONTROL_TYPE_SELECT_MULTIPLE: {
        std::string options;
        int listed = 0;
        for (const proto::SelectOption& option : control.select_options()) {
          if (listed++ == kMaxOptions) {
            options += ", …";
            break;
          }
          base::StrAppend(&options,
                          {listed > 1 ? ", " : "", "\"", option.text(), "\"",
                           option.is_selected() ? " (selected)" : ""});
        }
        return options.empty() ? "" : " with " + options;
      }
      default:
        if (IsButton(control.form_control_type()) ||
            control.field_value().empty()) {
          return "";
        }
        return " = \"" + Flatten(control.field_value(), kMaxLabelLength) + "\"";
    }
  }

  void Line(std::string_view line) {
    if (page_.text.size() + line.size() + 1 > max_length_) {
      page_.text += "… The rest of the page is left out.\n";
      full_ = true;
      return;
    }
    base::StrAppend(&page_.text, {line, "\n"});
  }

  const size_t max_length_;
  // How many forms the walk is inside.
  int forms_ = 0;
  std::map<int, std::string> labels_;
  bool full_ = false;
  PageText page_;
};

}  // namespace

PageText DescribePage(const proto::AnnotatedPageContent& page,
                      size_t max_length) {
  Writer writer(page.root_node(), max_length);
  writer.Walk(page.root_node(),
              page.main_frame_data().document_identifier().serialized_token());
  return std::move(writer).Take();
}

}  // namespace uw
