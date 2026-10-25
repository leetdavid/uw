/** Where a command writes: stdout gets results, stderr gets progress and errors. */

export interface Output {
  json: boolean;
  /** Prints `value` as JSON with --json, otherwise the text `describe` gives. */
  data(value: unknown, describe: () => string): void;
  /** A line of the result for a person; --json leaves it out. */
  line(text: string): void;
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
    line: (text) => {
      if (!json) streams.out(`${text}\n`);
    },
    say: (line) => streams.err(`${line}\n`),
  };
}
