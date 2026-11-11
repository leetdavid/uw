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

/** Rows in columns, each two spaces wider than its longest cell. */
export function table(rows: string[][]): string {
  const widths = rows[0]!.slice(0, -1).map((_, column) => Math.max(...rows.map((row) => row[column]!.length)) + 2);
  return rows.map((row) => row.map((cell, column) => cell.padEnd(widths[column] ?? 0)).join("").trimEnd()).join("\n") + "\n";
}

/** A title cut to 40 characters. */
export function cut(title: string): string {
  return title.length > 40 ? `${title.slice(0, 39)}…` : title;
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
