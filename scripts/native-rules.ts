/** Fast crash-regression guards for uw native code.
 *
 * Each rule pins a fixed app crash (see docs/architecture.md#native-lifetime-rules
 * and docs/try-uw.md#crash-regression-checklist). They scan tracked product
 * source and patches as text, so `pnpm test` catches reintroduced patterns
 * without a Chromium checkout or build.
 */
import { existsSync, readFileSync, readdirSync } from "node:fs";
import { join, relative } from "node:path";
import { fileURLToPath } from "node:url";

export const REPO = fileURLToPath(new URL("../", import.meta.url));

export interface NativeViolation {
  file: string;
  line: number;
  rule: string;
  message: string;
}

function readText(path: string): string | undefined {
  try {
    return readFileSync(path, "utf8");
  } catch {
    return undefined;
  }
}

function lineOf(text: string, index: number): number {
  return text.slice(0, index).split("\n").length;
}

function functionBody(text: string, signature: string): { body: string; start: number } | undefined {
  const start = text.indexOf(signature);
  if (start < 0) return undefined;
  const open = text.indexOf("{", start);
  if (open < 0) return undefined;
  let depth = 0;
  for (let i = open; i < text.length; i++) {
    if (text[i] === "{") depth++;
    if (text[i] === "}") {
      depth--;
      if (depth === 0) return { body: text.slice(open, i + 1), start: open };
    }
  }
  return undefined;
}

function listSources(root: string): string[] {
  const out: string[] = [];
  const walk = (dir: string): void => {
    let entries;
    try {
      entries = readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const entry of entries) {
      const path = join(dir, entry.name);
      if (entry.isDirectory()) walk(path);
      else if (/\.(cc|h|mm)$/.test(entry.name)) out.push(path);
    }
  };
  for (const top of ["browser", "components"]) walk(join(root, top));
  return out;
}

const DIRECT_ALIGN = /(?<!Schedule)AlignTabOrder\s*\(/g;
const MOVE_TABS = /MoveWebContentsAt\s*\(/g;

function checkReentrancy(root: string, push: (v: NativeViolation) => void): void {
  const path = join(root, "browser/tab_organization/tab_organization_controller.cc");
  const text = readText(path);
  if (text === undefined) return;
  const rel = relative(root, path);
  for (const signature of [
    "TabOrganizationController::Sync()",
    "TabOrganizationController::OnTabStripModelChanged(",
    "TabOrganizationController::OnTabChangedAt(",
    "TabOrganizationController::OnTabPinnedStateChanged(",
    "TabOrganizationController::TabGroupedStateChanged(",
    "TabOrganizationController::OnSplitTabChanged(",
  ]) {
    const found = functionBody(text, signature);
    if (!found) continue;
    for (const pattern of [DIRECT_ALIGN, MOVE_TABS]) {
      pattern.lastIndex = 0;
      let match: RegExpExecArray | null;
      while ((match = pattern.exec(found.body)) !== null) {
        push({
          file: rel,
          line: lineOf(text, found.start + (match.index ?? 0)),
          rule: "tabstrip-reentrancy",
          message: `${signature} must not move tabs synchronously; use ScheduleAlignTabOrder(). See docs/architecture.md#native-lifetime-rules.`,
        });
      }
    }
  }
  const schedule = functionBody(text, "TabOrganizationController::ScheduleAlignTabOrder()");
  if (schedule && !/PostTask/.test(schedule.body)) {
    push({
      file: rel,
      line: lineOf(text, schedule.start),
      rule: "tabstrip-reentrancy",
      message: "ScheduleAlignTabOrder must defer via PostTask; the model holds its reentrancy guard while notifying.",
    });
  }
}

function checkAdapterTeardown(root: string, push: (v: NativeViolation) => void): void {
  const path = join(root, "browser/ui/views/tab_organization/tab_tree_adapter.cc");
  const text = readText(path);
  if (text === undefined) return;
  const rel = relative(root, path);
  const destructor = functionBody(text, "TabTreeAdapter::~TabTreeAdapter()");
  if (destructor) {
    for (const banned of ["RemoveOwnedView(", "RemoveChildView(", "RemoveAllChildViews("]) {
      const index = destructor.body.indexOf(banned);
      if (index >= 0) {
        const name = banned.replace(/\($/, "");
        push({
          file: rel,
          line: lineOf(text, destructor.start + index),
          rule: "adapter-teardown",
          message: `Destructor must not call ${name}; views die with the host in cascade order. See docs/architecture.md#native-lifetime-rules.`,
        });
      }
    }
    if (!destructor.body.includes("DetachView(") && !destructor.body.includes("set_drag_controller(nullptr)")) {
      push({
        file: rel,
        line: lineOf(text, destructor.start),
        rule: "adapter-detach",
        message: "Destructor must detach adapter back-pointers (DetachView) from tracked views; the views outlive the adapter at teardown and would otherwise release a dangling drag-controller pointer.",
      });
    }
  }
  const refresh = functionBody(text, "TabTreeAdapter::Refresh()");
  if (refresh !== undefined) {
    for (const banned of ["RemoveOwnedView(", "RemoveChildView(", "RemoveAllChildViews("]) {
      const index = refresh.body.indexOf(banned);
      if (index >= 0) {
        const name = banned.replace(/\($/, "");
        push({
          file: rel,
          line: lineOf(text, refresh.start + index),
          rule: "stale-view-removal",
          message: `Refresh must not call ${name}; stale views retire (detach, hide, exclude from layouts) and die with the host, because the animating layout manager caches raw pointers to laid-out children.`,
        });
      }
    }
    for (const map of ["tasks_", "disclosures_"]) {
      const erase = refresh.body.indexOf(`${map}.erase(`);
      if (erase < 0 || !refresh.body.slice(erase, erase + 500).includes("RetireView(")) {
        push({
          file: rel,
          line: lineOf(text, refresh.start),
          rule: "stale-view-removal",
          message: `Refresh must erase ${map} entries before RetireView so maps never hold a retired view.`,
        });
        break;
      }
    }
    if (!refresh.body.includes("RetireView(")) {
      push({
        file: rel,
        line: lineOf(text, refresh.start),
        rule: "stale-view-removal",
        message: "Refresh must retire stale views (detach, hide, exclude from layouts) and never delete them; the animating layout manager's cached targets outlive any synchronous deletion.",
      });
    }
  }
  const patch = readText(join(root, "chromium/patches/tab-tree.patch"));
  if (patch !== undefined) {
    const anchor = patch.indexOf("ResetCollectionNode");
    if (anchor >= 0) {
      const window = patch.slice(anchor, anchor + 2000);
      if (/Remove(OwnedView|ChildView|AllChildViews)/.test(window)) {
        push({
          file: "chromium/patches/tab-tree.patch",
          line: lineOf(patch, anchor),
          rule: "adapter-teardown",
          message: "ResetCollectionNode must release the adapter without detaching views; forced removal trips the layout dangling-pointer check.",
        });
      }
    }
  }
}

function checkDialogLifetime(root: string, push: (v: NativeViolation) => void): void {
  for (const path of listSources(root)) {
    const text = readText(path);
    if (text === undefined) continue;
    const rel = relative(root, path);
    for (const banned of ["ai_setup_dialog.h", "AiSetupDialog::Show"]) {
      const index = text.indexOf(banned);
      if (index >= 0) {
        push({
          file: rel,
          line: lineOf(text, index),
          rule: "dialog-lifetime",
          message: `Do not use ${banned}; open organization settings instead so window teardown cannot strand a bubble. See docs/plans/roadmap.md T8.`,
        });
      }
    }
  }
  const controller = readText(join(root, "browser/tab_organization/tab_organization_controller.cc")) ?? "";
  if (controller && !controller.includes('ShowSettingsSubPage(window_.get(), "uwOrganization")')) {
    push({
      file: "browser/tab_organization/tab_organization_controller.cc",
      line: 1,
      rule: "dialog-lifetime",
      message: "OpenOrganizationSettings must open the uw settings subpage; the sidebar only links there.",
    });
  }
}

function checkHandlerScope(root: string, push: (v: NativeViolation) => void): void {
  for (const name of ["uw_organization_handler.cc", "uw_organization_handler.h"]) {
    const path = join(root, "browser/ui/webui/uw_organization", name);
    const text = readText(path);
    if (text === undefined) continue;
    const rel = relative(root, path);
    for (const banned of ["BrowserWindowInterface", "TabStripModel", "window_", "GetTabStripModel", "browser.h"]) {
      const index = text.indexOf(banned);
      if (index >= 0) {
        push({
          file: rel,
          line: lineOf(text, index),
          rule: "handler-scope",
          message: `Settings handler is profile-scoped and must not touch ${banned}; closing windows while settings is open must be safe.`,
        });
      }
    }
  }
  const handler = readText(join(root, "browser/ui/webui/uw_organization/uw_organization_handler.cc"));
  if (handler !== undefined) {
    const rel = "browser/ui/webui/uw_organization/uw_organization_handler.cc";
    if (!handler.includes("weak_factory_.GetWeakPtr()")) {
      push({
        file: rel,
        line: 1,
        rule: "handler-scope",
        message: "Async model requests must bind weak_factory_.GetWeakPtr() so a closed settings page cannot dangle.",
      });
    }
  }
}

function checkUrlAlias(root: string, push: (v: NativeViolation) => void): void {
  const path = join(root, "browser/ui/uw_url_rewrite.cc");
  const text = readText(path);
  if (text === undefined) return;
  const rel = relative(root, path);
  const require = (snippet: string, message: string): void => {
    if (!text.includes(snippet)) push({ file: rel, line: 1, rule: "url-alias", message });
  };
  require("SchemeIs(kUwScheme)", "HandleUwSchemeRewrite must only rewrite the uw scheme.");
  require("SetSchemeStr", "HandleUwSchemeRewrite must alias uw:// to the chrome scheme via replacements.");
  require("return false", "HandleUwSchemeRewrite must return false so WebUI still processes the rewritten URL.");
  if (text.includes("return true")) {
    push({
      file: rel,
      line: lineOf(text, text.indexOf("return true")),
      rule: "url-alias",
      message: "HandleUwSchemeRewrite must not return true; later handlers must see the rewritten URL.",
    });
  }
}

function checkKeyStore(root: string, push: (v: NativeViolation) => void): void {
  const path = join(root, "browser/tab_organization/api_key_store_stub.cc");
  const text = readText(path);
  if (text === undefined) return;
  const rel = relative(root, path);
  for (const banned of ["PrefService", "WriteFile", "base::File", "GetPrefs"]) {
    const index = text.indexOf(banned);
    if (index >= 0) {
      push({
        file: rel,
        line: lineOf(text, index),
        rule: "keystore-memory",
        message: `Stub key store must keep secrets in process memory; ${banned} would touch disk or prefs.`,
      });
    }
  }
  if (!text.includes("lock_guard") || !text.includes("NoDestructor")) {
    push({
      file: rel,
      line: 1,
      rule: "keystore-memory",
      message: "Stub key store must guard its map with a mutex and NoDestructor storage.",
    });
  }
}

export function checkNativeRules(repoRoot: string = REPO): NativeViolation[] {
  const violations: NativeViolation[] = [];
  const push = (v: NativeViolation): void => {
    violations.push(v);
  };
  checkReentrancy(repoRoot, push);
  checkAdapterTeardown(repoRoot, push);
  checkDialogLifetime(repoRoot, push);
  checkHandlerScope(repoRoot, push);
  checkUrlAlias(repoRoot, push);
  checkKeyStore(repoRoot, push);
  return violations.sort((a, b) => a.file.localeCompare(b.file) || a.line - b.line);
}

export function throwIfNativeViolations(repoRoot: string = REPO): void {
  const violations = checkNativeRules(repoRoot);
  if (violations.length > 0) {
    throw new Error(
      `Native lifetime checks failed:\n${violations.map((v) => `  ${v.file}:${v.line} [${v.rule}] ${v.message}`).join("\n")}`,
    );
  }
}
