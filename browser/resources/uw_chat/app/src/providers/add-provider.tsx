// The Add provider dialog: the preset tiles and a Custom provider. uw lists
// the address's models before it adds the provider. See the frame
// "Providers page: add a provider (Fluid)" on the canvas page "AI providers".
import { Loader2 } from "lucide-react";
import { useState } from "react";
import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogContent,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { InputField, InputGroup } from "@/components/ui/input-group";
import { RadioGroup, RadioItem } from "@/components/ui/radio-group";
import { cn } from "@/lib/utils";
import {
  PlanSignInOutcome,
  ProviderFormat,
  ProviderPreset,
  providersHandler,
  type Provider,
} from "./providers-api";
import {
  CHATGPT_BUTTON,
  FORMAT_NAMES,
  OpenAiMark,
  PRESET_NAMES,
  ProviderIcon,
} from "./provider-parts";

const PRESETS = [
  ProviderPreset.kOpenAi,
  ProviderPreset.kAnthropic,
  ProviderPreset.kOpenRouter,
  ProviderPreset.kVercelAiGateway,
  ProviderPreset.kOllama,
  ProviderPreset.kJev,
  ProviderPreset.kOpenCodeGo,
  ProviderPreset.kCodex,
  ProviderPreset.kCustom,
];

export const NO_PLAN =
  "This ChatGPT account has no plan uw can use. Codex needs ChatGPT Plus or Pro.";

export const ADDRESS_RULE =
  "This address cannot be used. It must start with https://, or with http:// for this Mac or your own network.";

const FORMATS = [
  ProviderFormat.kChatCompletions,
  ProviderFormat.kAnthropicMessages,
  ProviderFormat.kOpenAiResponses,
  ProviderFormat.kEachModel,
  ProviderFormat.kOpenAiDecisions,
  ProviderFormat.kSystemOne,
];

export function AddProviderDialog({
  open,
  onOpenChange,
  providers,
  presetAddresses,
  onAdded,
}: {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  providers: Provider[];
  presetAddresses: Partial<Record<ProviderPreset, string>>;
  onAdded: () => void;
}) {
  const [preset, setPreset] = useState(ProviderPreset.kOpenAi);
  const [name, setName] = useState("");
  const [address, setAddress] = useState(
    presetAddresses[ProviderPreset.kOpenAi] ?? "",
  );
  const [format, setFormat] = useState(ProviderFormat.kChatCompletions);
  const [key, setKey] = useState("");
  const [busy, setBusy] = useState(false);
  const [signingIn, setSigningIn] = useState(false);
  const [error, setError] = useState("");
  const custom = preset === ProviderPreset.kCustom;
  const codex = preset === ProviderPreset.kCodex;

  const pick = (next: ProviderPreset) => {
    if (signingIn) {
      return;
    }
    setPreset(next);
    setAddress(presetAddresses[next] ?? "");
    setError("");
  };

  const close = (next: boolean) => {
    if (!next && signingIn) {
      providersHandler.cancelPlanSignIn();
    }
    onOpenChange(next);
  };

  const continueWithChatGpt = async () => {
    setSigningIn(true);
    setError("");
    const { outcome, message } = await providersHandler.signInWithChatGpt("");
    setSigningIn(false);
    if (outcome === PlanSignInOutcome.kSignedIn) {
      onAdded();
      onOpenChange(false);
    } else if (outcome === PlanSignInOutcome.kNoPlan) {
      setError(NO_PLAN);
    } else if (outcome !== PlanSignInOutcome.kCancelled) {
      setError(message);
    }
  };

  const add = async () => {
    setBusy(true);
    setError("");
    const provider = { preset, name, address, format, apiKey: key };
    const { list } = await providersHandler.listModelsAt(provider);
    if (!list.ok) {
      setBusy(false);
      setError(
        list.error
          ? `uw could not list the models at this address: ${list.error}`
          : "uw could not list the models at this address.",
      );
      return;
    }
    const { id } = await providersHandler.addProvider(provider);
    setBusy(false);
    if (!id) {
      setError(ADDRESS_RULE);
      return;
    }
    setName("");
    setAddress(presetAddresses[preset] ?? "");
    setKey("");
    onAdded();
    onOpenChange(false);
  };

  return (
    <Dialog open={open} onOpenChange={close}>
      <DialogContent size="lg">
        <DialogHeader>
          <DialogTitle>Add provider</DialogTitle>
        </DialogHeader>
        <div className="grid grid-cols-3 gap-2">
          {PRESETS.map((each) => (
            <button
              key={each}
              type="button"
              onClick={() => pick(each)}
              className={cn(
                "flex min-h-[62px] flex-col items-center justify-center gap-[5px] whitespace-nowrap rounded-[10px] border border-border py-2 text-[12.5px] font-medium outline-none",
                each === preset &&
                  "border-[color-mix(in_srgb,var(--focus-ring)_55%,transparent)] ring-[3px] ring-[color-mix(in_srgb,var(--focus-ring)_12%,transparent)]",
              )}
            >
              <ProviderIcon preset={each} className="size-[18px]" />
              {PRESET_NAMES[each]}
              {each !== ProviderPreset.kCustom &&
                providers.some((saved) => saved.preset === each) && (
                  <small className="-mt-[3px] text-[11px] font-normal text-muted-foreground">
                    Added
                  </small>
                )}
            </button>
          ))}
        </div>
        {codex && (
          <div className="mt-4 rounded-[10px] bg-muted px-4 py-3.5">
            <p className="text-[13.5px] font-semibold">Your ChatGPT Plus or Pro plan</p>
            <p className="mt-1 text-[12.5px] leading-normal text-muted-foreground">
              Chats, task names, and quick choices on Codex's models count
              against your plan's limits, the same as Codex does. Sign-in opens
              in a new tab.
            </p>
          </div>
        )}
        <InputGroup className={cn("mt-4", codex && "hidden")}>
          {custom && (
            <>
              <InputField
                index={0}
                label="Name"
                placeholder="LiteLLM proxy"
                value={name}
                onChange={setName}
              />
            </>
          )}
          <InputField
            index={1}
            label="Address"
            placeholder="http://localhost:4000/v1"
            value={address}
            onChange={setAddress}
          />
          {preset !== ProviderPreset.kOllama && (
            <InputField
              index={2}
              label={custom ? "Key (not needed for a local address)" : "Key"}
              type="password"
              placeholder="Paste the key"
              value={key}
              onChange={setKey}
            />
          )}
        </InputGroup>
        {custom && (
          <div className="mt-4">
            <div className="mb-1.5 text-[12.5px] font-medium text-muted-foreground">
              Format
            </div>
            <RadioGroup
              value={String(format)}
              onValueChange={(value) => setFormat(Number(value) as ProviderFormat)}
            >
              {FORMATS.map((each, index) => (
                <RadioItem
                  key={each}
                  index={index}
                  value={String(each)}
                  label={FORMAT_NAMES[each]}
                />
              ))}
            </RadioGroup>
            <p className="mt-1.5 text-[12px] text-muted-foreground">
              "Each model's own" sends each model over its maker's format, for
              a proxy such as CLIProxyAPI. The last two serve decision models
              only.
            </p>
          </div>
        )}
        {error && (
          <p role="alert" className="mt-4 text-[12.5px] text-destructive">
            {error}
          </p>
        )}
        <DialogFooter>
          {signingIn && (
            <span className="mr-auto flex items-center gap-2 text-[12.5px] text-muted-foreground">
              <Loader2 aria-hidden className="size-3.5 animate-spin" />
              Finish signing in in the tab that opened
            </span>
          )}
          <Button variant="ghost" onClick={() => close(false)}>
            Cancel
          </Button>
          {codex ? (
            <Button
              disabled={signingIn}
              leadingIcon={OpenAiMark}
              className={CHATGPT_BUTTON}
              onClick={continueWithChatGpt}
            >
              Continue with ChatGPT
            </Button>
          ) : (
            <Button
              loading={busy}
              disabled={!address.trim() || (custom && !name.trim())}
              onClick={add}
            >
              Add provider
            </Button>
          )}
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
