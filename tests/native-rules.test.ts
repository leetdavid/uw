import assert from "node:assert/strict";
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import type { TestContext } from "node:test";
import { checkNativeRules, REPO } from "../scripts/native-rules.ts";

const CONTROLLER = "browser/tab_organization/tab_organization_controller.cc";
const ADAPTER = "browser/ui/views/tab_organization/tab_tree_adapter.cc";
const HANDLER = "browser/ui/webui/uw_organization/uw_organization_handler.cc";
const HANDLER_H = "browser/ui/webui/uw_organization/uw_organization_handler.h";
const REWRITE = "browser/ui/uw_url_rewrite.cc";
const KEYSTORE = "browser/tab_organization/api_key_store_stub.cc";
const PATCH = "chromium/patches/tab-tree.patch";

function write(root: string, name: string, content: string): void {
  const path = join(root, name);
  mkdirSync(join(path, ".."), { recursive: true });
  writeFileSync(path, content);
}

function passingTree(root: string): void {
  write(root, CONTROLLER, `void TabOrganizationController::Sync() {
  if (tree_.PlaceBeside(fresh[0], *last_active_)) {
    ScheduleAlignTabOrder();
  }
}
void TabOrganizationController::OnTabStripModelChanged(TabStripModel*, const TabStripModelChange&, const TabStripSelectionChange&) {
  Sync();
}
void TabOrganizationController::ScheduleAlignTabOrder() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(&TabOrganizationController::RunScheduledAlign, weak_factory_.GetWeakPtr()));
}
void TabOrganizationController::OpenOrganizationSettings() {
  chrome::ShowSettingsSubPage(window_.get(), "uwOrganization");
}
`);
  write(root, ADAPTER, `TabTreeAdapter::~TabTreeAdapter() {
  for (const auto& [id, view] : tasks_) DetachView(view);
  for (const auto& [id, view] : disclosures_) DetachView(view);
  tasks_.clear();
  disclosures_.clear();
}
void TabTreeAdapter::DetachView(views::View* view) {
  view->set_context_menu_controller(nullptr);
  view->set_drag_controller(nullptr);
}
void TabTreeAdapter::RetireView(views::View* view) {
  DetachView(view);
  view->SetVisible(false);
  removing_.insert(view);
}
void TabTreeAdapter::Refresh() {
  for (auto it = tasks_.begin(); it != tasks_.end();) {
    auto* view = it->second.get();
    it = tasks_.erase(it);
    RetireView(view);
  }
  for (auto it = disclosures_.begin(); it != disclosures_.end();) {
    auto* view = it->second.get();
    it = disclosures_.erase(it);
    RetireView(view);
  }
}
void TabTreeAdapter::ExecuteCommand(int command, int) {
  controller_->OpenOrganizationSettings();
}
`);
  write(root, HANDLER, `void UwOrganizationHandler::LoadModels(const std::string& endpoint, const std::string& api_key, LoadModelsCallback callback) {
  models_request_->Send(base::BindOnce(&UwOrganizationHandler::OnModelsLoaded, weak_factory_.GetWeakPtr(), std::move(callback)));
}
`);
  write(root, HANDLER_H, `class UwOrganizationHandler {
  const raw_ptr<Profile> profile_;
  base::WeakPtrFactory<UwOrganizationHandler> weak_factory_{this};
};
`);
  write(root, REWRITE, `bool HandleUwSchemeRewrite(GURL* url, content::BrowserContext* context) {
  if (!url->SchemeIs(kUwScheme)) return false;
  replacements.SetSchemeStr(content::kChromeUIScheme);
  return false;
}
`);
  write(root, KEYSTORE, `std::lock_guard<std::mutex> lock(KeyMutex());
static base::NoDestructor<std::mutex> mutex;
`);
  write(root, PATCH, `void UnpinnedTabContainerView::ResetCollectionNode() {
  // Never detach adapter views here.
  uw_tree_.reset();
}
`);
}

function fixture(t: TestContext): string {
  const root = mkdtempSync(join(tmpdir(), "uw-native-rules-"));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  passingTree(root);
  return root;
}

test("current tree passes native crash-regression checks", () => {
  assert.deepEqual(checkNativeRules(REPO), []);
});

test("minimal passing tree has no violations", (t) => {
  assert.deepEqual(checkNativeRules(fixture(t)), []);
});

test("synchronous tab moves from Sync trip the reentrancy check", (t) => {
  const root = fixture(t);
  write(root, CONTROLLER, `void TabOrganizationController::Sync() {
  AlignTabOrder();
}
void TabOrganizationController::ScheduleAlignTabOrder() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(&TabOrganizationController::RunScheduledAlign, weak_factory_.GetWeakPtr()));
}
void TabOrganizationController::OpenOrganizationSettings() {
  chrome::ShowSettingsSubPage(window_.get(), "uwOrganization");
}
`);
  const violations = checkNativeRules(root);
  assert.ok(violations.some((v) => v.rule === "tabstrip-reentrancy"), JSON.stringify(violations));
});

test("observer tab moves trip the reentrancy check", (t) => {
  const root = fixture(t);
  write(root, CONTROLLER, `void TabOrganizationController::Sync() {
  ScheduleAlignTabOrder();
}
void TabOrganizationController::OnTabStripModelChanged(TabStripModel*, const TabStripModelChange&, const TabStripSelectionChange&) {
  tabs_->MoveWebContentsAt(0, 1, false, std::nullopt);
}
void TabOrganizationController::ScheduleAlignTabOrder() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(&TabOrganizationController::RunScheduledAlign, weak_factory_.GetWeakPtr()));
}
void TabOrganizationController::OpenOrganizationSettings() {
  chrome::ShowSettingsSubPage(window_.get(), "uwOrganization");
}
`);
  const violations = checkNativeRules(root);
  assert.ok(violations.some((v) => v.rule === "tabstrip-reentrancy"), JSON.stringify(violations));
});

test("adapter teardown must not remove views", (t) => {
  const root = fixture(t);
  write(root, ADAPTER, `TabTreeAdapter::~TabTreeAdapter() {
  RemoveOwnedView(notice_);
}
void TabTreeAdapter::Refresh() {
  for (auto it = tasks_.begin(); it != tasks_.end();) {
    auto* view = it->second.get();
    it = tasks_.erase(it);
    RemoveOwnedView(view);
  }
  for (auto it = disclosures_.begin(); it != disclosures_.end();) {
    auto* view = it->second.get();
    it = disclosures_.erase(it);
    RemoveOwnedView(view);
  }
}
`);
  const violations = checkNativeRules(root);
  assert.ok(violations.some((v) => v.rule === "adapter-teardown"), JSON.stringify(violations));
});

test("unflushed stale views trip the removal check", (t) => {
  const root = fixture(t);
  write(root, ADAPTER, `TabTreeAdapter::~TabTreeAdapter() {
  for (const auto& [id, view] : tasks_) DetachView(view);
  tasks_.clear();
}
void TabTreeAdapter::Refresh() {
  it->second->SetVisible(false);
}
`);
  const violations = checkNativeRules(root);
  assert.ok(violations.some((v) => v.rule === "stale-view-removal"), JSON.stringify(violations));
});

test("adapter teardown must detach back-pointers", (t) => {
  const root = fixture(t);
  write(root, ADAPTER, `TabTreeAdapter::~TabTreeAdapter() {
  tasks_.clear();
  disclosures_.clear();
}
void TabTreeAdapter::Refresh() {
  for (auto it = tasks_.begin(); it != tasks_.end();) {
    auto* view = it->second.get();
    it = tasks_.erase(it);
    RetireView(view);
  }
  for (auto it = disclosures_.begin(); it != disclosures_.end();) {
    auto* view = it->second.get();
    it = disclosures_.erase(it);
    RetireView(view);
  }
}
`);
  const violations = checkNativeRules(root);
  assert.ok(violations.some((v) => v.rule === "adapter-detach"), JSON.stringify(violations));
});

test("setup dialog use trips the dialog-lifetime check", (t) => {
  const root = fixture(t);
  write(root, ADAPTER, `TabTreeAdapter::~TabTreeAdapter() {
  tasks_.clear();
}
void TabTreeAdapter::Refresh() {
  for (auto it = tasks_.begin(); it != tasks_.end();) {
    auto* view = it->second.get();
    it = tasks_.erase(it);
    RemoveOwnedView(view);
  }
  for (auto it = disclosures_.begin(); it != disclosures_.end();) {
    auto* view = it->second.get();
    it = disclosures_.erase(it);
    RemoveOwnedView(view);
  }
}
#include "uw/browser/ui/views/tab_organization/ai_setup_dialog.h"
void TabTreeAdapter::ExecuteCommand(int command, int) {
  AiSetupDialog::Show(organize_, controller_->GetWeakPtr());
}
`);
  const violations = checkNativeRules(root);
  assert.ok(violations.some((v) => v.rule === "dialog-lifetime"), JSON.stringify(violations));
});

test("window state in the settings handler trips the scope check", (t) => {
  const root = fixture(t);
  write(root, HANDLER, `void UwOrganizationHandler::GetState(GetStateCallback callback) {
  auto* model = window_->GetTabStripModel();
  weak_factory_.GetWeakPtr();
}
`);
  const violations = checkNativeRules(root);
  assert.ok(violations.some((v) => v.rule === "handler-scope"), JSON.stringify(violations));
});

test("url alias must not claim the rewrite", (t) => {
  const root = fixture(t);
  write(root, REWRITE, `bool HandleUwSchemeRewrite(GURL* url, content::BrowserContext* context) {
  if (!url->SchemeIs(kUwScheme)) return false;
  replacements.SetSchemeStr(content::kChromeUIScheme);
  return true;
}
`);
  const violations = checkNativeRules(root);
  assert.ok(violations.some((v) => v.rule === "url-alias"), JSON.stringify(violations));
});

test("disk-backed stub key store trips the memory check", (t) => {
  const root = fixture(t);
  write(root, KEYSTORE, `PrefService* prefs = profile->GetPrefs();
std::lock_guard<std::mutex> lock(KeyMutex());
static base::NoDestructor<std::mutex> mutex;
`);
  const violations = checkNativeRules(root);
  assert.ok(violations.some((v) => v.rule === "keystore-memory"), JSON.stringify(violations));
});
