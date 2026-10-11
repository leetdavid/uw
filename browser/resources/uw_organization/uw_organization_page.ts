// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview uw organization settings: the AI connection behind automatic
 * tab organization (mode, endpoint, model, API key) plus the global pause
 * controls. Secrets stay in the OS credential store; only non-secret
 * settings cross the native boundary.
 */

import 'chrome://resources/cr_elements/cr_button/cr_button.js';
import 'chrome://resources/cr_elements/cr_input/cr_input.js';

import type {CrInputElement} from 'chrome://resources/cr_elements/cr_input/cr_input.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {getHtml} from './uw_organization_page.html.js';
import {UwOrganizationBrowserProxyImpl} from './uw_organization_browser_proxy.js';

export interface SettingsUwOrganizationPageElement {
  $: {
    modeSelect: HTMLSelectElement,
    endpointInput: CrInputElement,
    modelInput: CrInputElement,
    keyInput: CrInputElement,
  };
}

export class SettingsUwOrganizationPageElement extends CrLitElement {
  static get is() {
    return 'settings-uw-organization-page';
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      mode_: {type: Number},
      modelsStatus_: {type: String},
      keyStatus_: {type: String},
      saveStatus_: {type: String},
      saveError_: {type: Boolean},
      saving_: {type: Boolean},
      enabled_: {type: Boolean},
      pauseMode_: {type: Number},
    };
  }

  protected accessor mode_: number = 0;
  protected accessor modelsStatus_: string = '';
  protected accessor keyStatus_: string = '';
  protected accessor saveStatus_: string = '';
  protected accessor saveError_: boolean = false;
  protected accessor saving_: boolean = false;
  protected accessor enabled_: boolean = false;
  protected accessor pauseMode_: number = 0;
  private browserProxy_ = UwOrganizationBrowserProxyImpl.getInstance();

  override connectedCallback() {
    super.connectedCallback();
    this.refresh_();
  }

  i18n_(id: string): string {
    return loadTimeData.getString(id);
  }

  isOllama_(): boolean {
    return this.mode_ === 0;
  }

  // Called by <settings-main> during settings search. Counts a match when
  // the query appears anywhere in this page's text.
  async searchContents(query: string):
      Promise<{canceled: boolean, matchCount: number, wasClearSearch: boolean}> {
    if (query === '') {
      return {canceled: false, matchCount: 0, wasClearSearch: true};
    }
    await this.updateComplete;
    const text = this.shadowRoot?.textContent?.toLowerCase() ?? '';
    const matchCount = text.includes(query.toLowerCase()) ? 1 : 0;
    return {canceled: false, matchCount, wasClearSearch: false};
  }

  protected onModeChange_() {
    this.mode_ = Number(this.$.modeSelect.value);
  }

  protected async onLoadModelsClick_() {
    this.modelsStatus_ = this.i18n_('uwOrganizationLoading');
    const endpoint = this.$.endpointInput.value.trim();
    const key = this.isOllama_() ? '' : this.$.keyInput.value;
    const response =
        await this.browserProxy_.loadModels(endpoint, key);
    if (!response.ok) {
      this.modelsStatus_ = this.i18n_('uwOrganizationModelsError');
      return;
    }
    if (response.models.length === 0) {
      this.modelsStatus_ = this.i18n_('uwOrganizationModelsEmpty');
      return;
    }
    const current = this.$.modelInput.value.trim();
    if (!current || !response.models.includes(current)) {
      this.$.modelInput.value = response.models[0]!;
    }
    this.modelsStatus_ = loadTimeData.getStringF(
        'uwOrganizationModelsLoaded', response.models.length);
  }

  protected async onSaveClick_() {
    if (this.saving_) {
      return;
    }
    this.saving_ = true;
    this.saveError_ = false;
    this.saveStatus_ = this.i18n_('uwOrganizationChecking');
    const response = await this.browserProxy_.save(
        this.mode_, this.$.endpointInput.value.trim(),
        this.$.modelInput.value.trim(),
        this.isOllama_() ? '' : this.$.keyInput.value);
    this.saving_ = false;
    if (!response.ok) {
      this.saveError_ = true;
      this.saveStatus_ = this.i18n_('uwOrganizationSetupError');
      return;
    }
    this.$.keyInput.value = '';
    await this.refresh_();
  }

  protected onPause1HourClick_() {
    this.setPause_(1);
  }

  protected onPauseUntilLaunchClick_() {
    this.setPause_(2);
  }

  protected onPauseIndefiniteClick_() {
    this.setPause_(3);
  }

  protected onResumeClick_() {
    this.setPause_(0);
  }

  private async setPause_(mode: number) {
    const response = await this.browserProxy_.setPause(mode);
    this.pauseMode_ = response.pauseMode;
  }

  private async refresh_() {
    const state = await this.browserProxy_.getState();
    await this.updateComplete;
    this.mode_ = state.mode;
    this.$.modeSelect.value = String(state.mode);
    this.$.endpointInput.value = state.endpoint;
    this.$.modelInput.value = state.model;
    this.$.keyInput.value = '';
    this.keyStatus_ = state.hasKey ? this.i18n_('uwOrganizationKeySaved') :
                                     this.i18n_('uwOrganizationNoKey');
    this.enabled_ = state.enabled;
    this.pauseMode_ = state.pauseMode;
    this.modelsStatus_ = state.model ?
        '' :
        this.i18n_('uwOrganizationModelEmpty');
    this.saveStatus_ = '';
    this.saveError_ = false;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-uw-organization-page': SettingsUwOrganizationPageElement;
  }
}

customElements.define(
    SettingsUwOrganizationPageElement.is, SettingsUwOrganizationPageElement);
