// The chat's context and cost: the indicator at the end of its header and
// the card it opens. See the frames "Context and cost: the card (Fluid)" and
// "Context and cost: every state (Fluid)" on the canvas page "Agent chats",
// and docs/plans/context-and-cost.md.
import { useEffect, useRef, useState, type ReactNode } from "react";
import { Check, Info } from "lucide-react";
import { Tooltip as Hint } from "@/components/ui/tooltip";
import { cn } from "@/lib/utils";
import type { ChatUsage, MessageCost } from "./chat-api";
import { Tooltip, nearest, useHovered, useWidth } from "./chart";

// The parts of the context, in order, in the dataviz palette's light and dark
// steps.
const PARTS = [
  { key: "instructions", label: "Instructions and tools", color: "bg-[#2a78d6] dark:bg-[#3987e5]" },
  { key: "conversation", label: "Conversation", color: "bg-[#eb6834] dark:bg-[#d95926]" },
  { key: "results", label: "Pages and results", color: "bg-[#1baf7a] dark:bg-[#199e70]" },
  { key: "images", label: "Images", color: "bg-[#eda100] dark:bg-[#c98500]" },
] as const;

// 640, 9k, 124k, or 1.2M.
export function tokensText(tokens: number): string {
  if (tokens < 1000) {
    return String(Math.round(tokens));
  }
  if (tokens < 999_500) {
    return `${Math.round(tokens / 1000)}k`;
  }
  return `${(tokens / 1_000_000).toFixed(1).replace(/\.0$/, "")}M`;
}

// $10.73, or <$0.01 below a cent.
export function costText(cost: number): string {
  if (cost > 0 && cost < 0.005) {
    return "<$0.01";
  }
  return `$${cost.toFixed(2)}`;
}

// A price per million tokens: $3, $0.07, or $1.10.
function priceText(price: number): string {
  return `$${Number.isInteger(price) ? price : price.toFixed(2)}`;
}

function dayText(ms: number): string {
  return new Date(ms).toLocaleDateString([], { month: "short", day: "numeric" });
}

// Whether the chat shows a cost: some request had a price.
function hasCost(usage: ChatUsage): boolean {
  return usage.priced || usage.total > 0;
}

function share(usage: ChatUsage): number {
  return usage.limit > 0 ? usage.tokens / usage.limit : 0;
}

function Ring({ usage }: { usage: ChatUsage }) {
  const size = 15;
  const stroke = 2.25;
  const radius = (size - stroke) / 2;
  const around = 2 * Math.PI * radius;
  const full = Math.min(1, share(usage));
  return (
    <svg aria-hidden width={size} height={size} viewBox={`0 0 ${size} ${size}`} className="shrink-0 -rotate-90">
      <circle
        cx={size / 2}
        cy={size / 2}
        r={radius}
        fill="none"
        strokeWidth={stroke}
        strokeDasharray={usage.counted ? undefined : "2 2"}
        className="stroke-[#e6e4ee] dark:stroke-[#2e2e2e]"
      />
      {usage.counted && (
        <circle
          cx={size / 2}
          cy={size / 2}
          r={radius}
          fill="none"
          strokeWidth={stroke}
          strokeLinecap="round"
          strokeDasharray={`${around * full} ${around}`}
          className={full >= 0.8 ? "stroke-[#e8710a] dark:stroke-[#fcad70]" : "stroke-muted-foreground"}
        />
      )}
    </svg>
  );
}

// The words under the context bar: which limit it fills toward.
function limitLine(usage: ChatUsage): string {
  const limit = tokensText(usage.limit);
  if (usage.modelLimit) {
    return `${usage.model} holds ${limit} tokens, less than where uw makes room. uw shrinks the oldest page readings to stay under it.`;
  }
  const last = usage.madeRoom.at(-1);
  return `uw makes room at about ${limit} tokens by shrinking the oldest page readings and images.${
    last ? ` It last did at request ${last}.` : ""
  }`;
}

function SectionHead({ name, figure }: { name: string; figure: ReactNode }) {
  return (
    <div className="flex items-baseline">
      <h3 className="text-[13px] font-semibold">{name}</h3>
      <span className="ml-auto text-[12.5px] text-muted-foreground tabular-nums">{figure}</span>
    </div>
  );
}

function ContextSection({ usage }: { usage: ChatUsage }) {
  const full = share(usage);
  const total = PARTS.reduce((sum, part) => sum + usage[part.key], 0) || 1;
  return (
    <section className="px-4 py-3.5" data-section="context">
      <SectionHead
        name="Context"
        figure={
          <>
            <b className="font-semibold text-foreground">{tokensText(usage.tokens)}</b>{" "}
            {usage.modelLimit ? "of" : "of about"} {tokensText(usage.limit)} tokens
          </>
        }
      />
      <div className="mt-2.5 h-2.5 overflow-hidden rounded-full bg-[#ecebf0] dark:bg-[#262626]">
        <div className="flex h-full gap-[2px]" style={{ width: `${Math.min(1, full) * 100}%` }}>
          {PARTS.map(
            (part) =>
              usage[part.key] > 0 && (
                <span
                  key={part.key}
                  className={cn("h-full first:rounded-l-full last:rounded-r-full", part.color)}
                  style={{ flexGrow: usage[part.key] / total }}
                />
              ),
          )}
        </div>
      </div>
      <dl className="mt-2.5 grid grid-cols-2 gap-x-4 gap-y-1 text-[12px]">
        {PARTS.map((part) => (
          <div key={part.key} className="flex items-center gap-1.5">
            <span aria-hidden className={cn("size-2 shrink-0 rounded-[2px]", part.color)} />
            <dt className="whitespace-nowrap text-muted-foreground">{part.label}</dt>
            <dd className="ml-auto tabular-nums">{tokensText(usage[part.key])}</dd>
          </div>
        ))}
      </dl>
      <p
        className={cn(
          "mt-2.5 text-[12px] leading-relaxed",
          usage.modelLimit && full >= 0.8 ? "text-[#e8710a] dark:text-[#fcad70]" : "text-muted-foreground",
        )}
      >
        {limitLine(usage)}
      </p>
      {usage.perRequest.length > 1 && (
        <>
          <div className="mt-3 text-[11.5px] text-muted-foreground">Context per request</div>
          <ContextChart usage={usage} />
        </>
      )}
    </section>
  );
}

function CostRow({ label, amount, tokens, cost, split }: {
  label: string;
  amount: string;
  tokens?: number;
  cost: number;
  split: boolean;
}) {
  return (
    <div className="flex items-center text-[12.5px]" data-cost={label}>
      <span>{label}</span>
      <span className="ml-auto text-muted-foreground tabular-nums">
        {amount || (tokens !== undefined ? tokensText(tokens) : "")}
      </span>
      {split && <span className="w-[64px] text-right tabular-nums">{costText(cost)}</span>}
    </div>
  );
}

function CostSection({ usage }: { usage: ChatUsage }) {
  const priced = hasCost(usage);
  const split = priced && usage.split;
  const rows =
    usage.runCount > 0
      ? [
          {
            label: "Replies in this chat",
            amount: `${usage.agentRequests} ${usage.agentRequests === 1 ? "request" : "requests"}`,
            cost: usage.total - usage.runsCost,
          },
          { label: "Runs", amount: `${usage.runCount} ${usage.runCount === 1 ? "run" : "runs"}`, cost: usage.runsCost },
        ]
      : [
          { label: "Cache reads", tokens: usage.cacheRead, cost: usage.cacheReadCost },
          { label: "Cache writes", tokens: usage.cacheWrite, cost: usage.cacheWriteCost },
          { label: "Output, with thinking", tokens: usage.output, cost: usage.outputCost },
          { label: "Fresh input", tokens: usage.fresh, cost: usage.freshCost },
          ...(usage.checkCount
            ? [
                {
                  label: "Command checks",
                  amount: `${usage.checkCount} ${usage.checkCount === 1 ? "check" : "checks"}`,
                  cost: usage.checksCost,
                },
              ]
            : []),
          ...(usage.memoryCheckCount
            ? [
                {
                  label: "Memory checks",
                  amount: `${usage.memoryCheckCount} ${usage.memoryCheckCount === 1 ? "check" : "checks"}`,
                  cost: usage.memoryChecksCost,
                },
              ]
            : []),
        ]
          .filter((row) => ("tokens" in row ? row.tokens! > 0 : true))
          .sort((a, b) => (split ? b.cost - a.cost : (b.tokens ?? 0) - (a.tokens ?? 0)));
  return (
    <section className="border-t border-border px-4 py-3.5" data-section="cost">
      <SectionHead
        name="Cost"
        figure={
          priced ? (
            <>
              <b className="font-semibold text-foreground">{costText(usage.total)}</b>{" "}
              {usage.partial && usage.countedSince ? `since ${dayText(usage.countedSince)}` : "this chat"}
            </>
          ) : null
        }
      />
      {!priced && (
        <div className="mt-2.5 flex items-center gap-2 rounded-lg bg-accent px-2.5 py-1.5 text-[12.5px] text-muted-foreground">
          <Info aria-hidden className="size-3.5" />
          No price for this model
        </div>
      )}
      <div className="mt-2 flex flex-col gap-1.5">
        {rows.map((row) => (
          <CostRow
            key={row.label}
            label={row.label}
            amount={"amount" in row ? row.amount! : ""}
            tokens={"tokens" in row ? row.tokens : undefined}
            cost={row.cost}
            split={split}
          />
        ))}
      </div>
      {priced && usage.cacheSaved >= 0.01 && (
        <p className="mt-2.5 flex items-center gap-1.5 text-[12.5px] text-success">
          <Check aria-hidden className="size-3.5" />
          The cache saved {costText(usage.cacheSaved)}
        </p>
      )}
      {usage.partial && usage.countedSince > 0 && (
        <p className="mt-2 text-[12px] text-muted-foreground">
          uw counts from {dayText(usage.countedSince)}. Earlier requests aren't in these numbers.
        </p>
      )}
      {priced && usage.messages.length > 1 && (
        <>
          <div className="mt-3 text-[11.5px] text-muted-foreground">Cost per message you sent</div>
          <CostChart messages={usage.messages} />
        </>
      )}
    </section>
  );
}

function ModelFooter({ usage }: { usage: ChatUsage }) {
  return (
    <footer className="flex items-center gap-1.5 border-t border-border px-4 py-2.5 text-[12.5px]">
      <span className="truncate font-medium">{usage.model}</span>
      <span className="truncate text-muted-foreground">{usage.provider}</span>
      <span className="ml-auto shrink-0 text-muted-foreground tabular-nums">
        {usage.onThisMac
          ? "On this Mac"
          : usage.hasPrice
            ? `${priceText(usage.inputPrice)} in · ${priceText(usage.outputPrice)} out per 1M`
            : ""}
      </span>
    </footer>
  );
}

// Each request's context as a line and a wash, the limit dashed, and a ringed
// dot where uw made room and at the latest request.
function ContextChart({ usage }: { usage: ChatUsage }) {
  const [ref, width] = useWidth<HTMLDivElement>();
  const values = usage.perRequest;
  const { hovered, setHovered, onKeyDown } = useHovered(values.length);
  const height = 72;
  const limit = usage.limits.at(-1) ?? usage.limit;
  const top = Math.max(limit, ...values) * 1.08 || 1;
  const pad = 5;
  const x = (at: number) => pad + (at / Math.max(1, values.length - 1)) * Math.max(0, width - 2 * pad);
  const y = (tokens: number) => height - (tokens / top) * height;
  const points = values.map((tokens, at) => `${x(at)},${y(tokens)}`).join(" ");
  const marked = new Set([...usage.madeRoom.map((request) => request - 1), values.length - 1]);
  return (
    <div className="context-chart mt-1">
      <div
        ref={ref}
        role="img"
        tabIndex={0}
        aria-label="Context per request. Use the arrow keys to read each request."
        className="relative rounded-md outline-none focus-visible:ring-2 focus-visible:ring-ring"
        style={{ height }}
        onPointerMove={(event) => {
          const box = event.currentTarget.getBoundingClientRect();
          setHovered(nearest(values.map((_, at) => x(at)), event.clientX - box.left));
        }}
        onPointerLeave={() => setHovered(null)}
        onKeyDown={onKeyDown}
      >
        {width > 0 && (
          <svg width={width} height={height} className="overflow-visible">
            <line x1={0} x2={width} y1={y(limit / 2)} y2={y(limit / 2)} className="stroke-border" strokeWidth={1} />
            <line
              x1={0}
              x2={width}
              y1={y(limit)}
              y2={y(limit)}
              className="stroke-[#a3a3a3] dark:stroke-[#525252]"
              strokeWidth={1}
              strokeDasharray="4 4"
            />
            <text x={2} y={y(limit) - 4} className="fill-muted-foreground text-[10.5px]">
              {tokensText(limit)}
            </text>
            <polygon
              points={`${x(0)},${height} ${points} ${x(values.length - 1)},${height}`}
              className="fill-[#2a78d6]/[0.12] dark:fill-[#3987e5]/[0.12]"
            />
            <polyline
              points={points}
              fill="none"
              strokeWidth={2}
              strokeLinejoin="round"
              strokeLinecap="round"
              className="stroke-[#2a78d6] dark:stroke-[#3987e5]"
            />
            {hovered !== null && (
              <line x1={x(hovered)} x2={x(hovered)} y1={0} y2={height} className="stroke-border" strokeWidth={1} />
            )}
            {[...marked].map((at) => (
              <circle
                key={at}
                cx={x(at)}
                cy={y(values[at]!)}
                r={4}
                strokeWidth={2}
                className="fill-[#2a78d6] stroke-popover dark:fill-[#3987e5]"
              />
            ))}
          </svg>
        )}
        {hovered !== null && width > 0 && (
          <Tooltip
            x={x(hovered)}
            width={width}
            text={`Request ${hovered + 1} · ${tokensText(values[hovered]!)} tokens${
              usage.madeRoom.includes(hovered + 1) ? " · uw made room" : ""
            }`}
          />
        )}
      </div>
      <div className="mt-1 flex text-[10.5px] text-muted-foreground">
        <span>Request 1</span>
        <span className="ml-auto">{values.length}</span>
      </div>
    </div>
  );
}

// A round step for the gridline: 1, 2, or 5 times a power of ten, at or below
// `most`.
function gridStep(most: number): number {
  if (most <= 0) {
    return 0;
  }
  const power = 10 ** Math.floor(Math.log10(most));
  return [5, 2, 1].map((each) => each * power).find((step) => step <= most) ?? power;
}

// Each message's cost as a bar; hovering one says the message's start, its
// cost, and its requests.
function CostChart({ messages }: { messages: MessageCost[] }) {
  const [ref, width] = useWidth<HTMLDivElement>();
  const { hovered, setHovered, onKeyDown } = useHovered(messages.length);
  const height = 64;
  const most = Math.max(...messages.map((message) => message.cost));
  const grid = gridStep(most);
  const top = Math.max(most, grid) * 1.15 || 1;
  const slot = messages.length ? width / messages.length : 0;
  const bar = Math.max(4, Math.min(22, slot - 6));
  const x = (at: number) => slot * at + slot / 2;
  const y = (cost: number) => height - (cost / top) * height;
  const label = (message: MessageCost) => {
    const start = message.text.length > 36 ? `${message.text.slice(0, 36)}…` : message.text;
    return `“${start}” ${costText(message.cost)} · ${message.requests} ${message.requests === 1 ? "request" : "requests"}`;
  };
  return (
    <div className="cost-chart mt-1">
      <div
        ref={ref}
        role="img"
        tabIndex={0}
        aria-label="Cost per message you sent. Use the arrow keys to read each message."
        className="relative rounded-md outline-none focus-visible:ring-2 focus-visible:ring-ring"
        style={{ height }}
        onPointerMove={(event) => {
          const box = event.currentTarget.getBoundingClientRect();
          setHovered(nearest(messages.map((_, at) => x(at)), event.clientX - box.left));
        }}
        onPointerLeave={() => setHovered(null)}
        onKeyDown={onKeyDown}
      >
        {width > 0 && (
          <svg width={width} height={height} className="overflow-visible">
            {grid > 0 && (
              <>
                <line x1={0} x2={width} y1={y(grid)} y2={y(grid)} className="stroke-border" strokeWidth={1} />
                <text x={0} y={y(grid) - 3} className="fill-muted-foreground text-[10.5px]">
                  {costText(grid)}
                </text>
              </>
            )}
            <line x1={0} x2={width} y1={height} y2={height} className="stroke-border" strokeWidth={1} />
            {messages.map((message, at) => {
              const top = y(message.cost);
              const tall = height - top;
              return (
                <path
                  key={at}
                  data-bar={at + 1}
                  d={
                    tall > 4
                      ? `M${x(at) - bar / 2},${height} V${top + 4} q0,-4 4,-4 H${x(at) + bar / 2 - 4} q4,0 4,4 V${height} Z`
                      : `M${x(at) - bar / 2},${height} V${top} H${x(at) + bar / 2} V${height} Z`
                  }
                  className="fill-[#2a78d6] dark:fill-[#3987e5]"
                  opacity={hovered === null || hovered === at ? 1 : 0.55}
                />
              );
            })}
          </svg>
        )}
        {hovered !== null && width > 0 && <Tooltip x={x(hovered)} width={width} text={label(messages[hovered]!)} />}
      </div>
      {messages.length <= 12 && (
        <div className="relative mt-1 h-3.5 text-[10.5px] text-muted-foreground">
          {messages.map((_, at) => (
            <span key={at} className="absolute -translate-x-1/2" style={{ left: x(at) }}>
              {at + 1}
            </span>
          ))}
        </div>
      )}
    </div>
  );
}

// The indicator, and the card under it while it is open. Hidden until the
// chat has answered.
export function UsageIndicator({ usage }: { usage: ChatUsage | null }) {
  const [open, setOpen] = useState(false);
  const box = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!open) {
      return;
    }
    const close = (event: PointerEvent | KeyboardEvent) => {
      if (event instanceof KeyboardEvent ? event.key === "Escape" : !box.current?.contains(event.target as Node)) {
        setOpen(false);
      }
    };
    document.addEventListener("pointerdown", close);
    document.addEventListener("keydown", close);
    return () => {
      document.removeEventListener("pointerdown", close);
      document.removeEventListener("keydown", close);
    };
  }, [open]);
  if (!usage || (!usage.counted && !usage.answered)) {
    return null;
  }
  const priced = hasCost(usage);
  const percent = Math.round(share(usage) * 100);
  const hint = usage.counted
    ? `${tokensText(usage.tokens)} ${usage.modelLimit ? "of" : "of about"} ${tokensText(usage.limit)} tokens (${percent}%)${
        priced ? ` · ${costText(usage.total)} this chat` : ""
      }`
    : "The model's provider says nothing of the tokens it used.";
  return (
    <div ref={box} className="relative shrink-0">
      <Hint content={hint} side="bottom" forceOpen={open ? false : undefined}>
        <button
          id="usage"
          type="button"
          aria-expanded={open}
          aria-label={`Context and cost: ${hint}`}
          disabled={!usage.counted}
          className={cn(
            "flex h-7 items-center gap-1.5 rounded-lg px-2 text-[12.5px] text-muted-foreground tabular-nums hover:bg-hover",
            open && "bg-active text-foreground",
          )}
          onClick={() => setOpen(!open)}
        >
          <Ring usage={usage} />
          {usage.counted ? tokensText(usage.tokens) : "—"}
          {usage.counted && priced && (
            <>
              <span aria-hidden className="text-muted-foreground/60">
                ·
              </span>
              {costText(usage.total)}
            </>
          )}
        </button>
      </Hint>
      {open && (
        <div
          id="usage-card"
          role="dialog"
          aria-label="Context and cost"
          className="absolute top-[calc(100%+8px)] right-0 z-30 w-[380px] overflow-hidden rounded-[14px] bg-popover text-foreground shadow-lg ring-1 ring-border"
        >
          <ContextSection usage={usage} />
          <CostSection usage={usage} />
          <ModelFooter usage={usage} />
        </div>
      )}
    </div>
  );
}
