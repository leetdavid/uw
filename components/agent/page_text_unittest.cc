// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/page_text.h"

#include <string>
#include <string_view>
#include <vector>

#include "components/optimization_guide/proto/features/common_quality_data.pb.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

namespace proto = optimization_guide::proto;

using ::testing::ElementsAre;
using ::testing::EndsWith;
using ::testing::HasSubstr;
using ::testing::Not;

proto::ContentNode* Add(proto::ContentNode* parent,
                        proto::ContentAttributeType type,
                        int node_id = 0) {
  proto::ContentNode* node = parent->add_children_nodes();
  node->mutable_content_attributes()->set_attribute_type(type);
  node->mutable_content_attributes()->set_common_ancestor_dom_node_id(node_id);
  return node;
}

void AddText(proto::ContentNode* parent, std::string_view text) {
  Add(parent, proto::CONTENT_ATTRIBUTE_TEXT)
      ->mutable_content_attributes()
      ->mutable_text_data()
      ->set_text_content(std::string(text));
}

proto::FormControlData* AddControl(proto::ContentNode* parent,
                                   proto::FormControlType type,
                                   int node_id) {
  proto::FormControlData* control =
      Add(parent, proto::CONTENT_ATTRIBUTE_FORM_CONTROL, node_id)
          ->mutable_content_attributes()
          ->mutable_form_control_data();
  control->set_form_control_type(type);
  return control;
}

class PageTextTest : public testing::Test {
 protected:
  PageTextTest() {
    page_.mutable_main_frame_data()
        ->mutable_document_identifier()
        ->set_serialized_token("main");
    root()->mutable_content_attributes()->set_attribute_type(
        proto::CONTENT_ATTRIBUTE_ROOT);
  }

  proto::ContentNode* root() { return page_.mutable_root_node(); }

  PageText Describe(size_t max_length = 10000) {
    return DescribePage(page_, max_length);
  }

  proto::AnnotatedPageContent page_;
};

TEST_F(PageTextTest, NumbersFieldsAndButtonsInPageOrder) {
  AddText(Add(root(), proto::CONTENT_ATTRIBUTE_HEADING), "Order form");
  proto::ContentNode* field =
      Add(root(), proto::CONTENT_ATTRIBUTE_FORM_CONTROL, 5);
  field->mutable_content_attributes()->set_label("Your name");
  field->mutable_content_attributes()
      ->mutable_form_control_data()
      ->set_form_control_type(proto::FORM_CONTROL_TYPE_INPUT_TEXT);
  proto::ContentNode* button =
      Add(root(), proto::CONTENT_ATTRIBUTE_FORM_CONTROL, 7);
  button->mutable_content_attributes()
      ->mutable_form_control_data()
      ->set_form_control_type(proto::FORM_CONTROL_TYPE_BUTTON_SUBMIT);
  AddText(button, "Send");
  proto::ContentNode* paragraph =
      Add(root(), proto::CONTENT_ATTRIBUTE_PARAGRAPH);
  AddText(paragraph, "We ship ");
  AddText(paragraph, " in two days.");

  const PageText page = Describe();

  EXPECT_EQ(page.text,
            "# Order form\n"
            "[1] text field \"Your name\"\n"
            "[2] button \"Send\"\n"
            "We ship in two days.\n");
  EXPECT_THAT(page.elements,
              ElementsAre(PageElement{5, "main", "text field \"Your name\""},
                          PageElement{7, "main", "button \"Send\""}));
}

TEST_F(PageTextTest, ItMarksWhatNeedsTheUsersApproval) {
  proto::ContentNode* form = Add(root(), proto::CONTENT_ATTRIBUTE_FORM);
  AddControl(form, proto::FORM_CONTROL_TYPE_INPUT_PASSWORD, 2)
      ->set_field_name("password");
  proto::FormControlData* card =
      AddControl(form, proto::FORM_CONTROL_TYPE_INPUT_TEXT, 3);
  card->set_field_name("card number");
  card->add_coarse_autofill_field_type(
      proto::COARSE_AUTOFILL_FIELD_TYPE_CREDIT_CARD);
  AddControl(form, proto::FORM_CONTROL_TYPE_BUTTON_SUBMIT, 4)
      ->set_field_name("Pay");
  // A button outside any form submits nothing.
  AddControl(root(), proto::FORM_CONTROL_TYPE_BUTTON_SUBMIT, 5)
      ->set_field_name("Menu");

  const PageText page = Describe();

  ASSERT_EQ(page.elements.size(), 4u);
  EXPECT_EQ(page.elements[0].fills, "a password");
  EXPECT_EQ(page.elements[1].fills, "payment details");
  EXPECT_FALSE(page.elements[1].submits);
  EXPECT_TRUE(page.elements[2].submits);
  EXPECT_EQ(page.elements[2].name, "button \"Pay\"");
  EXPECT_FALSE(page.elements[3].submits);
}

TEST_F(PageTextTest, AFieldIsNamedByItsLabel) {
  proto::ContentNode* label = Add(root(), proto::CONTENT_ATTRIBUTE_TEXT);
  label->mutable_content_attributes()->set_label_for_dom_node_id(5);
  label->mutable_content_attributes()->mutable_text_data()->set_text_content(
      "Email address");
  AddControl(root(), proto::FORM_CONTROL_TYPE_INPUT_EMAIL, 5)
      ->set_field_name("email");

  EXPECT_EQ(Describe().text,
            "Email address\n"
            "[1] email field \"Email address\"\n");
}

TEST_F(PageTextTest, ALinkSaysWhereItGoes) {
  proto::ContentNode* link = Add(root(), proto::CONTENT_ATTRIBUTE_ANCHOR, 9);
  link->mutable_content_attributes()->mutable_anchor_data()->set_url(
      "https://example.com/plans");
  AddText(link, "See plans");

  EXPECT_EQ(Describe().text,
            "[1] link \"See plans\" to https://example.com/plans\n");
}

TEST_F(PageTextTest, AFieldShowsItsValueButNeverAPassword) {
  proto::FormControlData* name =
      AddControl(root(), proto::FORM_CONTROL_TYPE_INPUT_TEXT, 1);
  name->set_field_name("name");
  name->set_field_value("Ada");
  proto::FormControlData* password =
      AddControl(root(), proto::FORM_CONTROL_TYPE_INPUT_PASSWORD, 2);
  password->set_field_name("password");
  password->set_field_value("hunter2");
  proto::FormControlData* terms =
      AddControl(root(), proto::FORM_CONTROL_TYPE_INPUT_CHECKBOX, 3);
  terms->set_field_name("terms");
  terms->set_is_checked(true);

  const PageText page = Describe();

  EXPECT_EQ(page.text,
            "[1] text field \"name\" = \"Ada\"\n"
            "[2] password field \"password\" (filled in)\n"
            "[3] checkbox \"terms\" (checked)\n");
  EXPECT_THAT(page.text, Not(HasSubstr("hunter2")));
}

TEST_F(PageTextTest, AClickableAreaIsReadThroughTheLinksInIt) {
  proto::ContentNode* card = Add(root(), proto::CONTENT_ATTRIBUTE_CONTAINER, 4);
  card->mutable_content_attributes()
      ->mutable_interaction_info()
      ->add_clickability_reasons(proto::CLICKABILITY_REASON_CLICK_HANDLER);
  AddText(Add(card, proto::CONTENT_ATTRIBUTE_PARAGRAPH), "Cats, a guide");
  AddText(Add(card, proto::CONTENT_ATTRIBUTE_ANCHOR, 6), "Read more");
  proto::ContentNode* menu = Add(root(), proto::CONTENT_ATTRIBUTE_CONTAINER, 8);
  menu->mutable_content_attributes()
      ->mutable_interaction_info()
      ->add_clickability_reasons(proto::CLICKABILITY_REASON_CURSOR_POINTER);
  AddText(menu, "Open menu");

  const PageText page = Describe();

  EXPECT_EQ(page.text,
            "Cats, a guide\n"
            "[1] link \"Read more\"\n"
            "[2] clickable \"Open menu\"\n");
  EXPECT_THAT(page.elements,
              ElementsAre(PageElement{6, "main", "link \"Read more\""},
                          PageElement{8, "main", "clickable \"Open menu\""}));
}

TEST_F(PageTextTest, AFrameNamesItsOwnDocument) {
  proto::ContentNode* frame = Add(root(), proto::CONTENT_ATTRIBUTE_IFRAME);
  frame->mutable_content_attributes()
      ->mutable_iframe_data()
      ->mutable_frame_data()
      ->mutable_document_identifier()
      ->set_serialized_token("frame");
  AddControl(frame, proto::FORM_CONTROL_TYPE_BUTTON_BUTTON, 3);
  AddText(frame->mutable_children_nodes(0), "Pay");
  // Chromium leaves out a frame from another site.
  Add(root(), proto::CONTENT_ATTRIBUTE_IFRAME);

  const PageText page = Describe();

  EXPECT_EQ(page.text, "[1] button \"Pay\"\n");
  EXPECT_THAT(page.elements,
              ElementsAre(PageElement{3, "frame", "button \"Pay\""}));
}

TEST_F(PageTextTest, AdsAreLeftOut) {
  proto::ContentNode* ad = Add(root(), proto::CONTENT_ATTRIBUTE_ANCHOR, 2);
  ad->mutable_content_attributes()->set_is_ad_related(true);
  AddText(ad, "Buy now");
  AddText(Add(root(), proto::CONTENT_ATTRIBUTE_PARAGRAPH), "The article");

  const PageText page = Describe();

  EXPECT_EQ(page.text, "The article\n");
  EXPECT_TRUE(page.elements.empty());
}

TEST_F(PageTextTest, WhatDoesNotFitIsLeftOut) {
  for (int i = 0; i < 50; ++i) {
    AddText(Add(root(), proto::CONTENT_ATTRIBUTE_PARAGRAPH),
            "A paragraph of the article.");
  }

  const PageText page = Describe(200);

  EXPECT_LE(page.text.size(), 200u + 40u);
  EXPECT_THAT(page.text, EndsWith("The rest of the page is left out.\n"));
}

}  // namespace
}  // namespace uw
