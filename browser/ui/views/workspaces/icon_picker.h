// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_WORKSPACES_ICON_PICKER_H_
#define UW_BROWSER_UI_VIEWS_WORKSPACES_ICON_PICKER_H_

#include <string>

#include "base/functional/callback.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

namespace uw {

// A workspace's emoji, picked or typed.
using IconPicked = base::RepeatingCallback<void(const std::string& icon)>;

// The square that shows a workspace's emoji. It keeps the last character the
// macOS emoji picker or the keyboard types into it.
class IconField : public views::Textfield, public views::TextfieldController {
  METADATA_HEADER(IconField, views::Textfield)

 public:
  IconField(const std::string& icon, IconPicked typed);
  IconField(const IconField&) = delete;
  IconField& operator=(const IconField&) = delete;
  ~IconField() override;

  const std::string& icon() const { return icon_; }
  void SetIcon(const std::string& icon);
  // Opens the macOS emoji picker, which types into this field.
  void OpenEmojiPicker();

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;

 private:
  IconPicked typed_;
  std::string icon_;
};

// The sixteen suggested emoji in two rows, the picked one ringed, and More…
// under them for the macOS emoji picker.
class IconGrid : public views::View {
  METADATA_HEADER(IconGrid, views::View)

 public:
  IconGrid(const std::string& selected,
           IconPicked picked,
           base::RepeatingClosure more);
  IconGrid(const IconGrid&) = delete;
  IconGrid& operator=(const IconGrid&) = delete;
  ~IconGrid() override;

  // Rings `icon`, or none when it is not among the suggestions.
  void Select(const std::string& icon);
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_WORKSPACES_ICON_PICKER_H_
