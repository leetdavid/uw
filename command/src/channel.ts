/** The channel to the running uw: JSON-RPC 2.0 over a Unix socket, one message per line. */

import { createConnection, type Socket } from "node:net";
import { createInterface } from "node:readline";
import { CommandError, FAILED, exitFor } from "./errors.ts";

interface Reply {
  id?: number;
  result?: unknown;
  error?: { code: number; message: string };
  // A notification, which uw sends while a chat runs.
  method?: string;
  params?: Record<string, unknown>;
}

export type Notified = (method: string, params: Record<string, unknown>) => void;

interface Waiting {
  resolve: (result: unknown) => void;
  reject: (error: Error) => void;
}

export class Channel {
  readonly #socket: Socket;
  readonly #waiting = new Map<number, Waiting>();
  #next = 1;
  #notified: Notified = () => {};
  #closed: () => void = () => {};

  private constructor(socket: Socket) {
    this.#socket = socket;
    createInterface({ input: socket }).on("line", (line) => this.#receive(line));
    socket.on("error", () => this.#fail("The connection to uw broke."));
    socket.on("close", () => {
      this.#fail("uw closed the connection.");
      this.#closed();
    });
  }

  static open(path: string): Promise<Channel> {
    return new Promise((resolve, reject) => {
      const socket = createConnection(path);
      socket.once("error", reject);
      socket.once("connect", () => {
        socket.off("error", reject);
        resolve(new Channel(socket));
      });
    });
  }

  request(method: string, params: object = {}): Promise<unknown> {
    const id = this.#next++;
    const reply = new Promise((resolve, reject) => this.#waiting.set(id, { resolve, reject }));
    this.#socket.write(`${JSON.stringify({ jsonrpc: "2.0", id, method, params })}\n`);
    return reply;
  }

  /** Hears uw's notifications, such as a chat's steps. */
  onNotification(notified: Notified): void {
    this.#notified = notified;
  }

  /** Hears when uw closes the connection, as when it quits. */
  onClose(closed: () => void): void {
    this.#closed = closed;
  }

  close(): void {
    this.#socket.end();
  }

  #receive(line: string): void {
    let reply: Reply;
    try {
      reply = JSON.parse(line) as Reply;
    } catch {
      this.#fail("uw sent a message the command can't read.");
      return;
    }
    if (reply.id === undefined) {
      if (reply.method) this.#notified(reply.method, reply.params ?? {});
      return;
    }
    const waiting = this.#waiting.get(reply.id);
    if (!waiting) return;
    this.#waiting.delete(reply.id);
    if (reply.error) waiting.reject(new CommandError(reply.error.message, exitFor(reply.error.code)));
    else waiting.resolve(reply.result);
  }

  #fail(message: string): void {
    for (const waiting of this.#waiting.values()) waiting.reject(new CommandError(message, FAILED));
    this.#waiting.clear();
  }
}
