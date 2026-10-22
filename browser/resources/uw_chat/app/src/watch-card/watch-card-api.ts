// The watch card's line to the browser, uw_chat.mojom.
import {
  WatchCardHandlerFactory,
  WatchCardHandlerRemote,
  WatchCardPageCallbackRouter,
} from "uw-chat-mojom";
import type { WatchCardState } from "uw-chat-mojom";

export type { WatchCardState };

export const watchCardPage = new WatchCardPageCallbackRouter();
export const watchCardHandler = new WatchCardHandlerRemote();

let connected = false;

// Asks the browser for the card. Call it once the page listens, or the card
// the browser sends back is lost.
export function connect() {
  if (connected) {
    return;
  }
  connected = true;
  WatchCardHandlerFactory.getRemote().createWatchCardHandler(
    watchCardPage.$.bindNewPipeAndPassRemote(),
    watchCardHandler.$.bindNewPipeAndPassReceiver(),
  );
}
