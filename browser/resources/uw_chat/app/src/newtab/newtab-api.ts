// The new tab page's lines to the browser: uw_chat.mojom for the page, and
// Chromium's searchbox, as in the command bar, for the box's suggestions.
import {
  NewTabHandlerFactory,
  NewTabHandlerRemote,
  NewTabPageCallbackRouter,
} from "uw-chat-mojom";
import type {
  NewTabBox,
  NewTabRow,
  NewTabRunningRow,
  NewTabSections,
} from "uw-chat-mojom";
import { SuggestInventory } from "//resources/mojo/components/omnibox/browser/fusebox_action.mojom-webui.js";
import {
  InputMethod,
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from "//resources/mojo/components/omnibox/browser/searchbox.mojom-webui.js";
import type {
  AutocompleteMatch,
  AutocompleteResult,
} from "//resources/mojo/components/omnibox/browser/searchbox.mojom-webui.js";

export type {
  AutocompleteMatch,
  AutocompleteResult,
  NewTabBox,
  NewTabRow,
  NewTabRunningRow,
  NewTabSections,
};

export const newTabPage = new NewTabPageCallbackRouter();
export const newTabHandler = new NewTabHandlerRemote();
export const searchPage = new PageCallbackRouter();
export const searchHandler = new PageHandlerRemote();

// The line the searchbox reads as the typed text with no row picked.
export const NO_ROW = 255;

let connected = false;

// Asks the browser for the page's state. Call it once the page listens, or
// what the browser sends first is lost.
export function connect() {
  if (connected) {
    return;
  }
  connected = true;
  NewTabHandlerFactory.getRemote().createNewTabHandler(
    newTabPage.$.bindNewPipeAndPassRemote(),
    newTabHandler.$.bindNewPipeAndPassReceiver(),
  );
  PageHandlerFactory.getRemote().createPageHandler(
    searchPage.$.bindNewPipeAndPassRemote(),
    searchHandler.$.bindNewPipeAndPassReceiver(),
  );
}

let queryId = 0;

// Asks for the rows for `text`: open tabs, history, bookmarks, and a search,
// none from the search engine.
export function query(text: string, caret: number) {
  if (!text) {
    searchHandler.stopAutocomplete(/*clearResult=*/ true);
    return;
  }
  searchHandler.queryAutocomplete(
    ++queryId,
    text,
    /*preventInlineAutocomplete=*/ true,
    caret,
    SuggestInventory.kDefault,
    /*isOnFocus=*/ false,
    /*keyword=*/ "",
    InputMethod.kKeyboard,
  );
}

// Opens the row on `line`, or the typed text, in this tab.
export function open(line: number, url: string) {
  searchHandler.openAutocompleteMatch(
    line,
    url,
    /*areMatchesShowing=*/ true,
    /*mouseButton=*/ 0,
    { altKey: false, ctrlKey: false, metaKey: false, shiftKey: false },
    /*viaKeyboard=*/ true,
  );
}
