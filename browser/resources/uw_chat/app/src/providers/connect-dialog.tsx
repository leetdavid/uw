// The connect dialog: search uw's catalog of web connectors, Recommended
// first, and add one with a click and a sign-in. See the frames "Connect
// dialog: recommended (Fluid)" and "Connect dialog: every state (Fluid)" on
// the canvas page "AI providers", and docs/plans/connect-dialog.md.
import { useEffect, useState, type ReactNode } from "react";
import { Braces, Check, Globe, KeyRound, Loader2, Search } from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Dialog, DialogContent, DialogHeader, DialogTitle } from "@/components/ui/dialog";
import {
  siAtlassian,
  siFigma,
  siGithub,
  siGmail,
  siGooglecalendar,
  siIntercom,
  siLinear,
  siNeon,
  siNetlify,
  siNotion,
  siPosthog,
  siRailway,
  siSentry,
  siStripe,
  siSupabase,
  siTldraw,
  siVercel,
  siWebflow,
  siZapier,
  type SimpleIcon,
} from "simple-icons";
import { cn } from "@/lib/utils";
import {
  CatalogSource,
  ConnectorState,
  providersHandler,
  type CatalogAnswer,
  type CatalogEntry,
  type ConnectorInfo,
} from "./providers-api";

// Logos for the services uw knows by name, drawn from simple-icons' paths.
// Exa's comes from @lobehub/icons-static-svg, which bundle.ts copies; the
// rest get a globe.
const ICONS: Record<string, SimpleIcon> = {
  atlassian: siAtlassian,
  figma: siFigma,
  github: siGithub,
  gmail: siGmail,
  "google-calendar": siGooglecalendar,
  intercom: siIntercom,
  linear: siLinear,
  neon: siNeon,
  netlify: siNetlify,
  notion: siNotion,
  posthog: siPosthog,
  railway: siRailway,
  sentry: siSentry,
  stripe: siStripe,
  supabase: siSupabase,
  tldraw: siTldraw,
  vercel: siVercel,
  webflow: siWebflow,
  zapier: siZapier,
};

const SOURCE_NAMES: Record<CatalogSource, string> = {
  [CatalogSource.kRecommended]: "Recommended",
  [CatalogSource.kClaudePlugin]: "Plugin · Claude's directory",
  [CatalogSource.kOpenAiPlugin]: "Plugin · OpenAI's directory",
  [CatalogSource.kRegistry]: "MCP Registry",
};

// The host and path of an address, as uw tells two servers apart.
function serverOf(url: string) {
  try {
    const parsed = new URL(url);
    return parsed.host.toLowerCase() + parsed.pathname.replace(/\/+$/, "");
  } catch {
    return url;
  }
}

function hostOf(url: string) {
  try {
    return new URL(url).host;
  } catch {
    return url;
  }
}

// "today at 9:12", or "Oct 22 at 9:12".
function whenRefreshed(ms: number) {
  const date = new Date(ms);
  const time = date.toLocaleTimeString([], { hour: "numeric", minute: "2-digit" });
  const today = new Date().toDateString() === date.toDateString();
  const day = today ? "today" : date.toLocaleDateString([], { month: "short", day: "numeric" });
  return `${day} at ${time}`;
}

export function EntryTile({ entry }: { entry: CatalogEntry }) {
  const known = entry.source !== CatalogSource.kRegistry;
  const icon = known ? ICONS[entry.name] : undefined;
  return (
    <span className="grid size-9 shrink-0 place-items-center rounded-[9px] border border-border bg-background">
      {icon ? (
        <svg aria-hidden viewBox="0 0 24 24" className="size-[18px] fill-current">
          <path d={icon.path} />
        </svg>
      ) : known && entry.name === "exa" ? (
        <span
          aria-hidden
          className="inline-block size-5 bg-current"
          style={{
            maskImage: "url(/icons/exa.svg)",
            maskSize: "contain",
            maskRepeat: "no-repeat",
            maskPosition: "center",
          }}
        />
      ) : (
        <Globe aria-hidden className="size-4 text-muted-foreground" />
      )}
    </span>
  );
}

// Where an added entry's connector stands, with the button that moves it on.
function Standing({ connector }: { connector: ConnectorInfo }) {
  const quiet = (icon: ReactNode, text: string) => (
    <span className="flex items-center gap-1.5 whitespace-nowrap text-[12.5px] text-muted-foreground">
      {icon}
      {text}
    </span>
  );
  switch (connector.state) {
    case ConnectorState.kStarting:
      return quiet(<Loader2 aria-hidden className="size-3 animate-spin" />, "Starting…");
    case ConnectorState.kSigningIn:
      return (
        <span className="flex items-center gap-2">
          <Loader2 aria-hidden className="size-3 animate-spin text-muted-foreground" />
          <Button variant="secondary" size="compact" onClick={() => providersHandler.cancelSignIn(connector.name)}>
            Cancel
          </Button>
        </span>
      );
    case ConnectorState.kNeedsSignIn:
      return (
        <Button variant="secondary" size="compact" onClick={() => providersHandler.signInConnector(connector.name)}>
          Sign in
        </Button>
      );
    case ConnectorState.kFailed:
    case ConnectorState.kCantSignIn:
      return quiet(null, "Didn't connect");
    default:
      return quiet(<Check aria-hidden className="size-3.5 text-success" />, "Added");
  }
}

function EntryRow({
  entry,
  connector,
  meta,
}: {
  entry: CatalogEntry;
  connector?: ConnectorInfo;
  meta: boolean;
}) {
  const [asking, setAsking] = useState(false);
  const [values, setValues] = useState<Record<string, string>>(() =>
    Object.fromEntries(entry.fields.map((field) => [field.name, field.value])),
  );
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const add = async () => {
    setBusy(true);
    const answer = await providersHandler.addFromCatalog(entry.id, values);
    setBusy(false);
    setError(answer.error);
    if (!answer.error) {
      setAsking(false);
    }
  };
  const line =
    connector?.state === ConnectorState.kSigningIn
      ? "Finish signing in in the tab that opened"
      : connector?.state === ConnectorState.kFailed || connector?.state === ConnectorState.kCantSignIn
        ? connector.error
        : entry.description;
  return (
    <div data-entry={entry.id} className="py-2.5">
      <div className="flex items-start gap-3">
        <EntryTile entry={entry} />
        <div className="min-w-0 flex-1">
          <div className="flex items-center gap-1.5">
            <span className="truncate text-[13.5px] font-semibold">{entry.title}</span>
            {meta && entry.source === CatalogSource.kRecommended && <Badge color="blue">Recommended</Badge>}
          </div>
          <div
            className={cn(
              "truncate text-[12.5px]",
              line === connector?.error ? "text-destructive" : "text-muted-foreground",
            )}
          >
            {line}
          </div>
          {meta && (
            <div className="truncate text-xs text-muted-foreground">
              <span className="font-medium text-foreground/80">{SOURCE_NAMES[entry.source]}</span>
              {" · "}
              {hostOf(entry.url)}
              {entry.fields.some((field) => field.required) && " · asks for a key"}
            </div>
          )}
        </div>
        <div className="shrink-0 pt-0.5">
          {connector ? (
            <Standing connector={connector} />
          ) : (
            <Button
              variant="secondary"
              size="compact"
              data-action="Add"
              loading={busy && !asking}
              onClick={() => (entry.fields.length ? setAsking(true) : add())}
            >
              Add
            </Button>
          )}
        </div>
      </div>
      {asking && !connector && (
        <div className="mt-2.5 ml-12 rounded-[10px] border border-border p-3">
          {entry.fields.map((field) => (
            <label key={field.name} className="mb-2.5 block">
              <span className="font-mono text-[11.5px] text-muted-foreground">
                {field.name}
                {field.required && <span className="text-destructive"> *</span>}
              </span>
              <input
                data-field={field.name}
                type={field.secret ? "password" : "text"}
                spellCheck={false}
                value={values[field.name] ?? ""}
                onChange={(event) => setValues({ ...values, [field.name]: event.target.value })}
                className="mt-1 h-8 w-full rounded-lg bg-accent px-2.5 font-mono text-[12px] outline-none"
              />
              {field.description && (
                <span className="mt-1 block text-xs text-muted-foreground">{field.description}</span>
              )}
            </label>
          ))}
          <div className="flex items-center gap-2">
            <span className="mr-auto flex items-center gap-1.5 text-xs text-muted-foreground">
              <KeyRound aria-hidden className="size-3.5" />
              Goes to Keychain
            </span>
            <Button variant="ghost" size="compact" onClick={() => setAsking(false)}>
              Cancel
            </Button>
            <Button size="compact" data-action="Add with fields" loading={busy} onClick={add}>
              Add
            </Button>
          </div>
        </div>
      )}
      {error && (
        <p role="alert" className="mt-1.5 ml-12 text-[12.5px] text-destructive">
          {error}
        </p>
      )}
    </div>
  );
}

export function ConnectDialog({
  open,
  onOpenChange,
  connectors,
  onPaste,
}: {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  connectors: ConnectorInfo[];
  onPaste: () => void;
}) {
  const [query, setQuery] = useState("");
  const [answer, setAnswer] = useState<CatalogAnswer | null>(null);
  // Searches again as the user types, and whenever a connector or the
  // catalog changes, which gives a new list of connectors.
  useEffect(() => {
    if (!open) {
      return;
    }
    let live = true;
    providersHandler.searchCatalog(query.trim()).then(({ answer }) => {
      if (live) {
        setAnswer(answer);
      }
    });
    return () => {
      live = false;
    };
  }, [open, query, connectors]);
  const byServer = new Map(
    connectors.filter((connector) => connector.remote).map((connector) => [serverOf(connector.address), connector]),
  );
  const searching = query.trim() !== "";
  const entries = answer?.entries ?? [];
  const pasteLink = (id?: string) => (
    <button id={id} type="button" className="inline-flex items-center gap-1.5 text-[12.5px] font-medium text-info" onClick={onPaste}>
      <Braces aria-hidden className="size-3.5" />
      Add by address or JSON
    </button>
  );
  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent size="xl">
        <DialogHeader>
          <DialogTitle>Add connector</DialogTitle>
        </DialogHeader>
        <p className="-mt-1 text-[13px] text-muted-foreground">
          Find a service, click Add, and sign in.
          {connectors.length > 0 && ` ${connectors.length} added.`}
        </p>
        <div className="mt-3 flex h-10 items-center gap-2 rounded-[10px] bg-accent px-3">
          <Search aria-hidden className="size-4 text-muted-foreground" />
          <input
            id="catalog-search"
            aria-label="Search for a service"
            placeholder="Search for a service"
            autoFocus
            spellCheck={false}
            value={query}
            onChange={(event) => setQuery(event.target.value)}
            className="h-full flex-1 bg-transparent text-[13.5px] outline-none placeholder:text-muted-foreground"
          />
          {answer?.refreshing && <Loader2 aria-label="Updating the catalog" className="size-3.5 animate-spin text-muted-foreground" />}
        </div>
        <div className="mt-3 max-h-[420px] min-h-[200px] overflow-y-auto">
          {!searching && (
            <>
              <div className="flex items-baseline">
                <h3 className="text-[13px] font-semibold">Recommended</h3>
                <span className="ml-auto text-xs text-muted-foreground">Each one tested by uw with a real account</span>
              </div>
              <div className="grid grid-cols-2 gap-x-8">
                {entries.map((entry) => (
                  <EntryRow key={entry.id} entry={entry} connector={byServer.get(serverOf(entry.url))} meta={false} />
                ))}
              </div>
            </>
          )}
          {searching && entries.length > 0 && (
            <div className="divide-y divide-border">
              {entries.map((entry) => (
                <EntryRow key={entry.id} entry={entry} connector={byServer.get(serverOf(entry.url))} meta />
              ))}
            </div>
          )}
          {searching && answer && entries.length === 0 && (
            <div id="no-results" className="flex flex-col items-center py-8 text-center">
              <span className="grid size-9 place-items-center rounded-[9px] bg-accent">
                <Search aria-hidden className="size-4 text-muted-foreground" />
              </span>
              <h3 className="mt-3 text-[13.5px] font-semibold">Nothing called "{query.trim()}"</h3>
              <p className="mt-1 max-w-[420px] text-[12.5px] text-muted-foreground">
                {answer.refreshed
                  ? `uw searched Recommended, Claude's and OpenAI's plugins, and the MCP Registry, as of ${whenRefreshed(answer.refreshed)}.`
                  : answer.refreshing
                    ? "uw is still downloading the MCP Registry and the plugin directories."
                    : "uw couldn't download the MCP Registry or the plugin directories yet."}
              </p>
              <div className="mt-3">{pasteLink()}</div>
            </div>
          )}
        </div>
        <div className="mt-3 flex items-center border-t border-border pt-3">
          {pasteLink("paste-connector")}
          <span className="ml-auto text-xs text-muted-foreground">
            Search covers the MCP Registry and Claude's and OpenAI's plugins, kept on this Mac.
          </span>
        </div>
      </DialogContent>
    </Dialog>
  );
}
