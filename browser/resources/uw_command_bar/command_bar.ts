// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

import {SuggestInventory} from '//resources/mojo/components/omnibox/browser/fusebox_action.mojom-webui.js';
import type {AutocompleteResult} from '//resources/mojo/components/omnibox/browser/searchbox.mojom-webui.js';
import {InputMethod, PageCallbackRouter, PageHandlerFactory, PageHandlerRemote} from '//resources/mojo/components/omnibox/browser/searchbox.mojom-webui.js';

const field = document.querySelector<HTMLInputElement>('#field')!;
const results = document.querySelector<HTMLUListElement>('#results')!;

const router = new PageCallbackRouter();
const handler = new PageHandlerRemote();
PageHandlerFactory.getRemote().createPageHandler(
    router.$.bindNewPipeAndPassRemote(),
    handler.$.bindNewPipeAndPassReceiver());

let queryId = 0;

function show(result: AutocompleteResult) {
  if (result.input !== field.value) {
    return;
  }
  results.replaceChildren(...result.matches.map(match => {
    const row = document.createElement('li');
    row.textContent = match.contents;
    if (match.description) {
      const description = document.createElement('span');
      description.textContent = match.description;
      row.append(description);
    }
    return row;
  }));
}

function clear() {
  field.value = '';
  results.replaceChildren();
  handler.stopAutocomplete(/*clearResult=*/ true);
}

router.autocompleteResultChanged.addListener(show);

field.addEventListener('input', () => {
  if (!field.value) {
    clear();
    return;
  }
  handler.queryAutocomplete(
      ++queryId, field.value, /*preventInlineAutocomplete=*/ true,
      field.selectionStart ?? field.value.length, SuggestInventory.kDefault,
      /*isOnFocus=*/ false, /*keyword=*/ '', InputMethod.kKeyboard);
});

document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    clear();
  }
});

// The browser holds keys until the field takes focus, so the field gives it up
// whenever the panel goes and takes it again each time the panel shows. It
// waits one task: during the window's focus event the browser has not yet heard
// which frame has focus, and ignores the field's.
window.addEventListener('focus', () => setTimeout(() => field.focus()));
window.addEventListener('blur', () => field.blur());
if (document.hasFocus()) {
  field.focus();
}
