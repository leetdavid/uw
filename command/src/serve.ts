/** uw mcp serve: uw's page tools for an outside agent, as an MCP server. See docs/plans/uw-command.md#the-tool-server. */

import { once } from "node:events";
import {
  CLIENT_CAPABILITIES_META_KEY, CLIENT_INFO_META_KEY, McpServer, acceptedContent, fromJsonSchema, inputRequired,
  type CallToolResult, type ClientCapabilities, type ElicitRequestFormParams, type Implementation, type JsonSchemaType, type ServerContext,
} from "@modelcontextprotocol/server";
import { StdioServerTransport, serveStdio } from "@modelcontextprotocol/server/stdio";
import { options, type Context, type Input, type Plan } from "./cli.ts";
import { CommandError, DONE, USAGE } from "./errors.ts";
import { VERSION } from "./launch.ts";
import { approvalCard, type Approval } from "./steps.ts";

const MODES = ["ask-first", "auto", "full-auto"];

interface Tool {
  name: string;
  description: string;
  inputSchema: JsonSchemaType;
}

/** What uw answers a tool call with: how it went, or what it waits on. */
interface Called {
  result?: Record<string, unknown>;
  waiting?: { step: string; approval: Approval };
}

/** What the client shows the user for a step that waits, and where the answer goes. */
const ANSWER: ElicitRequestFormParams["requestedSchema"] = {
  type: "object",
  properties: {
    choice: { type: "string", title: "Answer", enum: ["Approve", "Not this one"], default: "Approve" },
    note: { type: "string", title: "Tell the agent why, or what to do instead" },
  },
  required: ["choice"],
};

export async function servePlan(args: string[], input: Input): Promise<Plan | null> {
  const { values, positionals } = options(args, { mode: { type: "string" }, workspace: { type: "string" } });
  if (values.help) return null;
  if (positionals.length) throw new CommandError("uw mcp serve takes no arguments.\n  claude mcp add uw -- uw mcp serve", USAGE);
  if (values.mode !== undefined && !MODES.includes(values.mode)) {
    throw new CommandError(`--mode takes ask-first, auto, or full-auto, not ${values.mode}.`, USAGE);
  }
  const asked = {
    ...(values.mode === undefined ? {} : { mode: values.mode }),
    ...(values.workspace === undefined ? {} : { workspace: values.workspace }),
  };
  return { json: false, run: (context) => serve(context, asked, input) };
}

async function serve({ channel }: Context, asked: object, input: Input): Promise<number> {
  const { tools } = await channel.request("tools.list") as { tools: Tool[] };
  // The session starts with the first call, once the client has said who it is.
  let session: Promise<unknown> | undefined;
  const handle = serveStdio(() => {
    const server = new McpServer({ name: "uw", version: VERSION });
    const envelope = (ctx: ServerContext) => (ctx.mcpReq.envelope ?? {}) as Record<string, unknown>;
    const client = (ctx: ServerContext) =>
      (envelope(ctx)[CLIENT_INFO_META_KEY] ?? server.server.getClientVersion()) as Implementation | undefined;
    const canAsk = (ctx: ServerContext) =>
      Boolean(((envelope(ctx)[CLIENT_CAPABILITIES_META_KEY] ?? server.server.getClientCapabilities()) as ClientCapabilities | undefined)?.elicitation);
    for (const tool of tools) {
      server.registerTool(tool.name, { description: tool.description, inputSchema: fromJsonSchema(tool.inputSchema) }, async (args, ctx) => {
        try {
          const who = client(ctx);
          session ??= channel.request("tools.start", { ...asked, client: who?.title ?? who?.name ?? "An outside agent" });
          await session;
          // A call the client makes again, once its user has answered, carries the step that waited.
          const step = ctx.mcpReq.requestState<string>();
          const called = (step
            ? await channel.request("tools.answer", { step, ...answerOf(ctx.mcpReq.inputResponses) })
            : await channel.request("tools.call", { tool: tool.name, args, asks: canAsk(ctx) })) as Called;
          if (called.waiting) {
            return inputRequired({
              requestState: called.waiting.step,
              inputRequests: { approval: inputRequired.elicit({ message: question(called.waiting.approval), requestedSchema: ANSWER }) },
            });
          }
          return result(called.result ?? {});
        } catch (error) {
          return { content: [{ type: "text", text: (error as Error).message }], isError: true };
        }
      });
    }
    return server;
  }, { transport: new StdioServerTransport(input.pipes?.stdin, input.pipes?.stdout) });
  await once(input.pipes?.stdin ?? process.stdin, "end");
  await handle.close();
  return DONE;
}

/** A step that waits, as uw's card asks it. */
function question(approval: Approval): string {
  const { label, question: asked, detail, reason } = approvalCard(approval);
  return [`uw · ${label}`, asked, ...detail.map((line) => line.trim()), reason].filter(Boolean).join("\n");
}

function answerOf(responses: Record<string, unknown> | undefined): { choice: string; note?: string } {
  const answered = acceptedContent<{ choice?: string; note?: string }>(responses, "approval");
  if (answered?.choice === "Approve") return { choice: "allow" };
  return { choice: "decline", ...(answered?.note ? { note: answered.note } : {}) };
}

/** A tool's answer for the model: its JSON, or its error, as uw's agent gets them. */
function result(answer: Record<string, unknown>): CallToolResult {
  if (typeof answer.error === "string") {
    const said = typeof answer.user_said === "string" ? ` The user said: ${answer.user_said}` : "";
    return { content: [{ type: "text", text: `${answer.error}${said}` }], isError: true };
  }
  return { content: [{ type: "text", text: JSON.stringify(answer) }] };
}
