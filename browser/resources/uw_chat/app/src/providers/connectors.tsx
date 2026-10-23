// uw://ai/connectors: the connectors that give the agent tools, one at
// uw://ai/connectors/<name>, the connect dialog, and adding one by pasting an
// address or the JSON a server's README gives. uw://ai/connectors?add opens
// the dialog. See the "AI page" and "Connect dialog" frames on the canvas page
// "AI providers", docs/plans/connectors.md, and docs/plans/connect-dialog.md.
import { useEffect, useState, type ReactNode } from "react";
import {
  ChevronLeft,
  ChevronRight,
  Globe,
  KeyRound,
  Loader2,
  Plus,
  SquareTerminal,
} from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogContent,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Switch } from "@/components/ui/switch";
import { ConnectDialog } from "./connect-dialog";
import { cn } from "@/lib/utils";
import {
  ConnectorState,
  providersHandler,
  type ConnectorInfo,
  type NewConnector,
} from "./providers-api";

function Tile({ connector }: { connector: { remote: boolean } }) {
  const Icon = connector.remote ? Globe : SquareTerminal;
  return (
    <span className="grid size-8 shrink-0 place-items-center rounded-[9px] bg-muted">
      <Icon aria-hidden className="size-4 text-muted-foreground" />
    </span>
  );
}

function Dot({ className }: { className: string }) {
  return <i aria-hidden className={cn("size-[7px] shrink-0 rounded-full", className)} />;
}

// Where a connector stands, with the button that moves it on.
function Status({ connector }: { connector: ConnectorInfo }) {
  const button = (label: string, onClick: () => void, icon?: ReactNode) => (
    <Button
      variant="secondary"
      size="compact"
      data-action={label}
      onClick={(event) => {
        event.preventDefault();
        onClick();
      }}
    >
      {icon}
      {label}
    </Button>
  );
  const row = (dot: ReactNode, content: ReactNode) => (
    <span className="flex items-center gap-[7px] whitespace-nowrap text-[12.5px] text-muted-foreground">
      {dot}
      {content}
    </span>
  );
  switch (connector.state) {
    case ConnectorState.kReady:
      return row(<Dot className="bg-success" />, connector.remote ? "Connected" : "Running");
    case ConnectorState.kStarting:
      return row(<Loader2 aria-hidden className="size-3 animate-spin" />, "Starting…");
    case ConnectorState.kNeedsSignIn:
      return row(
        <Dot className="bg-warning" />,
        button("Sign in", () => providersHandler.signInConnector(connector.name)),
      );
    case ConnectorState.kSigningIn:
      return row(
        <Loader2 aria-hidden className="size-3 animate-spin" />,
        button("Cancel", () => providersHandler.cancelSignIn(connector.name)),
      );
    case ConnectorState.kCantSignIn:
      return row(<Dot className="bg-destructive" />, "Can't sign in");
    case ConnectorState.kFailed:
      return row(
        <Dot className="bg-destructive" />,
        connector.remote
          ? button("Try again", () => providersHandler.restartConnector(connector.name))
          : "Failed",
      );
    default:
      return row(<Dot className="bg-muted-foreground/40" />, "Off");
  }
}

// A failed program's last lines of its log, and what to do about it.
function FailureLog({ connector }: { connector: ConnectorInfo }) {
  return (
    <div data-log={connector.name} className="rounded-[10px] bg-accent px-3 py-2.5">
      {connector.logLines.length > 0 && (
        <pre className="overflow-x-auto font-mono text-[11.5px] leading-relaxed text-muted-foreground">
          {connector.logLines.join("\n")}
        </pre>
      )}
      <div className="mt-2 flex items-center gap-2">
        <span className="mr-auto text-xs text-muted-foreground">
          {connector.logLines.length > 0 ? "The last lines of its log" : "It printed nothing"}
        </span>
        <Button
          variant="secondary"
          size="compact"
          data-action="Show log"
          onClick={() => providersHandler.showConnectorLog(connector.name)}
        >
          Show log
        </Button>
        <Button
          variant="secondary"
          size="compact"
          leadingIcon={SquareTerminal}
          data-action="Open in terminal"
          onClick={() => providersHandler.openConnectorInTerminal(connector.name)}
        >
          Open in terminal
        </Button>
      </div>
    </div>
  );
}

function failedHere(connector: ConnectorInfo) {
  return !connector.remote && connector.state === ConnectorState.kFailed;
}

function OnSwitch({ connector }: { connector: ConnectorInfo }) {
  return (
    <span
      onClick={(event) => event.preventDefault()}
      aria-label={`${connector.name} on`}
      className="[&_[id]]:sr-only"
    >
      <Switch
        data-connector={connector.name}
        label={connector.on ? "On" : "Off"}
        checked={connector.on}
        onToggle={() => providersHandler.setConnectorOn(connector.name, !connector.on)}
      />
    </span>
  );
}

function ConnectorList({
  connectors,
  environmentProblem,
  onAdd,
}: {
  connectors: ConnectorInfo[];
  environmentProblem: string;
  onAdd: () => void;
}) {
  const on = connectors.filter((connector) => connector.on).length;
  return (
    <>
      <div className="flex items-center gap-2.5">
        <h1 className="text-[20px] font-semibold tracking-[-0.01em]">Connectors</h1>
        <span className="pt-0.5 text-muted-foreground">
          {on} on, {connectors.length - on} off
        </span>
        <Button id="add-connector" leadingIcon={Plus} className="ml-auto" onClick={onAdd}>
          Add connector
        </Button>
      </div>
      <p className="mt-1.5 max-w-[640px] text-[13.5px] text-muted-foreground">
        Connectors give the agent more tools: a program on this Mac, or a web
        address. The agent sees each connector's tools by name and loads them
        when a request calls for them.
      </p>
      {connectors.length > 0 && (
        <div className="mt-[18px] overflow-hidden rounded-xl border border-border">
          {connectors.map((connector) => (
            <div key={connector.name} className="border-t border-border first:border-t-0">
              <a
                href={`/connectors/${encodeURIComponent(connector.name)}`}
                data-row={connector.name}
                className="grid min-h-[58px] grid-cols-[32px_minmax(0,1fr)_76px_64px_152px_auto_16px] items-center gap-3 py-2 pl-3 pr-3.5 text-foreground hover:bg-hover"
              >
                <Tile connector={connector} />
                <div className="min-w-0">
                  <div
                    className={cn(
                      "text-[13.5px] font-semibold",
                      !connector.on && "text-muted-foreground",
                    )}
                  >
                    {connector.name}
                  </div>
                  <div
                    data-line
                    className={cn(
                      "truncate text-xs",
                      connector.error &&
                        (connector.state === ConnectorState.kFailed ||
                          connector.state === ConnectorState.kCantSignIn ||
                          connector.state === ConnectorState.kNeedsSignIn)
                        ? "text-destructive"
                        : "text-muted-foreground",
                      !connector.remote && !connector.error && "font-mono text-[11.5px]",
                    )}
                  >
                    {connector.error || connector.address}
                  </div>
                </div>
                <span className="justify-self-start rounded-md bg-muted px-[7px] py-[3px] text-xs whitespace-nowrap text-muted-foreground">
                  {connector.remote ? "Web" : "This Mac"}
                </span>
                <span className="text-[12.5px] whitespace-nowrap text-muted-foreground tabular-nums">
                  {connector.tools.length
                    ? `${connector.tools.length} tool${connector.tools.length === 1 ? "" : "s"}`
                    : "—"}
                </span>
                <Status connector={connector} />
                <OnSwitch connector={connector} />
                <ChevronRight aria-hidden className="size-4 text-muted-foreground" />
              </a>
              {failedHere(connector) && (
                <div className="pr-3.5 pb-3 pl-[56px]">
                  <FailureLog connector={connector} />
                </div>
              )}
            </div>
          ))}
        </div>
      )}
      <p className="mt-3 max-w-[640px] text-[12px] text-muted-foreground">
        Connectors start with uw and serve every profile. Keys and sign-ins stay
        in the macOS Keychain. Programs on this Mac start with your terminal's
        environment, so they find what your terminal finds.
      </p>
      {environmentProblem && (
        <p role="alert" className="mt-1.5 max-w-[640px] text-[12px] text-destructive">
          {environmentProblem}
        </p>
      )}
    </>
  );
}

function ConnectorPage({ connector }: { connector: ConnectorInfo }) {
  const [query, setQuery] = useState("");
  const wanted = query.trim().toLowerCase();
  const tools = connector.tools.filter((tool) =>
    `${tool.name} ${tool.description}`.toLowerCase().includes(wanted),
  );
  const remove = async () => {
    await providersHandler.removeConnector(connector.name);
    location.assign("/connectors");
  };
  const facts = [
    connector.state === ConnectorState.kReady
      ? connector.remote
        ? "Connected"
        : "Running"
      : undefined,
    connector.address,
    connector.version && `MCP ${connector.version}`,
  ].filter(Boolean);
  return (
    <>
      <a
        href="/connectors"
        className="inline-flex h-6 items-center gap-1 text-[12.5px] font-medium text-muted-foreground"
      >
        <ChevronLeft aria-hidden className="size-3.5" />
        Connectors
      </a>
      <div className="mt-2.5 flex items-center gap-3">
        <Tile connector={connector} />
        <div className="min-w-0">
          <h1 className="text-[20px] font-semibold tracking-[-0.01em]">{connector.name}</h1>
          <div className="truncate text-[12.5px] text-muted-foreground">{facts.join(" · ")}</div>
        </div>
        <div className="ml-auto flex items-center gap-2">
          {connector.remote && connector.state === ConnectorState.kReady && (
            <Button
              variant="secondary"
              size="compact"
              onClick={() => providersHandler.signOutConnector(connector.name)}
            >
              Sign out
            </Button>
          )}
          {connector.state !== ConnectorState.kReady && connector.state !== ConnectorState.kOff && (
            <Status connector={connector} />
          )}
          <Button variant="secondary" size="compact" className="text-destructive" onClick={remove}>
            Remove
          </Button>
          <OnSwitch connector={connector} />
        </div>
      </div>
      {connector.error && (
        <p role="alert" className="mt-3 text-[12.5px] text-destructive">
          {connector.error}
        </p>
      )}
      {failedHere(connector) && (
        <div className="mt-2.5 max-w-[760px]">
          <FailureLog connector={connector} />
        </div>
      )}
      {connector.secretNames.length > 0 && (
        <p className="mt-3 flex items-center gap-1.5 text-[12.5px] text-muted-foreground">
          <KeyRound aria-hidden className="size-3.5" />
          In Keychain: <span className="font-mono text-[11.5px]">{connector.secretNames.join(", ")}</span>
        </p>
      )}
      <div className="mt-5 mb-1.5 flex h-[30px] items-center gap-2">
        <h2 className="text-[13px] font-semibold">Tools</h2>
        <span className="text-muted-foreground">{connector.tools.length}</span>
        <input
          aria-label="Search tools"
          placeholder="Search tools"
          value={query}
          onChange={(event) => setQuery(event.target.value)}
          className="ml-auto h-[30px] w-[220px] rounded-lg bg-accent px-2.5 text-[12.5px] outline-none placeholder:text-muted-foreground"
        />
      </div>
      <p className="mb-2.5 max-w-[680px] text-[12.5px] text-muted-foreground">
        In Auto, the agent asks before a tool that doesn't say it only reads; in
        Ask first, before every tool. A tool you turn off is never offered to the
        agent.
      </p>
      <div className="overflow-hidden rounded-xl border border-border">
        {tools.map((tool) => (
          <div
            key={tool.name}
            data-tool={tool.name}
            className="grid grid-cols-[minmax(0,1fr)_auto_auto] items-center gap-4 border-t border-border py-2 pr-3.5 pl-4 first:border-t-0"
          >
            <div className="min-w-0">
              <div
                className={cn(
                  "font-mono text-[12.5px] font-medium",
                  !tool.on && "text-muted-foreground",
                )}
              >
                {tool.name}
              </div>
              <div className="truncate text-xs text-muted-foreground">{tool.description}</div>
            </div>
            <span className="flex items-center gap-1.5 whitespace-nowrap">
              {tool.alwaysAllowed ? (
                <>
                  <Badge data-tag="always" color="green">Always allowed</Badge>
                  <button
                    type="button"
                    className="text-xs font-medium text-info"
                    onClick={() =>
                      providersHandler.setToolAlwaysAllowed(connector.name, tool.name, false)
                    }
                  >
                    Take back
                  </button>
                </>
              ) : (
                tool.readOnly && (
                  <Badge data-tag="reads">Reads only</Badge>
                )
              )}
            </span>
            <span className="[&_[id]]:sr-only">
              <Switch
                data-tool-switch={tool.name}
                label={tool.on ? "On" : "Off"}
                checked={tool.on}
                onToggle={() =>
                  providersHandler.setConnectorToolOn(connector.name, tool.name, !tool.on)
                }
              />
            </span>
          </div>
        ))}
      </div>
      <div className="mt-5 mb-1.5 flex h-[30px] items-center gap-2">
        <h2 className="text-[13px] font-semibold">Prompts</h2>
        <span className="text-muted-foreground">{connector.prompts.length}</span>
      </div>
      {connector.prompts.length ? (
        <div className="overflow-hidden rounded-xl border border-border">
          {connector.prompts.map((prompt) => (
            <div key={prompt.name} className="border-t border-border px-4 py-2 first:border-t-0">
              <div className="font-mono text-[12.5px] font-medium">
                /{connector.name}:{prompt.name}
              </div>
              <div className="truncate text-xs text-muted-foreground">{prompt.description}</div>
            </div>
          ))}
        </div>
      ) : (
        <p className="text-[12.5px] text-muted-foreground">
          {connector.name} offers no prompts. A connector's prompts show in the
          chat's / menu.
        </p>
      )}
    </>
  );
}

function AddConnectorDialog({
  open,
  onOpenChange,
}: {
  open: boolean;
  onOpenChange: (open: boolean) => void;
}) {
  const [json, setJson] = useState("");
  const [found, setFound] = useState<NewConnector[]>([]);
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  useEffect(() => {
    if (!json.trim()) {
      setFound([]);
      setError("");
      return;
    }
    let live = true;
    providersHandler.readConnectors(json).then((answer) => {
      if (live) {
        setFound(answer.connectors);
        setError(answer.error);
      }
    });
    return () => {
      live = false;
    };
  }, [json]);
  const add = async () => {
    setBusy(true);
    const answer = await providersHandler.addConnectors(json);
    setBusy(false);
    if (answer.error) {
      setError(answer.error);
      return;
    }
    setJson("");
    onOpenChange(false);
  };
  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent size="lg">
        <DialogHeader>
          <DialogTitle>Add by address or JSON</DialogTitle>
        </DialogHeader>
        <p className="text-[13px] text-muted-foreground">
          Paste a server's web address, or the JSON from its README. The JSON
          can hold more than one connector.
        </p>
        <textarea
          id="connector-json"
          aria-label="Connector address or JSON"
          spellCheck={false}
          value={json}
          onChange={(event) => setJson(event.target.value)}
          placeholder={'https://mcp.linear.app/mcp\n\nor\n\n{\n  "mcpServers": {\n    "fs": { "command": "npx", "args": ["-y", "..."] }\n  }\n}'}
          className={cn(
            "mt-3 h-52 w-full resize-none rounded-[10px] bg-accent px-3 py-2.5 font-mono text-[11.5px] leading-relaxed outline-none placeholder:text-muted-foreground",
            error && "ring-[1.5px] ring-destructive",
          )}
        />
        {error && (
          <p role="alert" className="mt-2 text-[12.5px] text-destructive">
            {error}
          </p>
        )}
        {!error && found.length > 0 && (
          <>
            <div className="mt-3.5 text-xs font-semibold text-muted-foreground">
              {found.length} connector{found.length === 1 ? "" : "s"}
            </div>
            <div className="mt-1.5 rounded-[10px] border border-border">
              {found.map((connector) => (
                <div
                  key={connector.name}
                  data-found={connector.name}
                  className="grid grid-cols-[32px_minmax(0,1fr)_auto] items-center gap-2.5 border-t border-border px-3 py-2 first:border-t-0"
                >
                  <Tile connector={connector} />
                  <div className="min-w-0">
                    <div className="font-semibold">{connector.name}</div>
                    <div className="truncate text-xs text-muted-foreground">
                      {connector.remote ? "Web" : "This Mac"} · {connector.address}
                    </div>
                  </div>
                  <span className="flex items-center gap-1.5 text-xs whitespace-nowrap text-muted-foreground">
                    {connector.secretNames.length > 0 ? (
                      <>
                        <KeyRound aria-hidden className="size-3.5" />
                        <span className="font-mono">{connector.secretNames.join(", ")}</span>
                        {connector.secretNames.length === 1 ? " goes" : " go"} to Keychain
                      </>
                    ) : connector.remote ? (
                      "Asks you to sign in if it needs to"
                    ) : null}
                  </span>
                </div>
              ))}
            </div>
          </>
        )}
        <DialogFooter>
          <span className="mr-auto text-xs text-muted-foreground">Nothing starts until you add it.</span>
          <Button variant="ghost" onClick={() => onOpenChange(false)}>
            Cancel
          </Button>
          <Button id="add-connectors" loading={busy} disabled={!found.length || !!error} onClick={add}>
            {found.length > 1 ? `Add ${found.length} connectors` : "Add connector"}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}

// `name` opens one connector; empty shows the list. `onCount` hears how many
// are on, for the section list.
export function ConnectorsSection({
  name,
  onCount,
}: {
  name: string;
  onCount: (on: number) => void;
}) {
  const [connectors, setConnectors] = useState<ConnectorInfo[] | null>(null);
  const [environmentProblem, setEnvironmentProblem] = useState("");
  const [adding, setAdding] = useState(false);
  const [connecting, setConnecting] = useState(() =>
    new URLSearchParams(location.search).has("add"),
  );
  const connect = (open: boolean) => {
    setConnecting(open);
    if (!open && location.search) {
      history.replaceState(null, "", location.pathname);
    }
  };
  // Reads the list, and again each time a connector changes.
  useEffect(() => {
    let live = true;
    (async () => {
      while (live) {
        const answer = await providersHandler.getConnectors();
        if (!live) {
          return;
        }
        setConnectors(answer.connectors);
        setEnvironmentProblem(answer.environmentProblem);
        await providersHandler.waitForConnectorChange();
      }
    })();
    return () => {
      live = false;
    };
  }, []);
  const on = connectors?.filter((connector) => connector.on).length;
  useEffect(() => {
    if (on !== undefined) {
      onCount(on);
    }
  }, [on, onCount]);
  if (!connectors) {
    return null;
  }
  const open = connectors.find((connector) => connector.name === name);
  return (
    <>
      {open ? (
        <ConnectorPage connector={open} />
      ) : (
        <ConnectorList
          connectors={connectors}
          environmentProblem={environmentProblem}
          onAdd={() => connect(true)}
        />
      )}
      <ConnectDialog
        open={connecting}
        onOpenChange={connect}
        connectors={connectors}
        onPaste={() => {
          connect(false);
          setAdding(true);
        }}
      />
      <AddConnectorDialog open={adding} onOpenChange={setAdding} />
    </>
  );
}
