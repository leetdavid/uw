// The chat page's line to the browser, uw_chat.mojom. The bundle step points
// "uw-chat-mojom" at the bindings the build generates.
import {
  ApprovalChoice,
  ApprovalMode,
  ChatHandlerFactory,
  ChatHandlerRemote,
  ChatPageCallbackRouter,
  ChatState,
  ChatConnectorState,
  StepKind,
} from "uw-chat-mojom";
import type {
  AgentTab,
  Approval,
  ChatConnector,
  ChatModel,
  ChatPrompt,
  ChatSummary,
  SkillProposal,
  Step,
} from "uw-chat-mojom";

export { ApprovalChoice, ApprovalMode, ChatState, ChatConnectorState, StepKind };
export type {
  AgentTab,
  Approval,
  ChatConnector,
  ChatModel,
  ChatPrompt,
  ChatSummary,
  SkillProposal,
  Step,
};

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
