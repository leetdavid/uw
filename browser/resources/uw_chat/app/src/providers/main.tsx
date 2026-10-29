// uw://ai: the AI providers the user saves once, the language, decision, and
// agent models that pick from them, and skills. Each section has its own
// address: uw://ai/providers, which uw://ai also opens, uw://ai/providers/<id>,
// uw://ai/skills, uw://ai/connectors, and uw://ai/connectors/<name>. Built from Fluid Functionalism's parts; see the "(Fluid)"
// frames on the canvas page "AI providers", docs/plans/ai-providers.md, and
// docs/plans/connectors.md#the-ai-page.
import "@/trusted-types";
import "@/theme-colors";
import { StrictMode, useCallback, useEffect, useState } from "react";
import { createRoot } from "react-dom/client";
import { MotionConfig } from "motion/react";
import { ChevronRight, Loader2, MessageCircle, Plus } from "lucide-react";
import {
  Banner,
  BannerAction,
  BannerActions,
  BannerDescription,
  BannerTitle,
} from "@/components/ui/banner";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import { AddProviderDialog } from "./add-provider";
import { EffortChip, ModelChip } from "./model-chip";
import { ProviderPage } from "./provider-page";
import { ConnectorsSection } from "./connectors";
import { SkillsSection } from "./skills";
import { MemorySection } from "./memory";
import {
  AI_SECTIONS,
  FORMAT_NAMES,
  ProviderTile,
  modelOf,
  servesSlot,
  type PageData,
} from "./provider-parts";
import {
  Decides,
  ModelSlot,
  ProviderFormat,
  TestOutcome,
  UpgradeNotice,
  providersHandler,
  type ModelList,
  type Provider,
  type ModelTestResult,
} from "./providers-api";

// The short names the provider list shows.
const FORMAT_BADGES: Record<ProviderFormat, string> = {
  [ProviderFormat.kChatCompletions]: "Chat completions",
  [ProviderFormat.kAnthropicMessages]: "Messages",
  [ProviderFormat.kOpenAiResponses]: "Responses",
  [ProviderFormat.kOpenAiDecisions]: "Decisions",
  [ProviderFormat.kEachModel]: "Each model's",
  [ProviderFormat.kSystemOne]: "System One",
};

// What the chip says while the slot is empty, and the menu row that empties
// it. Names tasks has no such row.
const UNSET: Record<ModelSlot, string | undefined> = {
  [ModelSlot.kLanguage]: undefined,
  [ModelSlot.kDecision]: "Same as Names tasks",
  [ModelSlot.kAgent]: "Same as Names tasks",
};

// Each row is named for what its model does. kDecision holds the
// quick-choice model, a decision model or a language model.
const ROWS = [
  {
    slot: ModelSlot.kDecision,
    name: "Makes quick choices",
    what: "Places tabs, and checks the agent's commands and memories.",
  },
  {
    slot: ModelSlot.kLanguage,
    name: "Names tasks",
    what: "Writes a short name for each new task.",
  },
  {
    slot: ModelSlot.kAgent,
    name: "Starts chats",
    what: "New chats start on it. Each chat can switch.",
  },
];

// One sentence about a test. A decision model answers with a choice, a
// language model with text.
function testSentence(slot: ModelSlot, result: ModelTestResult) {
  switch (result.outcome) {
    case TestOutcome.kPassed:
      return `${result.model} answered in ${result.seconds.toFixed(1)} s.`;
    case TestOutcome.kIncomplete:
      return "Pick a model whose provider has its key first.";
    case TestOutcome.kNoResponse:
      return `No answer from ${result.host}: ${result.netError}.`;
    case TestOutcome.kHttpError:
      return `${result.host} answered HTTP ${result.httpStatus}. ${result.providerMessage}`.trim();
    case TestOutcome.kUnusable:
      return `${result.model} answered, but not with ${slot === ModelSlot.kDecision ? "a choice" : "text"}.`;
  }
}

function TestLine({
  slot,
  result,
}: {
  slot: ModelSlot;
  result: ModelTestResult;
}) {
  const [details, setDetails] = useState(false);
  const passed = result.outcome === TestOutcome.kPassed;
  return (
    <div role="status" className="text-[12.5px]">
      <span className={passed ? "text-success" : "text-destructive"}>
        {passed ? "✓ " : "✕ "}
        {testSentence(slot, result)}
      </span>
      {result.detail && (
        <button
          type="button"
          className="ml-2 font-medium text-info"
          onClick={() => setDetails(!details)}
        >
          Details
        </button>
      )}
      {details && (
        <pre className="mt-2 max-h-60 overflow-auto whitespace-pre-wrap rounded-lg bg-muted p-2.5 text-[11.5px] text-muted-foreground">
          {result.detail}
        </pre>
      )}
    </div>
  );
}

// Shown once, after the first launch that turned the saved connections into
// providers.
function UpgradeBanner({
  notice,
  onDismiss,
}: {
  notice: UpgradeNotice;
  onDismiss: () => void;
}) {
  if (notice === UpgradeNotice.kNone) {
    return null;
  }
  const failed = notice === UpgradeNotice.kFailed;
  return (
    <Banner status={failed ? "warning" : "info"} contrast="high" className="mt-4">
      <BannerTitle>
        {failed
          ? "uw could not move your saved keys."
          : "Your saved connections are providers now."}
      </BannerTitle>
      <BannerDescription>
        {failed
          ? "Your old settings stay as they were. Add your providers here with their keys; uw tries the move again at the next launch."
          : notice === UpgradeNotice.kMerged
            ? "The organizer and chats use the same models as before. Connections with the same address and key share one provider."
            : "The organizer and chats use the same models as before."}
      </BannerDescription>
      <BannerActions>
        <BannerAction variant="primary" onClick={onDismiss}>
          Got it
        </BannerAction>
      </BannerActions>
    </Banner>
  );
}

// "51 models · 1 decision", or "· checking" while uw asks them.
function ModelCount({ list }: { list?: ModelList }) {
  if (!list?.ok) {
    return <span />;
  }
  const count = list.models.length;
  const decides = list.models.filter(
    (model) => model.decides === Decides.kYes,
  ).length;
  return (
    <span className="flex items-center gap-1 whitespace-nowrap text-[12.5px] tabular-nums text-muted-foreground">
      <span>{`${count} model${count === 1 ? "" : "s"}`}</span>
      {list.checking ? (
        <>
          ·
          <Loader2 aria-hidden className="size-3 animate-spin" />
          checking
        </>
      ) : (
        decides > 0 && (
          <>
            · <span className="font-medium text-info">{decides} decision</span>
          </>
        )
      )}
    </span>
  );
}

function ProviderStatus({ provider, list }: { provider: Provider; list?: ModelList }) {
  if (!list) {
    return <span className="text-muted-foreground">Listing models…</span>;
  }
  const dot = list.ok ? "bg-success" : "bg-destructive";
  return (
    <span className="flex min-w-0 items-center gap-[7px] text-[12.5px] text-muted-foreground">
      <i className={`size-[7px] shrink-0 rounded-full ${dot}`} />
      <span className="truncate" title={list.ok ? undefined : list.error}>
        {!list.ok ? list.error : provider.hasKey ? "Key in Keychain" : "No key"}
      </span>
    </span>
  );
}

function ProvidersList({
  data,
  onChanged,
}: {
  data: PageData;
  onChanged: () => void;
}) {
  const [adding, setAdding] = useState(false);
  const [tests, setTests] = useState<Partial<Record<ModelSlot, ModelTestResult>>>({});
  const [testing, setTesting] = useState<ModelSlot | null>(null);

  const test = async (slot: ModelSlot) => {
    setTesting(slot);
    const { result } = await providersHandler.testModel(slot);
    setTests((old) => ({ ...old, [slot]: result }));
    setTesting(null);
    onChanged();
  };
  const forget = (slot: ModelSlot) =>
    setTests(({ [slot]: _, ...rest }) => rest);

  return (
    <>
      <h1 className="text-[20px] font-semibold tracking-[-0.01em]">Providers</h1>
      <p className="mt-1 text-[13.5px] text-muted-foreground">
        Add each provider once. Every model in uw comes from one of them.
      </p>
      <UpgradeBanner
        notice={data.upgradeNotice}
        onDismiss={async () => {
          await providersHandler.dismissUpgradeNotice();
          onChanged();
        }}
      />

      <h2 className="mb-2 mt-[22px] flex h-[30px] items-center text-[13px] font-semibold">
        What uw uses models for
      </h2>
      <div className="overflow-hidden rounded-xl border border-border">
        {ROWS.map(({ slot, name, what }) => {
          const choice = data.choices[slot];
          const testable = slot !== ModelSlot.kAgent && choice;
          return (
            <div
              key={slot}
              role="group"
              aria-label={name}
              className="border-border px-4 py-3 [&+&]:border-t"
            >
              <div className="grid grid-cols-[minmax(0,1fr)_260px_118px_76px] items-center gap-2.5">
                <div className="min-w-0">
                  <div className="text-[13.5px] font-semibold">{name}</div>
                  <div className="mt-0.5 text-[12.5px] leading-[1.35] text-muted-foreground">
                    {what}
                  </div>
                </div>
                <ModelChip
                  choice={choice}
                  providers={
                    slot === ModelSlot.kDecision
                      ? data.providers
                      : data.providers.filter((provider) =>
                          servesSlot(provider, slot),
                        )
                  }
                  lists={data.lists}
                  unset={UNSET[slot]}
                  quick={
                    slot === ModelSlot.kDecision
                      ? {
                          namesModel: data.choices[ModelSlot.kLanguage]?.model,
                          onAddProvider: () => setAdding(true),
                        }
                      : undefined
                  }
                  needsTools={slot === ModelSlot.kAgent}
                  className="w-full"
                  onPick={async (provider, model, language) => {
                    await providersHandler.pickModel(
                      slot,
                      provider.id,
                      model,
                      language,
                    );
                    forget(slot);
                    onChanged();
                  }}
                  onClear={async () => {
                    await providersHandler.clearModel(slot);
                    forget(slot);
                    onChanged();
                  }}
                />
                {slot === ModelSlot.kDecision ? (
                  choice?.language ? (
                    <span className="justify-self-start whitespace-nowrap rounded-[5px] bg-muted px-1.5 py-0.5 text-[11px] font-medium text-muted-foreground ring-1 ring-inset ring-border">
                      Language model
                    </span>
                  ) : (
                    <span />
                  )
                ) : (
                  <EffortChip
                    efforts={modelOf(data, choice)?.efforts ?? []}
                    effort={choice?.effort ?? ""}
                    model={choice?.model}
                    preset={
                      data.providers.find(
                        (provider) => provider.id === choice?.providerId,
                      )?.preset
                    }
                    className="w-full"
                    onPick={async (effort) => {
                      await providersHandler.setEffort(slot, effort);
                      onChanged();
                    }}
                  />
                )}
                {testable ? (
                  <Button
                    size="sm"
                    variant="secondary"
                    loading={testing === slot}
                    onClick={() => test(slot)}
                  >
                    Test
                  </Button>
                ) : (
                  <span />
                )}
              </div>
              {testable && tests[slot] && (
                <div className="mt-1.5">
                  <TestLine slot={slot} result={tests[slot]} />
                </div>
              )}
            </div>
          );
        })}
      </div>

      <div className="mb-2 mt-[22px] flex h-[30px] items-center gap-2">
        <h2 className="text-[13px] font-semibold">Providers</h2>
        <span className="text-muted-foreground">{data.providers.length}</span>
        <Button
          size="sm"
          leadingIcon={Plus}
          className="ml-auto"
          onClick={() => setAdding(true)}
        >
          Add provider
        </Button>
      </div>
      <div className="overflow-hidden rounded-xl border border-border">
        {!data.providers.length && (
          <p className="px-4 py-3 text-muted-foreground">
            No provider yet. Add one to pick its models.
          </p>
        )}
        {data.providers.map((provider) => {
          const list = data.lists[provider.id];
          return (
            <a
              key={provider.id}
              href={`/providers/${encodeURIComponent(provider.id)}`}
              className="grid h-[54px] grid-cols-[32px_minmax(0,1fr)_128px_164px_132px_16px] items-center gap-3 border-border pl-3 pr-3.5 [&+&]:border-t"
            >
              <ProviderTile preset={provider.preset} />
              <div className="min-w-0">
                <div className="text-[13.5px] font-semibold">{provider.name}</div>
                <div className="mt-px truncate text-[12px] text-muted-foreground">
                  {provider.address.replace(/^https?:\/\//, "")}
                </div>
              </div>
              <span
                title={FORMAT_NAMES[provider.format]}
                className="justify-self-start whitespace-nowrap rounded-md bg-muted px-[7px] py-[3px] text-[12px] text-muted-foreground"
              >
                {FORMAT_BADGES[provider.format]}
              </span>
              <ModelCount list={list} />
              <ProviderStatus provider={provider} list={list} />
              <ChevronRight aria-hidden className="size-4 text-muted-foreground" />
            </a>
          );
        })}
      </div>
      <p className="mt-2.5 text-[12px] text-muted-foreground">
        Keys stay in the macOS Keychain. Each feature sends its requests only to
        the provider of the model it uses.
      </p>
      <AddProviderDialog
        open={adding}
        onOpenChange={setAdding}
        providers={data.providers}
        presetAddresses={data.presetAddresses}
        onAdded={onChanged}
      />
    </>
  );
}

function SectionList({
  section,
  counts,
}: {
  section: string;
  counts: Record<string, string>;
}) {
  return (
    <nav
      aria-label="AI"
      className="sticky top-0 flex h-screen flex-col gap-0.5 border-r border-border px-2 pb-2 pt-4"
    >
      <h2 className="px-2.5 pb-2.5 text-[15px] font-semibold">AI</h2>
      {AI_SECTIONS.map(({ path, name, icon: Icon }) => (
        <a
          key={path}
          id={`section-${path}`}
          href={`/${path}`}
          aria-current={section === path ? "page" : undefined}
          className={cn(
            "flex h-9 items-center gap-2.5 rounded-lg px-2.5 text-foreground hover:bg-hover",
            section === path && "bg-accent font-semibold",
          )}
        >
          <Icon aria-hidden className="size-4 text-muted-foreground" />
          <span className="flex-1">{name}</span>
          <span className="text-xs font-normal tabular-nums text-muted-foreground">
            {counts[path]}
          </span>
        </a>
      ))}
      <div className="mt-auto border-t border-border pt-2">
        <button
          type="button"
          id="back-to-chat"
          className="flex h-9 w-full items-center gap-2.5 rounded-lg px-2.5 text-foreground hover:bg-hover"
          onClick={() => providersHandler.showChat()}
        >
          <MessageCircle aria-hidden className="size-4 text-muted-foreground" />
          Back to chat
        </button>
      </div>
    </nav>
  );
}

function AiPage() {
  const [data, setData] = useState<PageData | null>(null);
  const [skillsOn, setSkillsOn] = useState<number | null>(null);
  const [connectorsOn, setConnectorsOn] = useState<number | null>(null);
  const [memories, setMemories] = useState<number | null>(null);
  // "/providers/<id>" opens one provider; "/" is the providers list.
  const [section = "providers", id = ""] = location.pathname
    .split("/")
    .filter(Boolean)
    .map(decodeURIComponent);

  // `relist` names a provider whose models to list again, such as one with a
  // new key.
  const refresh = useCallback(async (relist?: string) => {
    const { state } = await providersHandler.getState();
    setData((old) => {
      const lists = { ...old?.lists };
      if (relist) {
        delete lists[relist];
      }
      return {
        providers: state.providers,
        choices: {
          [ModelSlot.kLanguage]: state.language,
          [ModelSlot.kDecision]: state.decision,
          [ModelSlot.kAgent]: state.agent,
        },
        lists,
        upgradeNotice: state.upgradeNotice,
        presetAddresses: Object.fromEntries(
          state.presetAddresses.map(({ preset, address }) => [preset, address]),
        ),
      };
    });
  }, []);

  useEffect(() => {
    refresh();
  }, [refresh]);

  // Lists a provider's models, and again with each answer while uw asks
  // which of them make decisions.
  const listInto = (providerId: string) =>
    providersHandler.listModels(providerId).then(({ list }) => {
      setData((now) =>
        now && { ...now, lists: { ...now.lists, [providerId]: list } },
      );
      if (list.checking) {
        providersHandler
          .waitForDecisionChecks(providerId)
          .then(() => listInto(providerId));
      }
    });

  // Lists the models of each provider that has no list yet; the list says
  // whether it answers.
  const unlisted = data?.providers
    .filter((provider) => !(provider.id in data.lists))
    .map((provider) => provider.id)
    .join(" ");
  useEffect(() => {
    for (const providerId of unlisted?.split(" ").filter(Boolean) ?? []) {
      setData((old) => {
        if (!old || providerId in old.lists) {
          return old;
        }
        listInto(providerId);
        return { ...old, lists: { ...old.lists, [providerId]: undefined } };
      });
    }
  }, [unlisted]);

  useEffect(() => {
    providersHandler
      .getSkills()
      .then(({ skills }) => setSkillsOn(skills.filter((skill) => skill.on).length));
    providersHandler
      .getConnectors()
      .then(({ connectors }) =>
        setConnectorsOn(connectors.filter((connector) => connector.on).length),
      );
    providersHandler.getMemories("").then(({ total }) => setMemories(total));
  }, []);

  const provider =
    section === "providers"
      ? data?.providers.find((each) => each.id === id)
      : undefined;
  const sectionName =
    AI_SECTIONS.find((each) => each.path === section)?.name ?? "Providers";
  useEffect(() => {
    document.title = `${provider ? provider.name : section === "connectors" && id ? id : sectionName} – AI`;
  }, [provider, section, id, sectionName]);

  return (
    <div className="grid min-h-full grid-cols-[220px_minmax(0,1fr)]">
      <SectionList
        section={section}
        counts={{
          providers: data ? String(data.providers.length) : "",
          skills: skillsOn === null ? "" : `${skillsOn} on`,
          connectors: connectorsOn === null ? "" : `${connectorsOn} on`,
          memory: memories === null ? "" : String(memories),
        }}
      />
      <main className="min-h-full overflow-auto pb-10 pt-[26px]">
        <div className="mx-auto w-[740px] max-w-[calc(100%-32px)]">
          {section === "skills" ? (
            <SkillsSection onCount={setSkillsOn} />
          ) : section === "connectors" ? (
            <ConnectorsSection name={id} onCount={setConnectorsOn} />
          ) : section === "memory" ? (
            <MemorySection onCount={setMemories} />
          ) : (
            data &&
            (provider ? (
              <ProviderPage provider={provider} data={data} onChanged={refresh} />
            ) : (
              <ProvidersList data={data} onChanged={refresh} />
            ))
          )}
        </div>
      </main>
    </div>
  );
}

// Fluid's parts read dark mode from a class on the root element.
const dark = matchMedia("(prefers-color-scheme: dark)");
const followScheme = () =>
  document.documentElement.classList.toggle("dark", dark.matches);
followScheme();
dark.addEventListener("change", followScheme);

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <MotionConfig reducedMotion="user">
      <AiPage />
    </MotionConfig>
  </StrictMode>,
);
