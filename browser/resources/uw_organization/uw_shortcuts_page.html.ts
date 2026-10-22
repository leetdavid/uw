// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {SettingsUwShortcutsPageElement} from './uw_shortcuts_page.js';

export function getHtml(this: SettingsUwShortcutsPageElement) {
  // clang-format off
  return html`<!--_html_template_start_-->
<style>
  :host {
    --cr-shortcut-input-width: 268px;
  }
  .uw-head {
    align-items: center;
    display: flex;
    gap: 12px;
  }
  .uw-head h2 {
    flex: 1;
    font-size: 123%;
    font-weight: 500;
    margin: 0;
  }
  .uw-intro,
  .uw-note,
  .uw-key {
    color: var(--cr-secondary-text-color);
  }
  .uw-intro {
    margin: 4px 0 8px;
  }
  h3.uw-menu {
    font-size: 100%;
    font-weight: 500;
    margin: 20px 0 8px;
  }
  .uw-section {
    background: var(--cr-card-background-color);
    border-radius: var(--cr-card-border-radius);
    box-shadow: var(--cr-card-shadow);
    box-sizing: border-box;
    padding: 0 var(--cr-section-padding);
  }
  .uw-row {
    align-items: center;
    display: flex;
    gap: 12px;
    min-height: var(--cr-section-min-height);
    padding: 4px 0;
  }
  .uw-row + .uw-row {
    border-top: var(--cr-separator-line);
  }
  .uw-title {
    flex: 1;
  }
  .uw-note[hidden],
  .uw-reset[hidden] {
    display: none;
  }
  .uw-reset {
    --cr-button-border-color: transparent;
  }
  .uw-key {
    box-sizing: border-box;
    padding-inline-start: 8px;
    width: 268px;
  }
</style>
<div class="cr-centered-card-container">
  <div class="uw-head">
    <h2>${this.i18n_('uwShortcutsPageTitle')}</h2>
    <cr-button id="resetAll" ?disabled="${!this.anyChanged_()}"
        @click="${this.onResetAllClick_}">
      ${this.i18n_('uwShortcutsResetAll')}
    </cr-button>
  </div>
  <div class="uw-intro">${this.i18n_('uwShortcutsIntro')}</div>
  ${this.menus_.map(menu => html`
  <h3 class="uw-menu">${menu.title}</h3>
  <div class="uw-section">
    ${menu.keys.map(key => html`
    <div class="uw-row">
      <div class="uw-title">
        <div>${key.title}</div>
        <div class="uw-note" ?hidden="${!key.takenBy}">
          ${this.wentTo_(key)}
        </div>
      </div>
      <cr-button class="uw-reset" ?hidden="${!this.changed_(key)}"
          data-name="${key.name}" data-key="${key.defaultKey}"
          aria-label="${this.labelFor_('uwShortcutsResetLabel', key)}"
          @click="${this.onResetClick_}">
        ${this.i18n_('uwShortcutsReset')}
      </cr-button>
      ${key.name ? html`
      <cr-shortcut-input data-name="${key.name}" .shortcut="${key.key}"
          .allowCtrlAltShortcuts="${true}" .commandRequired="${true}"
          .inputAriaLabel="${this.labelFor_('uwShortcutsInputLabel', key)}"
          .editButtonAriaLabel="${this.labelFor_('uwShortcutsEditLabel', key)}"
          .clearButtonAriaLabel="${this.labelFor_('uwShortcutsClearLabel', key)}"
          @shortcut-updated="${this.onShortcutUpdated_}">
      </cr-shortcut-input>` : html`
      <div class="uw-key" title="${this.i18n_('uwShortcutsChromiumKey')}">
        ${this.keyText_(key.key)}
      </div>`}
    </div>`)}
  </div>`)}
</div>
<!--_html_template_end_-->`;
  // clang-format on
}
