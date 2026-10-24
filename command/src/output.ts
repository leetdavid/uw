/** Where a command writes: stdout gets results, stderr gets progress and errors. */

export interface Output {
  json: boolean;
  /** Prints `value` as JSON with --json, otherwise the text `describe` gives. */
  data(value: unknown, describe: () => string): void;
  say(line: string): void;
}

export interface Streams {
  out: (text: string) => void;
  err: (text: string) => void;
}

export function output(streams: Streams, json: boolean): Output {
  return {
    json,
    data: (value, describe) => streams.out(json ? `${JSON.stringify(value)}\n` : describe()),
    say: (line) => streams.err(`${line}\n`),
  };
}
