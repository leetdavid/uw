// uw://providers/<id>: one provider's address, key, format, and models. See
// the frame "Providers page: a provider open (Fluid)" on the canvas page
// "AI providers".
import { useState, type ReactNode } from "react";
import { Check, ChevronLeft, Search, Trash2, X } from "lucide-react";
import { Button } from "@/components/ui/button";
import { InputField, InputGroup } from "@/components/ui/input-group";
import {
  ModelSlot,
  ProviderPreset,
  providersHandler,
  type Provider,
} from "./providers-api";
import {
  FORMAT_NAMES,
  ProviderTile,
  SLOT_NAMES,
  type PageData,
} from "./provider-parts";

const FIRST_MODELS = 8;
const SLOTS = [ModelSlot.kLanguage, ModelSlot.kDecision, ModelSlot.kAgent];

// Where the tool calls and effort levels on the page come from.
function abilitiesSource(preset: ProviderPreset) {
  if (preset === ProviderPreset.kOllama) {
    return "Tool calls and thinking come from Ollama's own model details.";
  }
  if (preset === ProviderPreset.kCustom) {
    return "The models.dev catalog does not cover custom providers, so uw takes each model to call tools, with its format's usual effort levels.";
  }
  return "Tool calls and effort levels come from the models.dev catalog, which uw keeps on this Mac and refreshes once a day.";
}

function Row({ label, children }: { label: string; children: ReactNode }) {
  return (
    <div className="grid h-[50px] grid-cols-[110px_minmax(0,1fr)] items-center gap-3 pl-4 pr-3 [&+&]:border-t [&+&]:border-border">
      <span className="font-medium text-muted-foreground">{label}</span>
      {children}
    </div>
  );
}

export function ProviderPage({
  provider,
  data,
  onChanged,
}: {
  provider: Provider;
  data: PageData;
  onChanged: (relist?: string) => void;
}) {
  const [replacing, setReplacing] = useState(false);
  const [key, setKey] = useState("");
  const [query, setQuery] = useState("");
  const [all, setAll] = useState(false);
  const list = data.lists[provider.id];
  const usedBy = SLOTS.filter(
    (slot) => data.choices[slot]?.providerId === provider.id,
  );
  const usedFor = (model: string) =>
    usedBy.filter((slot) => data.choices[slot]?.model === model);
  const needle = query.trim().toLowerCase();
  const models = (list?.models ?? []).filter((model) =>
    model.name.toLowerCase().includes(needle),
  );
  const shown = all || needle ? models : models.slice(0, FIRST_MODELS);

  const saveKey = async () => {
    await providersHandler.replaceKey(provider.id, key);
    setKey("");
    setReplacing(false);
    onChanged(provider.id);
  };

  const remove = async () => {
    await providersHandler.removeProvider(provider.id);
    location.assign("/");
  };

  return (
    <>
      <a
        href="/"
        className="inline-flex h-6 items-center gap-1 text-[12.5px] font-medium text-muted-foreground"
      >
        <ChevronLeft aria-hidden className="size-3.5" />
        AI providers
      </a>
      <div className="mt-2.5 flex items-center gap-3.5">
        <ProviderTile preset={provider.preset} large />
        <div>
          <h1 className="text-[20px] font-semibold tracking-[-0.01em]">
            {provider.name}
          </h1>
          <div className="mt-0.5 text-[12.5px] text-muted-foreground">
            {usedBy.length
              ? `Used by the ${usedBy.map((slot) => SLOT_NAMES[slot].toLowerCase()).join(" and the ")}`
              : "No model of uw uses it yet"}
          </div>
        </div>
        <Button
          variant="ghost"
          leadingIcon={Trash2}
          className="ml-auto text-destructive hover:text-destructive"
          onClick={remove}
        >
          Remove provider
        </Button>
      </div>

      <h2 className="mb-2 mt-5 flex h-[30px] items-center text-[13px] font-semibold">
        Connection
      </h2>
      <div className="overflow-hidden rounded-xl border border-border">
        <Row label="Address">
          <span className="flex h-8 min-w-0 items-center rounded-lg bg-muted px-2.5">
            <span className="truncate">{provider.address}</span>
          </span>
        </Row>
        <Row label="Key">
          {replacing ? (
            <span className="flex items-center gap-2">
              <InputGroup className="flex-1">
                <InputField
                  index={0}
                  label="New key"
                  labelHidden
                  type="password"
                  placeholder="Paste the new key"
                  value={key}
                  onChange={setKey}
                  autoFocus
                />
              </InputGroup>
              <Button size="sm" disabled={!key} onClick={saveKey}>
                Save
              </Button>
              <Button size="sm" variant="ghost" onClick={() => setReplacing(false)}>
                Cancel
              </Button>
            </span>
          ) : (
            <span className="flex items-center gap-2">
              <span className="flex h-8 flex-1 items-center rounded-lg bg-muted px-2.5 text-muted-foreground">
                {provider.hasKey ? (
                  <span className="tracking-[2px]">••••••••••••••••••••</span>
                ) : (
                  "No key"
                )}
              </span>
              {provider.hasKey && (
                <span className="inline-flex items-center gap-1.5 whitespace-nowrap text-[12.5px] text-success">
                  <Check aria-hidden className="size-3.5" />
                  In Keychain
                </span>
              )}
              {provider.preset !== ProviderPreset.kOllama && (
                <Button size="sm" variant="secondary" onClick={() => setReplacing(true)}>
                  {provider.hasKey ? "Replace" : "Add a key"}
                </Button>
              )}
            </span>
          )}
        </Row>
        <Row label="Format">
          <span className="text-muted-foreground">
            {FORMAT_NAMES[provider.format]}
            <small className="ml-2 text-[12px]">
              {provider.preset === ProviderPreset.kCustom
                ? "Picked when it was added"
                : "Set by the provider"}
            </small>
          </span>
        </Row>
      </div>

      <div className="mb-2 mt-5 flex h-[30px] items-center gap-2">
        <h2 className="text-[13px] font-semibold">Models</h2>
        {list?.ok && <span className="text-muted-foreground">{list.models.length}</span>}
        <label className="ml-auto flex h-[30px] w-[220px] items-center gap-2 rounded-lg bg-muted px-2.5 text-muted-foreground">
          <Search aria-hidden className="size-3.5" />
          <input
            aria-label="Search models"
            placeholder="Search models"
            value={query}
            onChange={(event) => setQuery(event.target.value)}
            className="min-w-0 flex-1 bg-transparent text-foreground outline-none placeholder:text-muted-foreground"
          />
        </label>
      </div>
      <div className="overflow-hidden rounded-xl border border-border">
        {!list && <p className="px-4 py-3 text-muted-foreground">Listing models…</p>}
        {list && !list.ok && <p className="px-4 py-3 text-destructive">{list.error}</p>}
        {list?.ok && !models.length && (
          <p className="px-4 py-3 text-muted-foreground">
            {needle ? "No model matches." : "This provider lists no models."}
          </p>
        )}
        {shown.map((model) => (
          <div
            key={model.name}
            className="grid h-[46px] grid-cols-[minmax(0,1fr)_112px_292px] items-center gap-3 border-border pl-4 pr-3 [&+&]:border-t"
          >
            <span className="flex min-w-0 items-center gap-2 font-medium">
              <span className="truncate">{model.name}</span>
              {usedFor(model.name).map((slot) => (
                <span
                  key={slot}
                  className="shrink-0 rounded-md bg-[color-mix(in_srgb,var(--info)_8%,transparent)] px-1.5 py-0.5 text-[11.5px] font-medium text-info"
                >
                  {SLOT_NAMES[slot]}
                </span>
              ))}
            </span>
            {provider.servesLanguage && (
              <>
            <span className="flex items-center gap-1.5 whitespace-nowrap text-[12.5px] text-muted-foreground">
              {model.callsTools ? (
                <Check aria-hidden className="size-3.5 text-success" />
              ) : (
                <X aria-hidden className="size-3.5 text-destructive" />
              )}
              {model.callsTools ? "Calls tools" : "No tool calls"}
            </span>
            <span className="flex flex-nowrap gap-1 overflow-hidden">
              {model.efforts.length ? (
                model.efforts.map((effort) => (
                  <i
                    key={effort}
                    className="rounded-[5px] bg-muted px-1.5 py-0.5 text-[11.5px] not-italic text-muted-foreground"
                  >
                    {effort}
                  </i>
                ))
              ) : (
                <span className="text-[12px] text-muted-foreground">No effort levels</span>
              )}
            </span>
              </>
            )}
          </div>
        ))}
        {shown.length < models.length && (
          <button
            type="button"
            onClick={() => setAll(true)}
            className="flex h-10 w-full items-center justify-center border-t border-border text-[12.5px] font-medium text-muted-foreground"
          >
            {models.length - shown.length} more models
          </button>
        )}
      </div>
      {provider.servesLanguage && (
        <p className="mt-2.5 text-[12px] leading-[1.45] text-muted-foreground">
          {abilitiesSource(provider.preset)}
        </p>
      )}
    </>
  );
}
