#!/usr/bin/env node
/**
 * The Windows half of `pnpm run release`, run in a release checkout on the
 * Linux host that builds Windows uw. `build` makes the release's installer,
 * `symbols` uploads its symbols to Sentry, and `check` installs it in the
 * Windows VM, reads its versions, and runs the Google check there. See
 * "Distribution" in docs/plans/windows.md.
 */

import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { chmodSync, copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, renameSync, rmSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";
import { setTimeout as sleep } from "node:timers/promises";
import { parseArgs } from "node:util";
import { BuildError, Chromium, OUTPUT, REPO, layoutAt, loadPins, nativeRuntime } from "./chromium.ts";
import type { Runtime } from "./chromium.ts";
import {
  CRASH_REPORT_URL, POSTHOG_HOST, SENTRY_CLI_VERSION, SENTRY_ORG, SENTRY_PROJECT, WINDOWS_BUILT, WINDOWS_FAILED, WINDOWS_FEED_URL,
  WINDOWS_INSTALLER,
} from "./release.ts";

export const SENTRY_CLI_LINUX_SHA256 = "c958865f9d4000183509ea21b1f04925d5390de6d8d87386b9e69f2b305070c3";
export const NODE_WINDOWS_VERSION = "24.13.0";
export const NODE_WINDOWS_SHA256 = "ca2742695be8de44027d71b3f53a4bdb36009b95575fe1ae6f7f0b5ce091cb88";
// The Windows 11 VM on the build host. See "Testing" in docs/plans/windows.md.
const VM_SSH = ["-o", "BatchMode=yes", "-o", "ServerAliveInterval=60", "-i", join(homedir(), ".ssh/uw-windows"), "-p", "2222"];
const VM = "uw@127.0.0.1";
const VM_FOLDER = "C:\\uwt\\release";
const VM_NODE = `C:\\uwt\\node-v${NODE_WINDOWS_VERSION}-win-x64\\node.exe`;
const VM_UW = "$env:LOCALAPPDATA\\uwbrowser\\uw\\Application\\chrome.exe";
const UNINSTALL_KEY = "HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
const DEBUG_PORT = 19222;
const NETWORK_TIMEOUT = 600_000;

export interface WindowsBuild {
  version: string;
  size: number;
  sha256: string;
}

export class WindowsRelease {
  readonly version: string;
  readonly runtime: Runtime;
  private readonly directory: string;
  private readonly source = join(REPO, ".chromium/src");

  constructor(version: string, runtime = nativeRuntime()) {
    if (!/^\d+\.\d+\.\d+$/.test(version)) throw new BuildError("The version must look like 0.1.0", 2);
    this.version = version;
    this.runtime = runtime;
    this.directory = join(REPO, ".chromium/releases", version);
  }

  /** Builds the installer, numbered `patch`, and leaves WINDOWS_BUILT, or WINDOWS_FAILED with the reason. */
  build(patch: number, posthogKey: string, jobs: number): WindowsBuild {
    mkdirSync(this.directory, { recursive: true });
    for (const name of [WINDOWS_BUILT, WINDOWS_FAILED, WINDOWS_INSTALLER]) rmSync(join(this.directory, name), { force: true });
    try {
      // A build stopped part-way leaves chrome/VERSION numbered, which prepare refuses.
      this.runtime.run(["git", "checkout", "--", "chrome/VERSION"], { cwd: this.source });
      const pins = loadPins();
      new Chromium(layoutAt(join(REPO, ".chromium")), pins, this.runtime).build(jobs, true, true, {
        patch,
        args: [
          `uw_release_version = "${this.version}"`, `uw_crash_report_url = "${CRASH_REPORT_URL}"`,
          `uw_posthog_key = "${posthogKey}"`, `uw_posthog_host = "${POSTHOG_HOST}"`,
          `uw_update_feed_url = "${WINDOWS_FEED_URL}"`,
        ],
      });
      const installer = join(this.directory, WINDOWS_INSTALLER);
      copyFileSync(join(this.source, OUTPUT, "mini_installer.exe"), installer);
      const bytes = readFileSync(installer);
      const build = {
        version: [...pins.version.split(".").slice(0, 3), patch].join("."),
        size: bytes.length,
        sha256: createHash("sha256").update(bytes).digest("hex"),
      };
      writeFileSync(join(this.directory, WINDOWS_BUILT), JSON.stringify(build));
      return build;
    } catch (error) {
      writeFileSync(join(this.directory, WINDOWS_FAILED), error instanceof Error ? error.message : String(error));
      throw error;
    }
  }

  /** Uploads the shipped programs and their PDBs, so Sentry can read Windows crash reports. */
  symbols(token: string): void {
    if (!token) throw new BuildError("Give the Sentry token on standard input", 2);
    const output = join(this.source, OUTPUT);
    const files = readdirSync(output).filter((name) => /\.(exe|dll)$/.test(name) && existsSync(join(output, `${name}.pdb`)))
      .flatMap((name) => [name, `${name}.pdb`]);
    if (!files.includes("chrome.dll.pdb")) throw new BuildError(`${output} has no chrome.dll.pdb. Build with symbols first.`);
    this.runtime.run([this.sentryCli(), "debug-files", "upload", "--org", SENTRY_ORG, "--project", SENTRY_PROJECT, "--wait", ...files],
      { cwd: output, env: { ...this.runtime.env, SENTRY_AUTH_TOKEN: token }, inherit: true });
  }

  private sentryCli(): string {
    const cli = join(REPO, ".chromium/releases", `sentry-cli-linux-${SENTRY_CLI_VERSION}`);
    if (existsSync(cli)) return cli;
    const download = `${cli}.download`;
    this.runtime.run(["curl", "-fsSL", "-o", download,
      `https://github.com/getsentry/sentry-cli/releases/download/${SENTRY_CLI_VERSION}/sentry-cli-Linux-x86_64`],
    { timeout: NETWORK_TIMEOUT });
    this.pinned(download, SENTRY_CLI_LINUX_SHA256, `sentry-cli ${SENTRY_CLI_VERSION}`);
    chmodSync(download, 0o755);
    renameSync(download, cli);
    return cli;
  }

  private pinned(path: string, sha256: string, name: string): void {
    if (createHash("sha256").update(readFileSync(path)).digest("hex") !== sha256) {
      rmSync(path, { force: true });
      throw new BuildError(`${name} does not match its pinned checksum`);
    }
  }

  /** Runs PowerShell in the VM and returns its output. */
  vm(script: string, timeout = NETWORK_TIMEOUT): string {
    const encoded = Buffer.from(script, "utf16le").toString("base64");
    return this.runtime.run(["ssh", ...VM_SSH, VM, `powershell -NoProfile -NonInteractive -EncodedCommand ${encoded}`],
      { timeout });
  }

  private copyIn(files: string[], folder: string): void {
    this.runtime.run(["scp", "-q", "-O", ...VM_SSH.map((part) => part === "-p" ? "-P" : part), ...files,
      `${VM}:${folder.replaceAll("\\", "/")}/`], { timeout: NETWORK_TIMEOUT });
  }

  /** Installs the build in the VM over any uw there, checks its versions and a page, and runs the Google check. */
  async check(): Promise<void> {
    const build = JSON.parse(readFileSync(join(this.directory, WINDOWS_BUILT), "utf8")) as WindowsBuild;
    const chromium = loadPins().version;
    this.vm(`Stop-Process -Name chrome -Force -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force ${VM_FOLDER} -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force ${VM_FOLDER}\\scripts | Out-Null
foreach ($uw in Get-ChildItem ${UNINSTALL_KEY} | Get-ItemProperty | Where-Object { $_.DisplayName -eq 'uw' }) {
  $setup = [regex]::Match($uw.UninstallString, '^"([^"]+)"').Groups[1].Value
  Start-Process -Wait -FilePath $setup -ArgumentList '--uninstall', '--force-uninstall'
}`);
    this.copyIn([join(this.directory, WINDOWS_INSTALLER)], VM_FOLDER);
    const installed = this.vm(`$p = Start-Process -Wait -PassThru -FilePath ${VM_FOLDER}\\${WINDOWS_INSTALLER} -ArgumentList '--do-not-launch-chrome'
if ($p.ExitCode -ne 0) { "installer exit $($p.ExitCode)"; exit 1 }
$uw = Get-ChildItem ${UNINSTALL_KEY} | Get-ItemProperty | Where-Object { $_.DisplayName -eq 'uw' }
"$($uw.DisplayVersion) $((Get-Item "${VM_UW}").VersionInfo.ProductVersion)"`);
    if (installed !== `${this.version} ${build.version}`) {
      throw new BuildError(`Installed apps and uw.exe read "${installed}"; expected "${this.version} ${build.version}"`);
    }
    this.runtime.log(`Installed apps lists uw ${this.version}, and uw.exe is ${build.version}.`);
    await this.checkPages(`Version ${this.version} (Chromium ${chromium})`);
    await this.googleCheck();
  }

  // Starts uw in the VM's signed-in session, as a person would, and reads it over DevTools.
  private async checkPages(about: string): Promise<void> {
    this.vm(`$task = New-ScheduledTaskAction -Execute "${VM_UW}" -Argument "--remote-debugging-port=9222 --user-data-dir=${VM_FOLDER}\\profile --no-first-run"
Register-ScheduledTask -TaskName uwrelease -Action $task -Principal (New-ScheduledTaskPrincipal -UserId uw -LogonType Interactive) -Settings (New-ScheduledTaskSettingsSet -Priority 4 -ExecutionTimeLimit (New-TimeSpan -Seconds 0)) -Force | Out-Null
Start-ScheduledTask -TaskName uwrelease`);
    const tunnel = spawn("ssh", [...VM_SSH, "-N", "-L", `${DEBUG_PORT}:127.0.0.1:9222`, VM], { stdio: "ignore" });
    try {
      // Settings may not fetch, so read the strings the page loaded. The page collapses their double spaces.
      const shown = String(await evaluate("chrome://settings/help",
        "import('chrome://resources/js/load_time_data.js').then((m) => m.loadTimeData.getString('aboutBrowserVersion'))"))
        .replace(/\s+/g, " ");
      if (!shown.startsWith(about)) throw new BuildError(`About uw reads "${shown}"; expected "${about}"`);
      this.runtime.log(`About uw reads "${shown}".`);
      const title = await evaluate("data:text/html,<title>uw</title><script>document.title='uw release check'</script>", "document.title");
      if (title !== "uw release check") throw new BuildError(`A page's script did not run in uw; its title is "${title}"`);
      this.runtime.log("uw loads a page and runs its script.");
    } finally {
      tunnel.kill();
      this.vm(`Unregister-ScheduledTask -TaskName uwrelease -Confirm:$false
Stop-Process -Name chrome -Force -ErrorAction SilentlyContinue
exit 0`);
    }
  }

  private async googleCheck(): Promise<void> {
    if (this.vm(`Test-Path ${VM_NODE}`) !== "True") {
      const zip = join(REPO, ".chromium/releases", `node-v${NODE_WINDOWS_VERSION}-win-x64.zip`);
      if (!existsSync(zip)) {
        this.runtime.run(["curl", "-fsSL", "-o", zip,
          `https://nodejs.org/dist/v${NODE_WINDOWS_VERSION}/node-v${NODE_WINDOWS_VERSION}-win-x64.zip`], { timeout: NETWORK_TIMEOUT });
        this.pinned(zip, NODE_WINDOWS_SHA256, `Node ${NODE_WINDOWS_VERSION} for Windows`);
      }
      this.copyIn([zip], "C:\\uwt");
      this.vm(`Expand-Archive -Force C:\\uwt\\node-v${NODE_WINDOWS_VERSION}-win-x64.zip C:\\uwt`);
    }
    this.copyIn([join(REPO, "package.json")], VM_FOLDER);
    this.copyIn(["google-check.ts", "chromium.ts", "patches.ts", "source-links.ts"].map((name) => join(REPO, "scripts", name)),
      `${VM_FOLDER}\\scripts`);
    // Two browsers for five minutes in ssh's session run Windows out of desktop
    // heap there, and ssh then refuses every login until Windows restarts, so the
    // check runs in the signed-in session and leaves its output in files.
    this.vm(`Set-Content -Path ${VM_FOLDER}\\google-check.ps1 -Value @'
& ${VM_NODE} ${VM_FOLDER}\\scripts\\google-check.ts --binary "${VM_UW}" *> ${VM_FOLDER}\\google-check.log
Set-Content ${VM_FOLDER}\\google-check.exit $LASTEXITCODE
'@
$task = New-ScheduledTaskAction -Execute powershell.exe -Argument "-NoProfile -ExecutionPolicy Bypass -File ${VM_FOLDER}\\google-check.ps1"
Register-ScheduledTask -TaskName uwgooglecheck -Action $task -Principal (New-ScheduledTaskPrincipal -UserId uw -LogonType Interactive) -Settings (New-ScheduledTaskSettingsSet -Priority 4 -ExecutionTimeLimit (New-TimeSpan -Seconds 0)) -Force | Out-Null
Start-ScheduledTask -TaskName uwgooglecheck`);
    this.runtime.log("Running the Google check in the VM for five minutes...");
    for (let waited = 0; this.vm(`Test-Path ${VM_FOLDER}\\google-check.exit`) !== "True"; waited += 20) {
      if (waited > 900) throw new BuildError("The Google check in the VM did not finish within 15 minutes");
      await sleep(20_000);
    }
    this.runtime.log(this.vm(`Unregister-ScheduledTask -TaskName uwgooglecheck -Confirm:$false
Get-Content ${VM_FOLDER}\\google-check.log`));
    if (this.vm(`Get-Content ${VM_FOLDER}\\google-check.exit`) !== "0") throw new BuildError("The Google check failed in the VM");
  }
}

/** Evaluates `expression` in a new tab at `url` of the uw that DevTools reaches on DEBUG_PORT. */
async function evaluate(url: string, expression: string): Promise<unknown> {
  const endpoint = `http://127.0.0.1:${DEBUG_PORT}`;
  for (let tries = 0; ; tries++) {
    try { await fetch(`${endpoint}/json/version`); break; } catch {
      if (tries === 60) throw new BuildError("uw in the VM did not open DevTools within a minute");
      await sleep(1000);
    }
  }
  const target = await (await fetch(`${endpoint}/json/new?${url}`, { method: "PUT" })).json() as { webSocketDebuggerUrl: string };
  await sleep(3000);
  const socket = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve) => socket.addEventListener("open", resolve));
  socket.send(JSON.stringify({ id: 1, method: "Runtime.evaluate", params: { expression, awaitPromise: true, returnByValue: true } }));
  const reply = await new Promise<{ result?: { result?: { value?: unknown }; exceptionDetails?: unknown } }>((resolve) =>
    socket.addEventListener("message", (event) => resolve(JSON.parse(String(event.data)))));
  socket.close();
  if (reply.result?.exceptionDetails) throw new BuildError(`${url}: ${JSON.stringify(reply.result.exceptionDetails)}`);
  return reply.result?.result?.value;
}

const HELP = `The Windows half of pnpm run release, run in the release checkout on the Linux build host.

Usage: node scripts/windows-release.ts <command> <version> [options]
  build <version> --patch NUMBER --posthog-key KEY [--jobs NUMBER]
                    Build the installer, numbered 153.0.8010.<NUMBER>, into .chromium/releases/<version>/
  symbols <version> Upload the build's PDBs to Sentry, with the Sentry token on standard input
  check <version>   Install the build in the Windows VM, check its versions and a page, and run the Google check

Example:
  node scripts/windows-release.ts build 0.1.11 --patch 5312 --posthog-key phc_... --jobs 16
`;

export async function main(argv = process.argv.slice(2)): Promise<number> {
  try {
    const { values, positionals } = parseArgs({
      args: argv, strict: true, allowPositionals: true,
      options: {
        patch: { type: "string" }, "posthog-key": { type: "string" }, jobs: { type: "string" },
        help: { type: "boolean", short: "h" },
      },
    });
    const [command, version] = positionals;
    if (values.help || !command) { console.log(HELP); return values.help ? 0 : 2; }
    if (!version || positionals.length > 2) throw new BuildError("Give a command and one version", 2);
    const release = new WindowsRelease(version);
    if (command === "build") {
      const patch = Number(values.patch);
      const jobs = Number(values.jobs ?? "16");
      if (!Number.isSafeInteger(patch) || patch < 0) throw new BuildError("--patch must be a number", 2);
      if (!Number.isSafeInteger(jobs) || jobs < 1) throw new BuildError("--jobs must be a positive integer", 2);
      if (!/^phc_\w+$/.test(values["posthog-key"] ?? "")) throw new BuildError("--posthog-key must be PostHog's project token", 2);
      release.build(patch, values["posthog-key"]!, jobs);
    } else if (command === "symbols") {
      release.symbols(readFileSync(0, "utf8").trim());
    } else if (command === "check") {
      await release.check();
    } else {
      throw new BuildError("Choose build, symbols, or check", 2);
    }
    return 0;
  } catch (error) {
    console.error(`Error: ${error instanceof Error ? error.message : String(error)}`);
    return error instanceof BuildError ? error.exitCode : 1;
  }
}

if (import.meta.main) process.exitCode = await main();
