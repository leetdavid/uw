// The Inbox's line to the browser, uw_chat.mojom.
import {
  InboxHandlerFactory,
  InboxHandlerRemote,
  InboxPageCallbackRouter,
} from "uw-chat-mojom";
import type { InboxAlert } from "uw-chat-mojom";

export type { InboxAlert };

export const inboxPage = new InboxPageCallbackRouter();
export const inboxHandler = new InboxHandlerRemote();

let connected = false;

// Asks the browser for the alerts. Call it once the page listens, or the
// alerts the browser sends first are lost.
export function connect() {
  if (connected) {
    return;
  }
  connected = true;
  InboxHandlerFactory.getRemote().createInboxHandler(
    inboxPage.$.bindNewPipeAndPassRemote(),
    inboxHandler.$.bindNewPipeAndPassReceiver(),
  );
}
