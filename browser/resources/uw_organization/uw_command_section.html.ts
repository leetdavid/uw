// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {SettingsUwCommandSectionElement} from './uw_command_section.js';

export function getHtml(this: SettingsUwCommandSectionElement) {
  // clang-format off
  return html`<!--_html_template_start_-->
<style>
  :host {
    display: block;
  }
  .row {
    align-items: center;
    display: flex;
    gap: 16px;
    min-height: var(--cr-section-min-height);
    padding: 12px var(--cr-section-padding);
  }
  .row + .row {
    border-top: var(--cr-separator-line);
  }
  h3 {
    font-size: 13px;
    font-weight: 600;
    margin: 0;
    padding: 16px var(--cr-section-padding) 0;
  }
  .text {
    flex: 1;
    min-width: 0;
  }
  .secondary {
    color: var(--cr-secondary-text-color);
    font-size: 12px;
    line-height: 1.45;
    margin-top: 3px;
  }
  .problem {
    color: var(--cr-fallback-color-error, #D93025);
  }
  .installed::before {
    background: #188038;
    border-radius: 50%;
    content: '';
    display: inline-block;
    height: 7px;
    margin-inline-end: 6px;
    vertical-align: 1px;
    width: 7px;
  }
  .note {
    min-height: 0;
    padding-block: 12px;
  }
  code {
    background: var(--cr-fallback-color-neutral-container, #F2F1F8);
    border-radius: 4px;
    color: var(--cr-primary-text-color);
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 11.5px;
    overflow-wrap: anywhere;
    padding: 1px 5px;
  }
</style>
<h3>${this.i18n_('uwCommandTitle')}</h3>
<div class="row">
  <div class="text">
    <div>${this.i18n_('uwCommandRow')}</div>
    ${this.state_?.installed ? html`
      <div class="secondary installed" id="installed">
        ${this.i18n_('uwCommandInstalledAt')} <code>${this.state_.link}</code>
      </div>
    ` : html`
      <div class="secondary" id="description">
        ${this.description_().map(piece => piece.command ?
            html`<code>${piece.text}</code>` : piece.text)}
      </div>
      <div class="secondary problem" id="problem"
          ?hidden="${!this.problem_()}">${this.problem_()}</div>
    `}
  </div>
  ${this.state_?.installed ? html`
    <cr-button id="uninstall" ?disabled="${this.busy_}"
        @click="${this.onUninstallClick_}">
      ${this.i18n_('uwCommandUninstall')}
    </cr-button>
  ` : html`
    <cr-button id="install" ?disabled="${this.busy_ || !this.state_}"
        @click="${this.onInstallClick_}">
      ${this.i18n_('uwCommandInstall')}
    </cr-button>
  `}
</div>
<div class="row secondary note" id="note">
  ${this.state_?.installed ? html`
    <span>${this.i18n_('uwCommandTry')}
      <code>uw chat "what's on my calendar today?"</code></span>
  ` : html`
    <span>${this.i18n_('uwCommandNote', this.folder_())}</span>
  `}
</div>
<!--_html_template_end_-->`;
  // clang-format on
}
