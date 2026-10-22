// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview uw settings: every command of the menu bar with its key, menu
 * by menu. uw's keys change in Chromium's key recorder, for the whole app.
 * Chromium's own keys only show, though a uw command can take one.
 */

import 'chrome://resources/cr_components/cr_shortcut_input/cr_shortcut_input.js';
import 'chrome://resources/cr_elements/cr_button/cr_button.js';

import {formatShortcutText} from 'chrome://resources/cr_components/cr_shortcut_input/cr_shortcut_util.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {ShortcutsHandler} from './uw_shortcuts.mojom-webui.js';
import type {MenuKey} from './uw_shortcuts.mojom-webui.js';
import {getHtml} from './uw_shortcuts_page.html.js';

// One menu of the menu bar, or one submenu, with its commands.
export interface Menu {
  title: string;
  keys: MenuKey[];
}

export class SettingsUwShortcutsPageElement extends CrLitElement {
  static get is() {
    return 'settings-uw-shortcuts-page';
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      menus_: {type: Array},
    };
  }

  protected accessor menus_: Menu[] = [];
  private handler_ = ShortcutsHandler.getRemote();

  override connectedCallback() {
    super.connectedCallback();
    this.show_(this.handler_.getMenuKeys());
  }

  i18n_(id: string): string {
    return loadTimeData.getString(id);
  }

  protected labelFor_(id: string, key: MenuKey): string {
    return loadTimeData.getStringF(id, key.title);
  }

  protected keyText_(key: string): string {
    return key ? formatShortcutText(key) : this.i18n_('uwShortcutsNoKey');
  }

  protected changed_(key: MenuKey): boolean {
    return key.key !== key.defaultKey;
  }

  protected anyChanged_(): boolean {
    return this.menus_.some(menu => menu.keys.some(key => this.changed_(key)));
  }

  protected wentTo_(key: MenuKey): string {
    return key.takenBy ? loadTimeData.getStringF(
                             'uwShortcutsWentTo',
                             formatShortcutText(key.defaultKey), key.takenBy) :
                         '';
  }

  protected onShortcutUpdated_(e: CustomEvent<string>) {
    const name = (e.currentTarget as HTMLElement).dataset['name']!;
    this.show_(this.handler_.setMenuKey(name, e.detail));
  }

  protected onResetClick_(e: Event) {
    const button = e.currentTarget as HTMLElement;
    const name = button.dataset['name']!;
    this.show_(
        name ? this.handler_.resetMenuKey(name) :
               this.handler_.giveBackMenuKey(button.dataset['key']!));
  }

  protected onResetAllClick_() {
    this.show_(this.handler_.resetMenuKeys());
  }

  // Groups the commands by menu, in the order the menus first show up. A
  // menu's commands after one of its submenus join its other commands.
  private async show_(reply: Promise<{keys: MenuKey[]}>) {
    const menus = new Map<string, MenuKey[]>();
    for (const key of (await reply).keys) {
      const keys = menus.get(key.menu);
      if (keys) {
        keys.push(key);
      } else {
        menus.set(key.menu, [key]);
      }
    }
    this.menus_ = Array.from(menus, ([title, keys]) => ({title, keys}));
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-uw-shortcuts-page': SettingsUwShortcutsPageElement;
  }
}

customElements.define(
    SettingsUwShortcutsPageElement.is, SettingsUwShortcutsPageElement);
