# Horse tab organization research

Checked 2026-09-20. Primary-source documentation review, not a running-app test.

## Verified in official documentation

- Web pages are tree nodes. A page can have child pages called Sub-Trails; Side-Trails sit beside the current Trail. Horse uses Trails in place of conventional tabs. [Trails guide][trails]
- The tree also has non-page containers. Trailheads are top-level headers; Folders group related Trails; Notes hold editable text and can have children. [Container guide][containers]
- Nesting is recursive in the documented model: links can branch again from child pages, and Folders can contain other Folders. The hierarchy is not limited to one group level. [Trails guide][trails], [Container guide][containers]
- By default, following a link creates a Sub-Trail beneath its source page instead of replacing that page. Source pages remain until deleted. [Trails guide][trails]
- Branching has exceptions: users can open links in the current Trail or exclude domains, with some exclusions supplied by default. [Link behavior][exclusions]
- Reopening an existing destination normally selects its existing node rather than making a duplicate. An explicit Duplicate Trail action creates another copy. [Duplicates][duplicates]
- Users can drag pages within a Trail, into another Trail, or out into a new one, including multi-selection. Dropping one Trail onto another nests it. [Moving pages][moving], [Overview][overview]
- Users can manually collect selected Trails into a new Folder. [Container guide][containers]
- Branches expand and collapse through disclosure controls or shortcuts; double-clicking a collapsed Trail expands its descendants. Folding does not delete the branch. [Trails guide][trails], [Overview][overview]
- Each window has its own selected page, navigation, and expansion state. The underlying tree is shared: moves, renames, additions, and deletions affect every window. [Multiple windows][windows]
- The sidebar automatically scrolls the current Trail into view when switching. [v2.9.2 release notes][selection]

## What the automation claim covers

- Horse says it builds Trails as browsing proceeds. The documented organizing mechanism is link ancestry, with manual moves and containers available afterward. [Homepage][home], [Overview][overview]
- Navigation handling recognizes cases such as app-like sites, lightboxes, and existing destinations. Its release notes describe building Trails around actual navigation outcomes. [v3.2.0 release notes][navigation]
- No claim of AI intent classification, semantic task extraction, or automatic regrouping of existing branches was found in the reviewed homepage and hierarchy guides. This is a bounded search result, not proof that no such feature exists. [Homepage][home], [Overview][overview], [Trails guide][trails]

## Unknowns and documentation ambiguities

- Recursive nesting is documented, but no maximum depth or explicit unlimited-depth guarantee was found. [Trails guide][trails], [Container guide][containers]
- The move guide does not specify focus, reload, or scroll-position guarantees when moving the currently displayed page, or when collapsing its ancestor. [Moving pages][moving], [Trails guide][trails]
- The homepage describes Option+L as collapsing everything except the current Trail; the shortcut reference calls it "Collapse All Trails". Exact active-branch behavior needs a runtime check. [Homepage][home], [Shortcuts][shortcuts]
- These UX sources do not establish compatibility with Chromium's native tab/group model. Treat that as a separate engineering question. [Overview][overview]

## uw implications and interview decisions

These are questions for uw, not agreed requirements or additional Horse claims.

1. Should pages themselves be parents, should nested tab groups contain pages, or should uw support both?
2. Should ordinary link navigation create descendants, or only links opened as new tabs? If inferred tasks conflict with link ancestry, which organization should win?
3. May automatic regrouping move individual pages, whole subtrees, or both, including extracting a subtree into its own task group?
4. How should uw make live tab moves easy to follow, including moves into collapsed branches?

Updated [roadmap estimates](plans/roadmap.md#implementability-estimates) account for tree-aware changes. uw's [agreed organization behavior](product.md#automatic-tab-organization) now allows selected tabs to move when intent is clear; Horse's docs do not validate that implementation or its feedback behavior.

[home]: https://browser.horse/
[overview]: https://browser.horse/manual/basics/how-does-horse-browser-work
[trails]: https://browser.horse/manual/getting-started/how-to-use-horse-browsers-trails
[containers]: https://browser.horse/manual/getting-started/how-to-use-trailheads-folders-and-notes
[exclusions]: https://browser.horse/manual/basics/how-to-disable-trails
[duplicates]: https://browser.horse/manual/navigation/duplicate-tabs-guide
[moving]: https://browser.horse/manual/navigation/how-to-move-pages
[windows]: https://browser.horse/manual/features/multiple-windows
[selection]: https://browser.horse/changelog/2.9.2
[navigation]: https://browser.horse/changelog/3.2.0
[shortcuts]: https://browser.horse/manual/basics/keyboard-shortcuts
