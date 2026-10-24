/** Exit codes, as the uw command plan defines them. */

export const DONE = 0;
export const FAILED = 1;
export const USAGE = 2;
export const NEEDS_YOU = 3;

export class CommandError extends Error {
  readonly exit: number;

  constructor(message: string, exit: number) {
    super(message);
    this.exit = exit;
  }
}

/** uw answers a request it can't carry out with an error whose code is the exit code. */
export function exitFor(code: number): number {
  return code === USAGE || code === NEEDS_YOU ? code : FAILED;
}
