// The Scheduled page's line to the browser, uw_chat.mojom.
import {
  ScheduledHandlerFactory,
  ScheduledHandlerRemote,
  ScheduledPageCallbackRouter,
} from "uw-chat-mojom";
import type { ScheduledRow, WatchAddress } from "uw-chat-mojom";

export type { ScheduledRow, WatchAddress };

export const scheduledPage = new ScheduledPageCallbackRouter();
export const scheduledHandler = new ScheduledHandlerRemote();

let connected = false;

// Asks the browser for the rows. Call it once the page listens, or the rows
// the browser sends first are lost.
export function connect() {
  if (connected) {
    return;
  }
  connected = true;
  ScheduledHandlerFactory.getRemote().createScheduledHandler(
    scheduledPage.$.bindNewPipeAndPassRemote(),
    scheduledHandler.$.bindNewPipeAndPassReceiver(),
  );
}
