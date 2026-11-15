// Small parts every view of uw://ai/providers shares.
import { Blocks, Brain, Link2, Plug, Sparkles, Split } from "lucide-react";
import { cn } from "@/lib/utils";
import {
  ModelSlot,
  ProviderFormat,
  ProviderPreset,
  UpgradeNotice,
  type Model,
  type ModelChoice,
  type ModelList,
  type Provider,
} from "./providers-api";

// The icons ship from @lobehub/icons-static-svg; bundle.ts copies them next
// to the page. Jev and Custom have none, so they get a plain one.
const ICON_FILES: Partial<Record<ProviderPreset, string>> = {
  [ProviderPreset.kOpenAi]: "openai",
  [ProviderPreset.kAnthropic]: "anthropic",
  [ProviderPreset.kOpenRouter]: "openrouter",
  [ProviderPreset.kVercelAiGateway]: "vercel",
  [ProviderPreset.kOllama]: "ollama",
  [ProviderPreset.kOpenCodeGo]: "opencode",
  [ProviderPreset.kCodex]: "openai",
};

// uw://ai's sections, each at its own address, in the order its sidebar
// and the chat page's AI settings menu list them.
export const AI_SECTIONS = [
  { path: "providers", name: "Providers", icon: Plug },
  { path: "skills", name: "Skills", icon: Sparkles },
  { path: "connectors", name: "Connectors", icon: Blocks },
  { path: "memory", name: "Memory", icon: Brain },
];

export const PRESET_NAMES: Record<ProviderPreset, string> = {
  [ProviderPreset.kOpenAi]: "OpenAI",
  [ProviderPreset.kAnthropic]: "Anthropic",
  [ProviderPreset.kOpenRouter]: "OpenRouter",
  [ProviderPreset.kVercelAiGateway]: "Vercel AI Gateway",
  [ProviderPreset.kOllama]: "Ollama",
  [ProviderPreset.kJev]: "Jev",
  [ProviderPreset.kOpenCodeGo]: "OpenCode Go",
  [ProviderPreset.kCodex]: "Codex",
  [ProviderPreset.kCustom]: "Custom",
};

export const FORMAT_NAMES: Record<ProviderFormat, string> = {
  [ProviderFormat.kChatCompletions]: "Chat completions",
  [ProviderFormat.kAnthropicMessages]: "Anthropic Messages",
  [ProviderFormat.kOpenAiResponses]: "OpenAI Responses",
  [ProviderFormat.kOpenAiDecisions]: "OpenAI Decisions",
  [ProviderFormat.kSystemOne]: "TypeSafe System One",
  [ProviderFormat.kEachModel]: "Each model's own",
};

export const SLOT_NAMES: Record<ModelSlot, string> = {
  [ModelSlot.kLanguage]: "Names tasks",
  [ModelSlot.kDecision]: "Makes quick choices",
  [ModelSlot.kAgent]: "Starts chats",
};

export function ProviderIcon({
  preset,
  className,
}: {
  preset: ProviderPreset;
  className?: string;
}) {
  const file = ICON_FILES[preset];
  if (!file) {
    const Icon = preset === ProviderPreset.kJev ? Split : Link2;
    return (
      <Icon
        aria-hidden
        className={cn("size-4 shrink-0 text-muted-foreground", className)}
      />
    );
  }
  // A mask takes the text color, so the icon follows dark mode.
  return (
    <span
      aria-hidden
      className={cn("inline-block size-4 shrink-0 bg-current", className)}
      style={{
        maskImage: `url(/icons/${file}.svg)`,
        maskSize: "contain",
        maskRepeat: "no-repeat",
        maskPosition: "center",
      }}
    />
  );
}

// OpenAI's mark as a button's leading icon, for "Continue with ChatGPT".
export function OpenAiMark({ size = 16 }: { size?: number }) {
  return (
    <span
      aria-hidden
      className="inline-block shrink-0 bg-current"
      style={{
        width: size,
        height: size,
        maskImage: "url(/icons/openai.svg)",
        maskSize: "contain",
        maskRepeat: "no-repeat",
        maskPosition: "center",
      }}
    />
  );
}

// "Continue with ChatGPT" is black with white text, as OpenAI's branding
// asks, in light and dark mode alike.
export const CHATGPT_BUTTON = "[--background:#fff] [--foreground:#000]";

export function ProviderTile({
  preset,
  large,
}: {
  preset: ProviderPreset;
  large?: boolean;
}) {
  return (
    <span
      className={cn(
        "grid shrink-0 place-items-center bg-muted",
        large ? "size-10 rounded-[11px]" : "size-8 rounded-[9px]",
      )}
    >
      <ProviderIcon preset={preset} className={large ? "size-5" : undefined} />
    </span>
  );
}

// Everything the page knows: the saved state, and each provider's model list
// once it arrives.
export interface PageData {
  providers: Provider[];
  // The address each preset fills in.
  presetAddresses: Partial<Record<ProviderPreset, string>>;
  choices: Record<ModelSlot, ModelChoice | null>;
  lists: Record<string, ModelList | undefined>;
  upgradeNotice: UpgradeNotice;
}

// The picked model as its provider lists it, once the list is in.
export function modelOf(
  data: PageData,
  choice: ModelChoice | null,
): Model | undefined {
  return choice
    ? data.lists[choice.providerId]?.models.find(
        (model) => model.name === choice.model,
      )
    : undefined;
}

export function servesSlot(provider: Provider, slot: ModelSlot) {
  return slot === ModelSlot.kDecision
    ? provider.servesDecisions
    : provider.servesLanguage;
}
