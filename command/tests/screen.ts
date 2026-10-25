/** A terminal for the terminal interface's tests: keys typed in, frames read back. */

import { EventEmitter } from "node:events";
import type { Screen } from "../src/tui.ts";

export const KEY = { enter: "\r", escape: "\u001B", down: "\u001B[B", up: "\u001B[A", tab: "\t", ctrlC: "\u0003", ctrlO: "\u000F" };

class Keys extends EventEmitter {
  isTTY = true;
  readonly #typed: string[] = [];

  type(text: string): void {
    this.#typed.push(text);
    this.emit("readable");
  }

  read = (): string | null => this.#typed.shift() ?? null;
  setEncoding(): void {}
  setRawMode(): void {}
  resume(): void {}
  pause(): void {}
  ref(): void {}
  unref(): void {}
}

class Frames extends EventEmitter {
  isTTY = true;
  columns = 110;
  rows = 40;
  readonly written: string[] = [];

  write = (text: string, encoding?: unknown, written?: () => void): boolean => {
    this.written.push(text);
    (typeof encoding === "function" ? encoding : written)?.();
    return true;
  };
}

export class FakeScreen {
  readonly stdin = new Keys();
  readonly stdout = new Frames();

  get screen(): Screen {
    return { stdin: this.stdin, stdout: this.stdout } as unknown as Screen;
  }

  /** Everything written, without colors or cursor moves. */
  all(): string {
    return this.#frames().join("");
  }

  /** The last frame drawn. */
  last(): string {
    return this.#frames().at(-1) ?? "";
  }

  #frames(): string[] {
    return this.stdout.written.map((text) => text.replace(/\u001B\[[0-9;?]*[A-Za-z]/g, "")).filter((text) => text.trim());
  }

  type(text: string): void {
    this.stdin.type(text);
  }
}

/** Lets Ink take in keys and draw. */
export const settle = (milliseconds = 60): Promise<void> => new Promise((resolve) => setTimeout(resolve, milliseconds));
