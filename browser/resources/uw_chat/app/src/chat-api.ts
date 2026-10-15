// The chat page's line to the browser, uw_chat.mojom. The bundle step points
// "uw-chat-mojom" at the bindings the build generates.
import {
  ChatHandlerFactory,
  ChatHandlerRemote,
  ChatPageCallbackRouter,
  ChatState,
  StepKind,
} from "uw-chat-mojom";
import type { AgentTab, ChatSummary, Step } from "uw-chat-mojom";

export { ChatState, StepKind };
export type { AgentTab, ChatSummary, Step };

export const chatPage = new ChatPageCallbackRouter();
export const chatHandler = new ChatHandlerRemote();

let connected = false;

// Asks the browser for this page's chat. Call it once the page listens, or
// the chat the browser sends back is lost.
export function connect() {
  if (connected) {
    return;
  }
  connected = true;
  ChatHandlerFactory.getRemote().createHandler(
    chatPage.$.bindNewPipeAndPassRemote(),
    chatHandler.$.bindNewPipeAndPassReceiver(),
  );
}
