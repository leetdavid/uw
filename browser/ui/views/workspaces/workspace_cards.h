// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_WORKSPACES_WORKSPACE_CARDS_H_
#define UW_BROWSER_UI_VIEWS_WORKSPACES_WORKSPACE_CARDS_H_

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

namespace views {
class Label;
class MenuRunner;
class Textfield;
}  // namespace views

namespace uw {

class CardBubble;
class IconField;
class IconGrid;
class WorkspaceSwitcher;

// The cards of the uw-workspaces guide. Each returns a bubble for the
// switcher to show; `closing` tells it when the card closes.

// Rename Workspace, Change Icon, Theme…, and Delete Workspace…, above the
// workspace's icon.
CardBubble* MakeWorkspaceMenu(WorkspaceSwitcher* switcher,
                              const std::string& id,
                              views::View* icon,
                              base::OnceClosure closing);
// The suggested emoji for workspace `id`, above its icon.
CardBubble* MakeIconCard(WorkspaceSwitcher* switcher,
                         const std::string& id,
                         views::View* icon,
                         base::OnceClosure closing);
// Workspace `id`'s emoji and its name in a field, above `foot`. Return and a
// click elsewhere save the name, and Esc leaves it.
CardBubble* MakeRenameCard(const std::string& id,
                           views::View* foot,
                           base::OnceClosure closing);
// A name, an emoji, and a profile for a new workspace, above `foot`.
CardBubble* MakeNewWorkspaceCard(WorkspaceSwitcher* switcher,
                                 views::View* foot,
                                 base::OnceClosure closing);
// Asks before deleting workspace `id`, over the page, and also asks about its
// profile when it is the profile's last workspace.
CardBubble* MakeDeleteCard(WorkspaceSwitcher* switcher,
                           const std::string& id,
                           base::OnceClosure closing);

// The workspace menu's rows.
class WorkspaceMenu : public views::View {
  METADATA_HEADER(WorkspaceMenu, views::View)

 public:
  // Views tests reach by id.
  enum Id {
    kRenameId = 1,
    kIconId,
    kThemeId,
    kDeleteId,
  };

  WorkspaceMenu(WorkspaceSwitcher* switcher, const std::string& id);
  WorkspaceMenu(const WorkspaceMenu&) = delete;
  WorkspaceMenu& operator=(const WorkspaceMenu&) = delete;
  ~WorkspaceMenu() override;

 private:
  // Closes the menu, then runs `action` with the switcher.
  void Then(base::RepeatingCallback<void(WorkspaceSwitcher*)> action);

  const raw_ptr<WorkspaceSwitcher> switcher_;
  const std::string id_;
};

// The rename card's emoji and name field.
class RenameCard : public views::View, public views::TextfieldController {
  METADATA_HEADER(RenameCard, views::View)

 public:
  RenameCard(const std::string& id, CardBubble* bubble);
  RenameCard(const RenameCard&) = delete;
  RenameCard& operator=(const RenameCard&) = delete;
  ~RenameCard() override;

  views::Textfield* field() { return field_; }

  // Gives the workspace the typed name, unless it is blank.
  void Save();

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  const std::string id_;
  const raw_ptr<CardBubble> bubble_;
  raw_ptr<views::Textfield> field_ = nullptr;
};

// The new workspace card, with its step that names a new profile.
class NewWorkspaceCard : public views::View,
                         public ui::SimpleMenuModel::Delegate,
                         public views::TextfieldController {
  METADATA_HEADER(NewWorkspaceCard, views::View)

 public:
  NewWorkspaceCard(WorkspaceSwitcher* switcher, CardBubble* bubble);
  NewWorkspaceCard(const NewWorkspaceCard&) = delete;
  NewWorkspaceCard& operator=(const NewWorkspaceCard&) = delete;
  ~NewWorkspaceCard() override;

  views::Textfield* name_field() { return name_; }
  IconField* icon_field() { return icon_; }
  views::Textfield* profile_name_field() { return profile_name_; }
  views::View* profile_button() { return profile_button_; }
  views::Label* hint() { return hint_; }
  bool naming_profile() const;

  // Picks the existing profile in folder `profile`.
  void PickProfile(const base::FilePath& profile);
  // Shows the step that names a new profile.
  void StartNewProfile();

  // Return creates the workspace, or ends the profile step. Esc closes the
  // card, or goes back from the profile step. Each says whether the card
  // closes.
  bool Accept();
  bool Cancel();

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;

 private:
  void ShowProfileMenu();
  void EndNewProfile(bool keep);
  void Update();

  const raw_ptr<WorkspaceSwitcher> switcher_;
  const raw_ptr<CardBubble> bubble_;
  raw_ptr<views::View> main_ = nullptr;
  raw_ptr<views::View> profile_step_ = nullptr;
  raw_ptr<IconField> icon_ = nullptr;
  raw_ptr<IconGrid> grid_ = nullptr;
  raw_ptr<views::Textfield> name_ = nullptr;
  raw_ptr<views::View> profile_button_ = nullptr;
  raw_ptr<views::Label> profile_label_ = nullptr;
  raw_ptr<views::Label> profile_icons_ = nullptr;
  raw_ptr<views::Label> hint_ = nullptr;
  raw_ptr<views::Textfield> profile_name_ = nullptr;
  // The profile picked, or empty with `new_profile_` named.
  base::FilePath profile_;
  std::u16string new_profile_;
  std::vector<base::FilePath> menu_profiles_;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_WORKSPACES_WORKSPACE_CARDS_H_
