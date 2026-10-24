// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview The uw command's section at the top of Settings, System:
 * Install links /usr/local/bin/uw to the command inside uw.app, and
 * Uninstall removes it. See docs/plans/uw-command.md.
 */

import 'chrome://resources/cr_elements/cr_button/cr_button.js';

import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {CommandProblem} from './uw_organization.mojom-webui.js';
import type {CommandState} from './uw_organization.mojom-webui.js';
import {UwOrganizationBrowserProxyImpl} from './uw_organization_browser_proxy.js';
import {getHtml} from './uw_command_section.html.js';

// A piece of a sentence: text, or a command shown in monospace.
export interface Piece {
  text: string;
  command: boolean;
}

const EXAMPLES = ['uw chat', 'uw mcp add', 'uw settings'];

export class SettingsUwCommandSectionElement extends CrLitElement {
  static get is() {
    return 'settings-uw-command-section';
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      state_: {type: Object},
      busy_: {type: Boolean},
    };
  }

  // Null until uw has looked at the link.
  protected accessor state_: CommandState|null = null;
  protected accessor busy_: boolean = false;
  private browserProxy_ = UwOrganizationBrowserProxyImpl.getInstance();

  override connectedCallback() {
    super.connectedCallback();
    this.browserProxy_.getCommand().then(state => {
      this.state_ = state;
    });
  }

  i18n_(id: string, ...values: string[]): string {
    return loadTimeData.getStringF(id, ...values);
  }

  protected description_(): Piece[] {
    return loadTimeData.getString('uwCommandDescription')
        .split(/(\$[1-3])/)
        .filter(text => text)
        .map(text => /^\$[1-3]$/.test(text) ?
                 {text: EXAMPLES[Number(text[1]) - 1] ?? text, command: true} :
                 {text, command: false});
  }

  protected folder_(): string {
    const link = this.state_?.link ?? '';
    return link.slice(0, link.lastIndexOf('/'));
  }

  protected problem_(): string {
    switch (this.state_?.problem) {
      case CommandProblem.kCancelled:
        return this.i18n_('uwCommandCancelled', this.folder_());
      case CommandProblem.kFailed:
        return this.i18n_('uwCommandFailed', this.state_?.link ?? '');
      default:
        return '';
    }
  }

  protected async onInstallClick_() {
    this.busy_ = true;
    this.state_ = await this.browserProxy_.installCommand();
    this.busy_ = false;
  }

  protected async onUninstallClick_() {
    this.busy_ = true;
    this.state_ = await this.browserProxy_.uninstallCommand();
    this.busy_ = false;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-uw-command-section': SettingsUwCommandSectionElement;
  }
}

customElements.define(
    SettingsUwCommandSectionElement.is, SettingsUwCommandSectionElement);
