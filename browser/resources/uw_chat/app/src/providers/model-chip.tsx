// A model choice: the provider's icon and the model's name. Its menu groups
// the models by provider and has a search field. It lists the models that
// write text, or for the decision model only those that make decisions.
// Where a chat runs on the model, one that calls no tools is shown but cannot
// be picked. See the frames "Chat: model and effort chips (Fluid)" and "Quick
// choices: every state (Fluid)" on the canvas page "AI providers".
import { Fragment, useState, type ReactNode } from "react";
import { ChevronDown, Gauge, Loader2, Plug } from "lucide-react";
import {
  DropdownContent,
  DropdownEmpty,
  DropdownLabel,
  DropdownMenu,
  DropdownSearch,
  DropdownSeparator,
  DropdownTrigger,
} from "@/components/ui/dropdown";
import { MenuItem } from "@/components/ui/menu-item";
import { cn } from "@/lib/utils";
import {
  Decides,
  type Model,
  type ModelList,
  type Provider,
  type ProviderPreset,
} from "./providers-api";
import { ProviderIcon } from "./provider-parts";

// How many models a provider shows before the search narrows them.
const FIRST_MODELS = 8;

export const CHIP =
  "inline-flex h-[30px] min-w-0 items-center gap-[7px] rounded-lg bg-muted px-2 font-medium text-foreground outline-none focus-visible:ring-2 focus-visible:ring-ring";

export function ModelChip({
  choice,
  providers,
  lists,
  unset,
  decisions,
  needsTools,
  gone,
  manage,
  className,
  onOpen,
  onPick,
  onClear,
}: {
  choice: { providerId: string; model: string } | null;
  // The providers that can serve this choice.
  providers: Provider[];
  lists: Record<string, ModelList | undefined>;
  // The row that empties the choice, and what the chip says then.
  unset?: string;
  // Lists the decision models instead of those that write text.
  decisions?: boolean;
  // A chat runs on the model, so it has to call tools.
  needsTools?: boolean;
  // The chosen model is no longer offered.
  gone?: boolean;
  // Ends the menu with a link to uw://ai/providers.
  manage?: boolean;
  className?: string;
  onOpen?: () => void;
  onPick: (provider: Provider, model: string) => void;
  onClear?: () => void;
}) {
  const [query, setQuery] = useState("");
  const chosen = choice
    ? providers.find((provider) => provider.id === choice.providerId)
    : undefined;
  const needle = query.trim().toLowerCase();
  const fits = (model: Model) =>
    decisions ? model.decides === Decides.kYes : model.writesText;

  let index = 0;
  let checkedIndex: number | undefined;
  const rows: ReactNode[] = [];
  if (unset && onClear && !needle) {
    if (!choice) {
      checkedIndex = index;
    }
    rows.push(
      <MenuItem
        key="unset"
        index={index++}
        label={unset}
        checked={!choice}
        onSelect={onClear}
      />,
      <DropdownSeparator key="unset-line" />,
    );
  }
  for (const provider of providers) {
    const list = lists[provider.id];
    const models = (list?.models ?? []).filter(
      (model) => fits(model) && model.name.toLowerCase().includes(needle),
    );
    // How many models uw is still asking whether they make decisions.
    const checking =
      decisions && list?.checking
        ? list.models.filter((model) => model.decides === Decides.kUnknown)
            .length
        : 0;
    if ((needle || (decisions && list?.ok)) && !models.length && !checking) {
      continue;
    }
    rows.push(
      <DropdownLabel key={provider.id} className="flex items-center gap-1.5">
        <ProviderIcon preset={provider.preset} className="size-3 opacity-70" />
        {provider.name}
      </DropdownLabel>,
    );
    if (!list) {
      rows.push(<DropdownEmpty key={`${provider.id}-wait`}>Listing models…</DropdownEmpty>);
      continue;
    }
    if (!list.ok) {
      rows.push(<DropdownEmpty key={`${provider.id}-error`}>{list.error}</DropdownEmpty>);
      continue;
    }
    // The first few, and the chosen one wherever it is.
    const shown = needle
      ? models
      : models.filter(
          (model, at) =>
            at < FIRST_MODELS ||
            (choice?.providerId === provider.id && choice.model === model.name),
        );
    rows.push(
      <Fragment key={`${provider.id}-models`}>
        {shown.map((model) => {
          const checked =
            choice?.providerId === provider.id && choice.model === model.name;
          if (checked) {
            checkedIndex = index;
          }
          const unusable = needsTools && !model.callsTools;
          return (
            <MenuItem
              key={model.name}
              index={index++}
              label={model.name}
              checked={checked}
              disabled={unusable}
              data-note={unusable ? "No tool calls" : undefined}
              className="after:ml-auto after:pl-2 after:text-[11.5px] after:text-destructive after:content-[attr(data-note)]"
              onSelect={() => onPick(provider, model.name)}
            />
          );
        })}
        {shown.length < models.length && (
          <DropdownEmpty>
            {models.length - shown.length} more · type to search
          </DropdownEmpty>
        )}
        {checking > 0 && (
          <DropdownEmpty className="flex items-center justify-center gap-2">
            <Loader2 aria-hidden className="size-3 animate-spin" />
            Checking {checking} model{checking === 1 ? "" : "s"}…
          </DropdownEmpty>
        )}
      </Fragment>,
    );
  }
  if (!rows.length) {
    rows.push(
      <DropdownEmpty key="none">
        {needle ? "No model matches" : "Add a provider first"}
      </DropdownEmpty>,
    );
  }
  if (manage) {
    rows.push(
      <DropdownSeparator key="manage-line" />,
      <MenuItem
        key="manage"
        index={index++}
        icon={Plug}
        label="Manage providers"
        onSelect={() => open("chrome://ai/providers")}
      />,
    );
  }
  const total = providers.reduce(
    (sum, provider) =>
      sum + (lists[provider.id]?.models.filter(fits).length ?? 0),
    0,
  );

  return (
    <DropdownMenu onOpenChange={(opened) => opened && onOpen?.()}>
      <DropdownTrigger
        render={
          <button type="button" className={cn(CHIP, className)}>
            {chosen && <ProviderIcon preset={chosen.preset} />}
            <span
              className={cn(
                "flex-1 truncate text-left",
                !choice && "text-muted-foreground",
                choice &&
                  (!chosen || gone) &&
                  "line-through decoration-muted-foreground",
              )}
            >
              {choice?.model ?? unset ?? "Pick a model"}
            </span>
            <ChevronDown aria-hidden className="size-3 text-muted-foreground" />
          </button>
        }
      />
      <DropdownContent checkedIndex={checkedIndex} className="max-h-[360px] w-[300px]">
        <DropdownSearch
          value={query}
          onValueChange={setQuery}
          placeholder={total > FIRST_MODELS ? `Search ${total} models` : "Search models"}
        />
        {rows}
      </DropdownContent>
    </DropdownMenu>
  );
}

// The levels of the chosen model, named as its provider names them, and the
// provider's own default. Shows nothing for a model that takes no levels.
export function EffortChip({
  efforts,
  effort,
  model,
  preset,
  className,
  onPick,
}: {
  efforts: string[];
  effort: string;
  // The model and its provider, which head the menu.
  model?: string;
  preset?: ProviderPreset;
  className?: string;
  onPick: (effort: string) => void;
}) {
  if (!efforts.length) {
    return null;
  }
  const levels = ["", ...efforts];
  const checkedIndex = Math.max(0, levels.indexOf(effort));
  return (
    <DropdownMenu>
      <DropdownTrigger
        render={
          <button type="button" aria-label="Effort" className={cn(CHIP, className)}>
            <Gauge aria-hidden className="size-3.5 text-muted-foreground" />
            <span className="flex-1 truncate text-left">{effort || "default"}</span>
            <ChevronDown aria-hidden className="size-3 text-muted-foreground" />
          </button>
        }
      />
      <DropdownContent checkedIndex={checkedIndex} className="w-[168px]">
        {model && (
          <DropdownLabel className="flex items-center gap-1.5">
            {preset !== undefined && (
              <ProviderIcon preset={preset} className="size-3 opacity-70" />
            )}
            <span className="truncate">{model}</span>
          </DropdownLabel>
        )}
        {levels.map((level, index) => (
          <MenuItem
            key={level || "default"}
            index={index}
            label={level || "default"}
            checked={index === checkedIndex}
            onSelect={() => onPick(level)}
          />
        ))}
      </DropdownContent>
    </DropdownMenu>
  );
}
