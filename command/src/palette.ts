/** uw's dark palette as 24-bit colors, whatever the terminal's theme. See the canvas guide "uw-command". */

export const COLOR = {
  text: "#E6E3F2",
  dim: "#8E8AA6",
  faint: "#5F5B75",
  primary: "#9DB0FF",
  green: "#81C995",
  amber: "#FCAD70",
  red: "#F28B82",
  fill: "#22202E",
  // White at 12% on uw's terminal ground, #16151E.
  hairline: "#323139",
  // The primary at 55%, the composer's edge.
  composer: "#606A9A",
  // The amber at 60%, an approval card's edge, and at 13%, its lit row.
  card: "#A0704F",
  lit: "#342929",
} as const;
