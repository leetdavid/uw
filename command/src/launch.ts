/** Finds the running uw, or starts it with no window, and opens the channel to it. */

import { spawn } from "node:child_process";
import { readlinkSync, realpathSync } from "node:fs";
import { homedir } from "node:os";
import { join, sep } from "node:path";
import { setTimeout as sleep } from "node:timers/promises";
import { Channel } from "./channel.ts";
import { CommandError, FAILED } from "./errors.ts";

declare const UW_VERSION: string;

/** The build sets UW_VERSION to uw's version. Run from source, the command skips the version check. */
export const VERSION = typeof UW_VERSION === "string" ? UW_VERSION : "dev";
export const SOCKET = "uw-command.sock";
export const LINK = "/usr/local/bin/uw";
export const NOT_INSTALLED =
  "The uw command isn't installed. In uw, open Settings › System and click Install uw command.";
const START_WAIT_MS = 30_000;

export interface Place {
  /** The uw.app this command belongs to. */
  app: string | undefined;
  userData: string;
  defaultUserData: boolean;
  link: string;
}

/** The binary sits at uw.app/Contents/Helpers/uw. UW_APP and UW_USER_DATA_DIR point a run from source elsewhere. */
export function place(env: NodeJS.ProcessEnv = process.env, binary = process.execPath): Place {
  const own = realpathSync(binary).split(`${sep}Contents${sep}Helpers${sep}`);
  const userData = env.UW_USER_DATA_DIR;
  return {
    app: env.UW_APP ?? (own.length === 2 && own[0]?.endsWith(".app") ? own[0] : undefined),
    userData: userData ?? join(homedir(), "Library", "Application Support", "uw"),
    defaultUserData: userData === undefined,
    link: env.UW_COMMAND_LINK ?? LINK,
  };
}

export function installed(where: Place): boolean {
  if (!where.app) return false;
  try {
    return realpathSync(where.link).startsWith(realpathSync(where.app) + sep);
  } catch {
    return false;
  }
}

/** Chromium's SingletonLock names the host and process id of the uw using this user data folder. */
export function running(userData: string): boolean {
  let pid: number;
  try {
    pid = Number(readlinkSync(join(userData, "SingletonLock")).split("-").pop());
  } catch {
    return false;
  }
  try {
    process.kill(pid, 0);
    return true;
  } catch (error) {
    return (error as NodeJS.ErrnoException).code === "EPERM";
  }
}

export function startHidden(where: Place): void {
  const args = ["-g", "-j", "-a", where.app ?? "", "--args", "--no-startup-window"];
  if (!where.defaultUserData) args.push(`--user-data-dir=${where.userData}`);
  spawn("open", args, { detached: true, stdio: "ignore" }).unref();
}

export interface Reach {
  start?: (where: Place) => void;
  say?: (line: string) => void;
  waitMs?: number;
  /** This command's version; tests set it to check the comparison. */
  version?: string;
}

export async function reach(where: Place, options: Reach = {}): Promise<Channel> {
  const socket = join(where.userData, SOCKET);
  const channel = await Channel.open(socket).catch(() => undefined) ?? await startAndWait(where, socket, options);
  const mine = options.version ?? VERSION;
  const { version } = await channel.request("hello", { version: mine }) as { version: string };
  if (mine !== "dev" && version !== mine) {
    channel.close();
    throw new CommandError(`Restart uw to finish its update. uw ${version} is running; this command is ${mine}.`, FAILED);
  }
  return channel;
}

async function startAndWait(where: Place, socket: string, options: Reach): Promise<Channel> {
  if (!installed(where)) throw new CommandError(NOT_INSTALLED, FAILED);
  if (running(where.userData)) {
    throw new CommandError("uw is running but isn't answering the uw command. Quit uw, then try again.", FAILED);
  }
  options.say?.("Starting uw with no window…");
  (options.start ?? startHidden)(where);
  const deadline = Date.now() + (options.waitMs ?? START_WAIT_MS);
  while (Date.now() < deadline) {
    await sleep(100);
    const channel = await Channel.open(socket).catch(() => undefined);
    if (channel) return channel;
  }
  throw new CommandError("uw didn't start in time. Open uw, then try again.", FAILED);
}
