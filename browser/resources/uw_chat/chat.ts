// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

import {ChatHandlerFactory, ChatHandlerRemote, ChatPageCallbackRouter, StepKind} from './uw_chat.mojom-webui.js';
import type {Step} from './uw_chat.mojom-webui.js';

const log = document.getElementById('log')!;
const working = document.getElementById('working')!;
const form = document.getElementById('composer') as HTMLFormElement;
const field = document.getElementById('message') as HTMLTextAreaElement;
const send = document.getElementById('send') as HTMLButtonElement;

const page = new ChatPageCallbackRouter();
const handler = new ChatHandlerRemote();
ChatHandlerFactory.getRemote().createHandler(
    page.$.bindNewPipeAndPassRemote(), handler.$.bindNewPipeAndPassReceiver());

function add(kind: string, text: string) {
  const line = document.createElement('p');
  line.className = kind;
  line.textContent = text;
  log.append(line);
  line.scrollIntoView({block: 'end'});
}

// Nothing can be sent until the chat is loaded.
send.disabled = true;

function setWorking(on: boolean) {
  working.hidden = !on;
  send.disabled = on;
}

// One line in plain words for each tool the agent used.
function describe(step: Step): string {
  const args = JSON.parse(step.args);
  const result = JSON.parse(step.result);
  if (result.error) {
    return `${step.tool}: ${result.error}`;
  }
  switch (step.tool) {
    case 'open':
      return `Opened ${args.url} in tab ${result.tab}`;
    case 'navigate':
      return `Went to ${args.url} in tab ${args.tab}`;
    case 'read':
      return `Read ${result.title || result.url} in tab ${args.tab}`;
    case 'click':
      return `Clicked element ${args.element} in tab ${args.tab}`;
    case 'type':
      return `Typed into element ${args.element} in tab ${args.tab}`;
    case 'recall':
      return `Looked again at ${result.result?.title || `step ${args.step}`}`;
    default:
      return step.tool;
  }
}

function show(step: Step) {
  switch (step.kind) {
    case StepKind.kTool:
      add(JSON.parse(step.result).error ? 'step error' : 'step',
          describe(step));
      break;
    case StepKind.kAnswer:
      add('answer', step.text);
      break;
    case StepKind.kFailed:
      add('failed', step.text);
      break;
    case StepKind.kUser:
      add('mine', step.text);
      break;
    case StepKind.kStopped:
      add('stopped', 'Stopped before it finished.');
      break;
    case StepKind.kResumed:
      add('step', 'Continued');
      break;
  }
}

// Picks up a chat that stopped, from the button after its last line.
function offerToContinue() {
  const button = document.createElement('button');
  button.id = 'continue';
  button.type = 'button';
  button.textContent = 'Continue';
  button.addEventListener('click', () => {
    button.remove();
    show({kind: StepKind.kResumed} as Step);
    setWorking(true);
    handler.resume();
  });
  log.lastElementChild!.append(button);
}

page.onOpened.addListener((id: string, title: string, lines: Step[]) => {
  // The chat's id in the address lets session restore reopen this chat.
  if (location.pathname !== `/${id}`) {
    history.replaceState(null, '', `/${id}`);
  }
  if (title) {
    document.title = title;
  }
  lines.forEach(show);
  if (lines.at(-1)?.kind === StepKind.kStopped) {
    offerToContinue();
  }
  setWorking(false);
});

page.onStep.addListener((step: Step) => {
  show(step);
  if (step.kind !== StepKind.kTool) {
    setWorking(false);
  }
});

form.addEventListener('submit', (e: Event) => {
  e.preventDefault();
  const text = field.value.trim();
  if (!text || send.disabled) {
    return;
  }
  // The first message names the tab.
  if (!log.querySelector('.mine')) {
    document.title = text;
  }
  document.getElementById('continue')?.remove();
  add('mine', text);
  field.value = '';
  setWorking(true);
  handler.send(text);
});

field.addEventListener('keydown', (e: KeyboardEvent) => {
  if (e.key === 'Enter' && !e.shiftKey && !e.isComposing) {
    e.preventDefault();
    form.requestSubmit();
  }
});
