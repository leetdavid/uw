// The / menu over the composer: the skills the user can start by typing / and
// a name. See the frame "Connectors and /: every state (Fluid)" and
// docs/plans/connectors.md#skills-only-you-start.
import {
  useEffect,
  useLayoutEffect,
  useState,
  type KeyboardEvent,
  type RefObject,
} from "react";
import { cn } from "@/lib/utils";
import type { ChatPrompt } from "@/chat-api";

export interface SlashItem {
  // What follows the /.
  command: string;
  description: string;
  // A tag such as "Only you".
  tag?: string;
  // What to type after the command.
  hint?: string;
  // A connector's prompt, whose arguments are fields above the composer.
  prompt?: { connector: string; prompt: ChatPrompt };
}

// A picked prompt's arguments, one field each. Enter sends.
export function PromptFields({
  prompt,
  values,
  onChange,
  onSubmit,
}: {
  prompt: ChatPrompt;
  values: Record<string, string>;
  onChange: (values: Record<string, string>) => void;
  onSubmit: () => void;
}) {
  if (!prompt.arguments.length) {
    return null;
  }
  return (
    <div id="prompt-fields" className="mb-2 flex gap-2">
      {prompt.arguments.map((argument) => (
        <label
          key={argument.name}
          className="min-w-0 flex-1 rounded-[9px] bg-accent px-2.5 py-1.5 focus-within:ring-[1.5px] focus-within:ring-info"
        >
          <span className="block font-mono text-[11px] text-muted-foreground">
            {argument.name}
            {argument.required && <span className="text-destructive"> *</span>}
          </span>
          <input
            data-argument={argument.name}
            value={values[argument.name] ?? ""}
            placeholder={argument.description}
            onChange={(event) =>
              onChange({ ...values, [argument.name]: event.target.value })
            }
            onKeyDown={(event) => {
              if (event.key === "Enter") {
                event.preventDefault();
                onSubmit();
              }
            }}
            className="block w-full bg-transparent text-[13px] outline-none placeholder:text-muted-foreground"
          />
        </label>
      ))}
    </div>
  );
}

// The prompt `draft` starts with, if any.
export function pickedPrompt(draft: string, items: SlashItem[]) {
  return items.find(
    (item) =>
      item.prompt &&
      (draft.trim() === `/${item.command}` || draft.startsWith(`/${item.command} `)),
  );
}

// Whether a required argument of `item` is empty.
export function missingArgument(item: SlashItem | undefined, values: Record<string, string>) {
  return Boolean(
    item?.prompt?.prompt.arguments.some(
      (argument) => argument.required && !values[argument.name]?.trim(),
    ),
  );
}

// The command being typed: "/" and no space yet.
const typing = (draft: string) => /^\/[\w:-]*$/.exec(draft)?.[0].slice(1);
// The command picked, and what follows it.
const picked = (draft: string) => /^\/([\w:-]+) (.*)$/s.exec(draft);

// Where a line of text in `field` ends, in pixels from its left edge.
function textWidth(field: HTMLTextAreaElement, text: string) {
  const canvas = document.createElement("canvas");
  const context = canvas.getContext("2d");
  if (!context) {
    return 0;
  }
  const style = getComputedStyle(field);
  context.font = `${style.fontWeight} ${style.fontSize} ${style.fontFamily}`;
  return context.measureText(text).width;
}

export function useSlashMenu(
  draft: string,
  setDraft: (draft: string) => void,
  items: SlashItem[],
) {
  const [highlight, setHighlight] = useState(0);
  const [dismissed, setDismissed] = useState("");
  const query = typing(draft);
  const shown =
    query === undefined || dismissed === draft
      ? []
      : items.filter((item) => item.command.startsWith(query));
  useEffect(() => setHighlight(0), [query]);

  const pick = (item: SlashItem) => setDraft(`/${item.command} `);
  // Runs before the composer's own keys, so Enter picks rather than sends.
  const onKeyDownCapture = (event: KeyboardEvent) => {
    if (!shown.length) {
      return;
    }
    if (event.key === "ArrowDown" || event.key === "ArrowUp") {
      const step = event.key === "ArrowDown" ? 1 : -1;
      setHighlight((highlight + step + shown.length) % shown.length);
    } else if (event.key === "Tab" || (event.key === "Enter" && !event.shiftKey)) {
      pick(shown[highlight]);
    } else if (event.key === "Escape") {
      setDismissed(draft);
    } else {
      return;
    }
    event.preventDefault();
    event.stopPropagation();
  };
  return { shown, highlight, pick, onKeyDownCapture };
}

export function SlashMenu({
  shown,
  highlight,
  onPick,
}: {
  shown: SlashItem[];
  highlight: number;
  onPick: (item: SlashItem) => void;
}) {
  if (!shown.length) {
    return null;
  }
  const groups = [
    { title: "Skills", items: shown.filter((item) => !item.command.includes(":")) },
    { title: "Prompts", items: shown.filter((item) => item.command.includes(":")) },
  ].filter((group) => group.items.length);
  return (
    <div
      id="slash-menu"
      role="listbox"
      aria-label="Skills and prompts"
      className="absolute inset-x-0 bottom-full z-20 mb-2 max-h-80 overflow-y-auto rounded-xl bg-popover p-1.5 shadow-lg ring-1 ring-border"
    >
      {groups.map((group) => (
        <div key={group.title}>
          <div className="px-2.5 pb-1 pt-2 text-[11px] font-semibold text-muted-foreground">
            {group.title}
          </div>
          {group.items.map((item) => (
            <button
              key={item.command}
              type="button"
              role="option"
              data-command={item.command}
              aria-selected={shown[highlight] === item}
              // Keeps the field focused.
              onMouseDown={(event) => event.preventDefault()}
              onClick={() => onPick(item)}
              className={cn(
                "flex w-full flex-col items-start rounded-lg px-2.5 py-1.5 text-left",
                shown[highlight] === item && "bg-accent",
              )}
            >
              <span className="flex items-center gap-2">
                <span className="font-mono text-[12.5px] font-medium">
                  /{item.command}
                </span>
                {item.tag && (
                  <span className="rounded-[5px] bg-info/10 px-1.5 text-[11px] font-medium text-info">
                    {item.tag}
                  </span>
                )}
              </span>
              <span className="w-full truncate text-xs text-muted-foreground">
                {item.description}
              </span>
            </button>
          ))}
        </div>
      ))}
    </div>
  );
}

// Over the field: a tint behind the picked command, and its hint after it
// until the user types more.
export function SlashCommandMark({
  draft,
  items,
  field,
}: {
  draft: string;
  items: SlashItem[];
  field: RefObject<HTMLElement | null>;
}) {
  const [place, setPlace] = useState<{
    left: number;
    top: number;
    command: number;
    height: number;
  } | null>(null);
  const match = picked(draft);
  const item = match && items.find((each) => each.command === match[1]);
  const command = item ? `/${item.command}` : "";
  useLayoutEffect(() => {
    const textarea = field.current?.querySelector("textarea");
    const box = field.current?.getBoundingClientRect();
    if (!command || !textarea || !box || draft.includes("\n")) {
      setPlace(null);
      return;
    }
    const style = getComputedStyle(textarea);
    const bounds = textarea.getBoundingClientRect();
    setPlace({
      left: bounds.left - box.left + parseFloat(style.paddingLeft),
      top: bounds.top - box.top + parseFloat(style.paddingTop),
      command: textWidth(textarea, command),
      height: parseFloat(style.lineHeight) || 22,
    });
  }, [command, draft, field]);
  if (!place || !item || !match) {
    return null;
  }
  return (
    <>
      <span
        aria-hidden
        className="pointer-events-none absolute rounded-md bg-info/15"
        style={{
          left: place.left - 3,
          top: place.top,
          width: place.command + 6,
          height: place.height,
        }}
      />
      {item.hint && !match[2] && (
        <span
          id="slash-hint"
          aria-hidden
          className="pointer-events-none absolute truncate text-muted-foreground"
          style={{
            left: place.left + place.command + 8,
            top: place.top,
            lineHeight: `${place.height}px`,
            right: 16,
          }}
        >
          {item.hint}
        </span>
      )}
    </>
  );
}
