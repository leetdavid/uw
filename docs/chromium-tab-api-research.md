# Chromium tab API research

Investigated 2026-09-23 at `792bf6722e73a45aa9e47c163b9901bdc17f3230`. [`chrome/VERSION`](https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/VERSION) confirms **153.0.8010.53**. All upstream links below use this pin.
Scope supplied for this research: native tree sidebar, Scratchpad, manual task groups/extraction, close branch, organization undo, and sidebar menu. Jev automation and shared live pins are excluded.
Read local `AGENTS.md`, [architecture](architecture.md), product requirements, and build status. Findings come from pinned source through the GitHub API. No Chromium build or runtime verification was performed.

## Recommendation

Retain Chromium's `TabView` rows, `RootTabCollectionNode`, tab controllers, pinned container, and vertical region. Add window-owned hierarchy metadata using `ui::TreeNodeModel`, then adapt the unpinned vertical layout and tree-specific input handling. This is a source-backed recommendation, not an implemented or tested integration. Implementability: **6/10**, including browser behavior and maintenance, using the [roadmap convention](plans/roadmap.md#implementability-estimates).
This reuses tab multi-selection, favicons, loading/audio indicators, close buttons, menus, hover cards, and the tab-drag machinery. A separate `views::TreeView` would need replacements or extensions for several of those behaviors. Keep substantial uw logic in the ordinary feature files described by the architecture, connected through narrow patches. [TabView][tab-view] [controller][controller]
Use the uw tree for page parents, task containers, Scratchpad, names, order, and expansion state. Keep actual tab ownership in `TabStripModel`. A uw task is not automatically a Chromium `TabGroupId`; stock groups and splits still need explicit reconciliation when tabs cross their boundaries. [model][model] [collections][collections]

## What already exists

| Capability | Native tab views/collections | `views::TreeView` / `ui::TreeNodeModel` |
| --- | --- | --- |
| Arbitrary hierarchy | No. `TabCollection::Type` is `TABSTRIP`, `PINNED`, `UNPINNED`, `GROUP`, `SPLIT`; tabs are leaves. `TabGroupTabCollection` allows only `SPLIT` child collections. `AddCollection<T>(unique_ptr<T>, size_t)` CHECKs allowed types. [collections][collections] [group constructor][group-collection] | Yes. Nodes own children; page nodes can carry a tab reference and children. `TreeNodeModel<Node>::Add(Node*, unique_ptr<Node>, size_t)` and `Remove(Node*, size_t)` notify observers. [tree model][tree-model] |
| Multi-selection | Yes. `TabView::OnMousePressed` supports Cmd-click on macOS, Shift-click, and combined modifiers. The controller has `ToggleSelected`, `ExtendSelectionTo`, and `AddSelectionFromAnchorTo`, each taking `const tabs::TabInterface*`. [tab input][tab-input] [controller][controller] | No stock multi-selection. `SetSelectedNode(ui::TreeModelNode*)`, `GetSelectedNode()`, and private `selected_node_` represent one node. [tree view][tree-view] |
| Editing | `ShowGroupEditorBubble(const TabCollectionNode*)` edits real Chrome groups. Synthetic task names need a Views editor, for example `Textfield::SetText(std::u16string_view)` and `SetController(TextfieldController*)`. [controller][controller] [textfield][textfield] | Built in: `SetEditable(bool)`, `StartEditing(TreeModelNode*)`, `CommitEdit()`, `CancelEdit()`; `TreeViewController::CanEdit(TreeView*, TreeModelNode*)` restricts editing to tasks. [tree view][tree-view] [tree controller][tree-controller] |
| Drag/drop | Existing `TabDragHandler::InitializeDrag(TabCollectionNode&, const ui::ListSelectionModel&, const ui::LocatedEvent&)`, `ContinueDrag(View&, const LocatedEvent&)`, `EndDrag(EndDragReason)`. Native targets are collection/index based, not uw parent/sibling positions. [drag][drag] | No tree-specific drag/drop or reparent policy in this implementation. Inherited Views hooks do not provide tree dragging automatically. [tree view][tree-view] |
| Context menus | `ShowTabContextMenu(TabCollectionNode*, const gfx::Point&, ui::mojom::MenuSourceType)` preserves stock tab commands. [controller][controller] | `ShowContextMenu` forwards to `views::ContextMenuController` when over a node; the application supplies menu contents and commands. [tree implementation][tree-impl] |

`TreeNodeModel` is useful independently of `TreeView`. Move owned metadata nodes with the model's `Remove` and `Add` wrappers; direct node mutation needs explicit observer notification. Neither class supplies organization transactions or undo. [tree model][tree-model]

## Window-scoped APIs and lifetime

| Need | Exact upstream entry point and consequence |
| --- | --- |
| Window's tabs | `BrowserWindowInterface::GetTabStripModel()` returns `TabStripModel*`. `GetSessionID() const` identifies the window; `GetAllTabInterfaces()` enumerates its tabs. Use the supplied window, not the globally active browser. [window][window] |
| Resolve identity | `tabs::TabInterface::GetHandle()` returns `tabs::TabHandle`; `handle.Get()` returns the tab or null. `GetIndexOfTab(const tabs::TabInterface*) const` returns the current index or `kNoTab`. Resolve again at execution, including after a menu has been open. [handles][handles] [model][model] |
| Page access | `TabInterface::GetContents() const` returns the current `WebContents*`; discarding can replace it. `RegisterWillDiscardContents(WillDiscardContentsCallback)` reports replacement. Cache tab identity, not a long-lived contents pointer. [tab interface][tab-interface] |
| Activation/selection | `ActivateTab(tabs::TabInterface*, TabStripUserGestureDetails = ...)`; `SelectTabAt(int)`, `DeselectTabAt(int)`, `ExtendSelectionTo(int)`, `AddSelectionFromAnchorTo(int)`, `IsTabSelected(int) const`. `SetSelectionFromModel(ui::ListSelectionModel)` is available; organization should normally leave selection alone. [model][model] |
| Changes | `TabStripModelObserver::OnTabStripModelChanged(TabStripModel*, const TabStripModelChange&, const TabStripSelectionChange&)` covers insertion, removal, move, replacement, and selection. Register through `AddObserver`; `base::ScopedObservation<TabStripModel, ...>` is explicitly forbidden at this pin. [observer][observer] [model][model] |
| Window ownership | Own the model/undo manager in `BrowserWindowFeatures`, initialized in `Init(BrowserWindowInterface*)`. Existing `VerticalTabStripStateController` demonstrates `DECLARE_USER_DATA`, `ui::ScopedUnownedUserData<T>`, and `From(BrowserWindowInterface*)`. The user-data host exposes a feature; it does not own it. [window features][window-features] [state controller][state-controller] |

Keep metadata outside the view: `BaseTabStripRegionView::ResetTabStrip()` destroys the root node and controllers. Orientation switches and view reconstruction must not erase tasks or undo history. Observe removals and pin changes; `RegisterWillDetach(WillDetach)` distinguishes deletion from `kInsertIntoOtherWindow`. A live handle can now belong to another window, so null checking alone is insufficient. [region base][region-base] [tab interface][tab-interface]
Handles do not persist across process restarts. For later restart restoration, existing hooks are `SessionService::AddWindowExtraData(SessionID, const char*, const std::string&)` and `AddTabExtraData(SessionID, SessionID, const char*, const std::string&)`. `session_restore.cc` already reads window extra data for vertical-tab state. Persist durable node IDs and remap restored tabs; these hooks do not automatically restore a uw tree. [handles][handles] [session service][sessions] [restore][restore]

## Concrete insertion seam

1. `views/frame/base_tab_strip_region_view.cc::InitializeTabStrip()` creates `RootTabCollectionNode`, `TabDragHandlerImpl`, and `TabStripCollectionController`, then calls `root_node_->Init()`. Preserve this construction so tab anchors, loading updates, focus lookup, and drag contexts still find real tab rows. [region base][region-base]
2. `views/tabs/common/tab_collection_node.cc::CreateViewForNode(TabCollectionNode*)` creates `TabStripView`, `PinnedTabContainerView`, `UnpinnedTabContainerView`, and `TabView`. `TabCollectionNode` mirrors real collections 1:1; do not inject fictional group/page collections into it. [node][node]
3. `TabStripView::AddScrollViewContents(std::unique_ptr<views::View>)` installs pinned and unpinned containers into separate scroll views. It expects those concrete container classes. Extend the existing unpinned container rather than replacing this callback with an unrelated tree widget. This retains the upstream pinned strip and surrounding region. [strip view][strip-view]
4. The layout seam is `UnpinnedTabContainerViewLayout::CalculateVerticalLayout(const UnpinnedTabContainerView*, const views::SizeBounds&) const`. It currently walks `collection_node_->GetDirectChildren()`. Supply a uw visible-row projection here, including an always-present Scratchpad header before task rows, indentation, and collapsed descendants. Keep existing tab row objects; add native Views task/header controls. This requires layout/visibility, focus-order, and accessibility adaptation, not just a factory substitution. [layout][layout] [node][node]
5. Adapt `UnpinnedTabContainerView::GetTabDragTarget(const gfx::Point&)`, `GetLinkDropIndex(const gfx::Point&)`, and `TabDragHandlerImpl` for uw branch destinations. `GetCollectionNodeFromView` recognizes only tab/group/split views; generic header rows are ignored by existing drag targeting. `HandleDraggedTabsIntoNode` ultimately calls `MoveSelectedTabsTo(index, group)`, so it cannot express a page parent. Reuse the drag session mechanics, supply branch membership and destination translation. [container][container] [drag implementation][drag-impl]
6. `VerticalTabStripRegionView::OnTabStripViewSet()` places the strip after its top separator. Add the organization button through `VerticalTabStripTopContainer` or an adjacent native header row. Its existing context menu demonstrates `ui::SimpleMenuModel`, `views::MenuRunner`, and an expand-on-hover lock. [region][region] [top container][top-container]

For the button, `views::MenuButton(PressedCallback, std::u16string_view, int)` and `MenuRunner::RunMenuAt(Widget*, MenuButtonController*, const gfx::Rect&, MenuAnchorPosition, ui::mojom::MenuSourceType, ...)` are available. Own the runner as a member; native macOS menus can enter a blocking nested loop. Route sidebar and context commands to the same window-owned organization controller. [button][button] [menu runner][menu-runner]
The tab controller accepts a `std::unique_ptr<TabMenuModelFactory>` override. `Create(ui::SimpleMenuModel::Delegate*, TabMenuModelDelegate*, TabStripModel*, int)` returns `std::unique_ptr<ui::SimpleMenuModel>`. Custom execution also needs routing: `TabContextMenuController::ExecuteCommand` casts ordinary IDs to `TabStripModel::ContextMenuCommand`. A menu-factory override alone is insufficient. [factory][factory] [menu routing][menu-routing]

## Behavior and failure cases

- Capture source relationships at insertion. `GetOpenerOfTabAt(int) const` and `SetOpenerOfTabAt(int, tabs::TabInterface*)` exist, but opener is temporary selection bookkeeping. `AddTab` also assigns an opener to typed new tabs; `TabNavigating` and active-tab changes can clear openers. It is not the durable tree. [model implementation][model-impl]
- For genuine new-tab links, `chrome/browser/ui/navigator/browser_navigator.cc` has `NavigateParams::source_contents`, disposition, and transition at the `AddTab(...)` call. Capture the source handle there when needed, then retain uw parentage independently. Do not turn every opener-bearing Cmd-T tab into a child or reparent on ordinary navigation. Cross-window and pinned sources need explicit eligibility checks. [navigator][navigator]
- Extraction should normalize selected ancestors/descendants into disjoint branches, preserve internal order, and mutate metadata without recreating tabs. If leaf order must follow the tree for browser shortcuts and Shift-selection, use `int MoveWebContentsAt(int index, int to_position, bool select_after_move)` with `false`. It moves inline, emits a move rather than detach/attach, and preserves the active tab. Re-resolve indices after every move. [model][model]
- Respect pinned-prefix, group-contiguity, and split constraints. The group-explicit move overload CHECKs invalid group contiguity; `AddToNewGroup`/`RemoveFromGroup` can unsplit partially selected splits. Do not use detach/reinsert for same-window organization. Avoid reentrant `TabStripModel` mutations from observer callbacks; update metadata there and perform any necessary browser mutation after the notification completes. [model][model] [model implementation][model-impl]
- Preserve current page and keyboard focus, including the selection at undo time. `TabCollectionNode::MoveChild` retains row objects and restores row focus during reparenting. Metadata layout changes should not call activation or `RequestFocus()`. Verify focused descendants too. Stock `TabStripView::OnTabChanged` expands a collapsed Chrome group; this must not override uw's stored collapsed state. [node][node] [strip view][strip-view]
- A `TreeView` replacement adds another trap: `SetSelectedNode` expands ancestors; `TreeNodeRemoved` selects a nearby node and notifies the controller; `SetModel` clears internal selection and expansion state. Wiring every selection callback to tab activation would switch pages during model moves. [tree implementation][tree-impl]
- Ordinary page close removes only that page's metadata and promotes surviving children after actual removal. For close branch, snapshot the root page and descendant handles, then use `ExecuteCloseTabsCommand(base::RepeatingCallback<std::vector<tabs::TabInterface*>()>, bool delete_groups)` with `false` to retain saved Chrome groups. Resolve only still-live, same-window members each invocation. This preserves unload/history handling; cancellation can leave a partial branch. Do not delete the entire metadata branch optimistically. Closing the final tab can destroy the window. [model][model] [model implementation][model-impl] [window][window]
- Reuse `UndoManager::AddUndoOperation(std::unique_ptr<UndoOperation>)`, `StartGroupingActions()`, `EndGroupingActions()`, and `Undo()`. [`UndoOperation::Undo()`](https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/components/undo/undo_operation.h) supplies uw-specific inverses. One extraction includes group creation and every move in one undo action. Keep this manager window-owned and separate from page/text editing undo. [undo][undo]
- Recommended undo policy for intervening tab changes: apply inverse placements only to surviving same-window tabs, preserve new tabs, and leave the current active tab/focus intact. Use stable node IDs and parent/sibling anchors, not indices or whole-tree replacement. If the old parent is gone, use the nearest surviving ancestor or Scratchpad. Before removing a newly created task on undo, preserve any later-added children. Closed tabs are not resurrected by organization undo; restoring closed pages is Chromium session restoration, not preservation of a destroyed live page. [handles][handles] [model][model]

## GN ownership and verification

| Source or dependency | Exact existing target |
| --- | --- |
| `views/tabs/common/{tab_view,tab_collection_node,tab_strip_view,unpinned_tab_container_view*}.cc` | `//chrome/browser/ui/views/tabs/common:impl`; header target `:common`. [GN][common-gn] |
| Vertical top/bottom controls | `//chrome/browser/ui/views/tabs/vertical:impl`. [GN][vertical-gn] |
| `views/frame/{base,vertical}_tab_strip_region_view.cc` | `//chrome/browser/ui:ui`, not the small `views/frame:frame` target. [GN][ui-gn] |
| `TabStripModel` / browser-window owner | `//chrome/browser/ui/tabs:tab_strip` plus `:tab_strip_impl`; `browser_window_features.cc` is in `//chrome/browser/ui/browser_window/internal:internal`. [tabs GN][tabs-gn] [window GN][window-gn] |
| Reused framework types | `//components/tabs:public` / `:impl`, `//ui/base` for `TreeNodeModel`, `//ui/views` for `TreeView`, `Textfield`, buttons and menus, `//ui/menus` for `SimpleMenuModel`, `//ui/base/unowned_user_data`, `//components/undo:undo`. [tabs component][component-gn] [base GN][base-gn] [Views GN][views-gn] [menus GN][menus-gn] [undo GN][undo-gn] |

The `common:impl` target already depends on browser tab/window APIs and Views. Add uw dependencies to the actual consuming targets above; do not put browser-dependent hierarchy logic in a generic component. Source mapping into `src/uw` remains local tooling work per the architecture.
Unit wiring: `chrome/browser/ui/tabs/BUILD.gn:unit_tests` includes `tab_strip_model_unittest.cc`; common and vertical each have `:unit_tests`. `//chrome/test:unit_tests` depends on these. Reuse `TabStripModelTest.*` and the existing vertical-state tests; add uw model cases for extraction, promotion, cycles, and undo after insert/close/detach. [tabs GN][tabs-gn] [common GN][common-gn] [test GN][test-gn]
Browser wiring: common has `:browser_tests`; vertical's `:browser_tests` depends on it, and `//chrome/test:browser_tests` depends on vertical. `vertical_tab_strip_region_view_browsertest.cc` is listed directly in `chrome/test/BUILD.gn`. Existing cases include `TabViewTest.MultiSelectUserActions` and `VerticalTabStripRegionViewTest.SwitchModes`. [tab tests][tab-tests] [region tests][region-tests] [test GN][test-gn]
Interactive wiring follows `common:interactive_ui_tests` through vertical to `//chrome/test:interactive_ui_tests`. `All/TabCollectionNodeInteractiveUiTest.KeepsFocusWhenMovedOutOfGroup/Vertical` and `...ValidateViewFocusOrder/Vertical` are useful native-focus references. The latter is enabled on ordinary macOS builds. [focus tests][focus-tests] [vertical GN][vertical-gn] [test GN][test-gn]
On a prepared build host: run `gn check out/uw`, then `autoninja -C out/uw chrome unit_tests browser_tests interactive_ui_tests`. Execute focused filters, including `--gtest_filter='All/TabCollectionNodeInteractiveUiTest.*/*'`. Optional framework checks are `//ui/views:views_unittests` with `TreeViewTest.*`. Chromium's upstream build/test tooling still requires Python. [Views GN][views-gn] [local build instructions](../README.md#build-host-requirements)
Required uw browser checks: active-page JS/form state and focus survive extraction/undo; collapse stays unchanged; Cmd-click/middle-click create correct children; close cancellation preserves survivors; new/closed/moved-window tabs do not corrupt undo; pins, ordinary shortcuts, native groups/splits, and horizontal-mode round trips remain usable. Source inspection and headless smoke cannot establish these results.

## Blocker assessment

No hard Chromium platform blocker was found for the approved manual slice. There is no drop-in widget that satisfies it: stock collections cannot model the hierarchy, and stock `TreeView` cannot provide the required multi-selection or branch dragging. A requirement to use either unchanged would require a scope change. The recommended path needs a tree-aware layout/input adapter and lifetime tests; it does not require replacing Chromium's tab ownership or introducing shared live pins.
Session persistence and close-branch resurrection are not supplied by `TreeNodeModel` or `UndoManager`. If this slice must restore hierarchy across restart, wire the session extra-data read/write path explicitly. If organization undo must restore closed pages with exact live state, that is additional scope beyond an inverse organization operation.

<!-- Pinned source references -->

[tab-view]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/views/tabs/common/tab_view.h
[tab-input]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/views/tabs/common/tab_view.cc#L571-L689
[controller]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/views/tabs/common/tab_strip_collection_controller.h
[model]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/tabs/tab_strip_model.h
[model-impl]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/tabs/tab_strip_model.cc
[observer]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/tabs/tab_strip_model_observer.h
[collections]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/components/tabs/public/tab_collection.h
[group-collection]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/components/tabs/impl/tab_group_tab_collection.cc
[tree-model]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/ui/base/models/tree_node_model.h
[tree-view]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/ui/views/controls/tree/tree_view.h
[tree-impl]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/ui/views/controls/tree/tree_view.cc
[tree-controller]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/ui/views/controls/tree/tree_view_controller.h
[textfield]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/ui/views/controls/textfield/textfield.h
[window]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/browser_window/public/browser_window_interface.h
[window-features]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/browser_window/internal/browser_window_features.cc
[state-controller]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h
[handles]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/components/tabs/public/supports_handles.h
[tab-interface]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/components/tabs/public/tab_interface.h
[sessions]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/sessions/session_service.h
[restore]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/sessions/session_restore.cc
[navigator]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/navigator/browser_navigator.cc#L870-L1004
[region-base]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/views/frame/base_tab_strip_region_view.cc
[node]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/views/tabs/common/tab_collection_node.cc
[strip-view]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/views/tabs/common/tab_strip_view.cc
[layout]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/views/tabs/common/unpinned_tab_container_view_layout.cc
[container]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/views/tabs/common/unpinned_tab_container_view.cc
[drag]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/views/tabs/common/tab_drag_handler.h
[drag-impl]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/views/tabs/common/tab_drag_handler.cc
[region]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/views/frame/vertical_tab_strip_region_view.cc
[top-container]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_top_container.h
[button]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/ui/views/controls/button/menu_button.h
[menu-runner]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/ui/views/controls/menu/menu_runner.h
[factory]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/tabs/tab_menu_model_factory.h
[menu-routing]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/views/tabs/tab/tab_context_menu_controller.cc
[undo]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/components/undo/undo_manager.h
[common-gn]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/views/tabs/common/BUILD.gn
[vertical-gn]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/views/tabs/vertical/BUILD.gn
[ui-gn]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/BUILD.gn
[tabs-gn]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/tabs/BUILD.gn
[window-gn]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/browser_window/internal/BUILD.gn
[component-gn]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/components/tabs/BUILD.gn
[base-gn]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/ui/base/BUILD.gn
[views-gn]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/ui/views/BUILD.gn
[menus-gn]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/ui/menus/BUILD.gn
[undo-gn]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/components/undo/BUILD.gn
[test-gn]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/test/BUILD.gn
[tab-tests]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/views/tabs/common/tab_view_browsertest.cc
[region-tests]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/views/frame/vertical_tab_strip_region_view_browsertest.cc
[focus-tests]: https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/views/tabs/common/tab_collection_node_interactive_uitest.cc
