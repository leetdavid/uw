// A model choice: the provider's icon and the model's name. Its menu groups
// the models by provider and has a search field. See the frame "Chat: model
// and effort chips (Fluid)" on the canvas page "AI providers".
import { Fragment, useState, type ReactNode } from "react";
import { ChevronDown } from "lucide-react";
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
import { ModelSlot, type Provider } from "./providers-api";
import {
  ProviderIcon,
  providerOf,
  servesSlot,
  type PageData,
} from "./provider-parts";

// What the chip says while the slot is empty, and the menu row that empties
// it. The language model has no such row.
const UNSET: Record<ModelSlot, string | null> = {
  [ModelSlot.kLanguage]: null,
  [ModelSlot.kDecision]: "None",
  [ModelSlot.kAgent]: "Same as the language model",
};

export function ModelChip({
  slot,
  data,
  onPick,
  onClear,
}: {
  slot: ModelSlot;
  data: PageData;
  onPick: (provider: Provider, model: string) => void;
  onClear: () => void;
}) {
  const [query, setQuery] = useState("");
  const choice = data.choices[slot];
  const chosen = providerOf(data, choice);
  const unset = UNSET[slot];
  const providers = data.providers.filter((provider) =>
    servesSlot(provider, slot),
  );

  let index = 0;
  let checkedIndex: number | undefined;
  const rows: ReactNode[] = [];
  const needle = query.trim().toLowerCase();
  if (unset && !needle) {
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
    const list = data.lists[provider.id];
    const models = (list?.models ?? []).filter((model) =>
      model.toLowerCase().includes(needle),
    );
    if (needle && !models.length) {
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
    rows.push(
      <Fragment key={`${provider.id}-models`}>
        {models.map((model) => {
          const checked =
            choice?.providerId === provider.id && choice.model === model;
          if (checked) {
            checkedIndex = index;
          }
          return (
            <MenuItem
              key={model}
              index={index++}
              label={model}
              checked={checked}
              onSelect={() => onPick(provider, model)}
            />
          );
        })}
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

  return (
    <DropdownMenu>
      <DropdownTrigger
        render={
          <button
            type="button"
            className="inline-flex h-[30px] w-full min-w-0 items-center gap-[7px] rounded-lg bg-muted px-2 font-medium text-foreground outline-none focus-visible:ring-2 focus-visible:ring-ring"
          >
            {chosen && <ProviderIcon preset={chosen.preset} />}
            <span
              className={cn(
                "flex-1 truncate text-left",
                !choice && "text-muted-foreground",
                choice && !chosen && "line-through decoration-muted-foreground",
              )}
            >
              {choice?.model ?? unset ?? "Pick a model"}
            </span>
            <ChevronDown aria-hidden className="size-3 text-muted-foreground" />
          </button>
        }
      />
      <DropdownContent
        checkedIndex={checkedIndex}
        className="max-h-[360px] w-[280px]"
      >
        <DropdownSearch
          value={query}
          onValueChange={setQuery}
          placeholder="Search models"
        />
        {rows}
      </DropdownContent>
    </DropdownMenu>
  );
}
