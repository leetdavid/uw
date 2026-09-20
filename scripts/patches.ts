/** Ordered Chromium patches, applied without touching the user's Git index. */
import { existsSync, mkdtempSync, readFileSync, renameSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import type { Runner } from "./chromium.ts";

interface PatchState {
  phase: "applying" | "applied" | "unapplying";
  diff: string;
}

export class PatchSet {
  readonly source: string;
  readonly directory: string;
  readonly state: string;
  readonly run: Runner;
  readonly environment: NodeJS.ProcessEnv;

  constructor(source: string, directory: string, state: string, run: Runner, environment = process.env) {
    this.source = source;
    this.directory = directory;
    this.state = state;
    this.run = run;
    this.environment = environment;
  }

  private git(args: string[], input?: string, env?: NodeJS.ProcessEnv): string {
    return this.run(["git", "-C", this.source, ...args], {
      input, env: { ...this.environment, ...env }, rawOutput: true, timeout: 300_000,
    });
  }

  private readonly diffArgs = [
    "diff", "--binary", "--full-index", "--no-ext-diff", "--no-textconv", "--no-renames", "--no-color",
    "--src-prefix=a/", "--dst-prefix=b/", "--ignore-submodules=all",
  ];

  private readState(): PatchState | undefined {
    if (!existsSync(this.state)) return undefined;
    const state: unknown = JSON.parse(readFileSync(this.state, "utf8"));
    if (typeof state !== "object" || state === null || !("diff" in state) || typeof state.diff !== "string" ||
        !("phase" in state) ||
        (state.phase !== "applying" && state.phase !== "applied" && state.phase !== "unapplying")) {
      throw new Error(`${this.state}: invalid patch record. Preserve it and the checkout before recovering.`);
    }
    return { phase: state.phase, diff: state.diff };
  }

  private record(phase: PatchState["phase"], diff: string): void {
    writeFileSync(`${this.state}.tmp`, JSON.stringify({ phase, diff }));
    renameSync(`${this.state}.tmp`, this.state);
  }

  /** Accept managed changes and known file states of an interrupted operation. */
  check(): string {
    const diff = this.git([...this.diffArgs, "HEAD"]);
    const recorded = this.readState();
    const files = new Map(recorded?.diff.split(/(?=^diff --git )/m)
      .map((file) => [file.split("\n", 1)[0], file]));
    const interrupted = recorded && recorded.phase !== "applied" && (!diff ||
      diff.split(/(?=^diff --git )/m).every((file) => {
        const header = file.split("\n", 1)[0]!;
        // Git can remove an old path before writing its replacement. Reversing
        // this deletion restores HEAD before the complete series is retried.
        return file === files.get(header) ||
          (files.has(header) && file.startsWith(`${header}\ndeleted file mode `));
      }));
    if (!(diff === (recorded?.diff ?? "") || interrupted) ||
        this.git(["diff", "--cached", "--name-only", "--ignore-submodules=none"]) ||
        this.git(["ls-files", "--others", "--exclude-standard"])) {
      throw new Error(`${this.source} has local changes beyond the recorded uw patches. Preserve them before running prepare or sync.`);
    }
    return diff;
  }

  private expectedDiff(): string {
    const names = readFileSync(join(this.directory, "series"), "utf8").split("\n")
      .map((line) => line.trim()).filter((line) => line && !line.startsWith("#"));
    if (!names.length || new Set(names).size !== names.length ||
        names.some((name) => !/^[a-z0-9][a-z0-9-]*\.patch$/.test(name))) {
      throw new Error(`${this.directory}/series: expected unique patch filenames in application order`);
    }
    const temp = mkdtempSync(join(tmpdir(), "uw-patch-index-"));
    try {
      const env = { GIT_INDEX_FILE: join(temp, "index") };
      this.git(["read-tree", "HEAD"], undefined, env);
      for (const name of names) {
        try {
          this.git(["apply", "--cached", "--whitespace=error", "-"], readFileSync(join(this.directory, name), "utf8"), env);
        } catch (error) {
          throw new Error(`${name} could not be prepared. Check the patch against this Chromium revision.\n` +
            (error instanceof Error ? error.message : String(error)), { cause: error });
        }
      }
      if (this.git(["diff", "--cached", "--name-only", "--diff-filter=A", "HEAD"], undefined, env)) {
        throw new Error("The patch runner only supports changes to tracked Chromium files. Keep new product source outside the patch set.");
      }
      return this.git([...this.diffArgs, "--cached", "HEAD"], undefined, env);
    } finally {
      rmSync(temp, { recursive: true, force: true });
    }
  }

  apply(): void {
    const current = this.check();
    // Validate the whole series before changing any source file.
    const expected = this.expectedDiff();
    if (current === expected) {
      if (!current) rmSync(this.state, { force: true });
      else if (this.readState()?.phase !== "applied") this.record("applied", expected);
      return;
    }
    this.unapply();
    if (!expected) return;
    this.git(["apply", "--check", "--whitespace=error", "-"], expected);
    this.record("applying", expected);
    this.git(["apply", "--whitespace=error", "-"], expected);
    this.record("applied", expected);
  }

  unapply(): void {
    const current = this.check();
    if (current) {
      this.git(["apply", "--reverse", "--check", "-"], current);
      this.record("unapplying", current);
      this.git(["apply", "--reverse", "-"], current);
    }
    rmSync(this.state, { force: true });
  }

  verify(): void {
    if (this.check() !== this.expectedDiff()) {
      throw new Error("Chromium customizations are out of date. Run pnpm run prepare:chromium, then pnpm run build.");
    }
  }
}
