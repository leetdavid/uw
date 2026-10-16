// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview uw settings: whether automatic tab organization runs, how
 * eagerly it moves tabs, the global pause controls, a row that opens the theme
 * picker, and the command bar. The models live on uw://providers.
 */

import 'chrome://resources/cr_elements/cr_button/cr_button.js';
import 'chrome://resources/cr_elements/cr_link_row/cr_link_row.js';
import 'chrome://resources/cr_elements/cr_slider/cr_slider.js';
import 'chrome://resources/cr_elements/cr_toggle/cr_toggle.js';

import type {CrSliderElement} from 'chrome://resources/cr_elements/cr_slider/cr_slider.js';
import type {CrToggleElement} from 'chrome://resources/cr_elements/cr_toggle/cr_toggle.js';

import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {Waiting} from './uw_organization.mojom-webui.js';
import type {State, Thresholds} from './uw_organization.mojom-webui.js';
import {UwOrganizationBrowserProxyImpl} from './uw_organization_browser_proxy.js';
import {getHtml} from './uw_organization_page.html.js';

// uw::kMinThreshold and uw::kMaxThreshold, and the step between two stops of
// a threshold slider.
const MIN_THRESHOLD = 0.5;
const MAX_THRESHOLD = 0.95;
const THRESHOLD_STEP = 0.05;

// One slider of the page: what the threshold is for, the slider's last stop
// and current stop, and the threshold as shown beside it.
interface ThresholdRow {
  key: keyof Thresholds;
  label: string;
  stops: number;
  stop: number;
  shown: string;
}

const WAITING_TEXT: Record<Waiting, string> = {
  [Waiting.kNothing]: '',
  [Waiting.kNoModel]: 'uwOrganizationWaitingNoModel',
};

export class SettingsUwOrganizationPageElement extends CrLitElement {
  static get is() {
    return 'settings-uw-organization-page';
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      state_: {type: Object},
    };
  }

  // Null until the saved state has loaded.
  protected accessor state_: State|null = null;
  private browserProxy_ = UwOrganizationBrowserProxyImpl.getInstance();

  override connectedCallback() {
    super.connectedCallback();
    this.refresh_();
    // The models are picked and tested on uw://providers, often in another
    // tab, so the status follows when this one is shown again.
    document.addEventListener('visibilitychange', this.onVisible_);
  }

  override disconnectedCallback() {
    super.disconnectedCallback();
    document.removeEventListener('visibilitychange', this.onVisible_);
  }

  private onVisible_ = () => {
    if (document.visibilityState === 'visible') {
      this.refresh_();
    }
  };

  i18n_(id: string): string {
    return loadTimeData.getString(id);
  }

  protected statusTitle_(): string {
    const state = this.state_;
    if (!state?.enabled) {
      return this.i18n_('uwOrganizationStatusOff');
    }
    if (state.waiting !== Waiting.kNothing) {
      return this.i18n_('uwOrganizationStatusWaiting');
    }
    return this.i18n_(
        state.pauseMode === 0 ? 'uwOrganizationStatusOn' :
                                'uwOrganizationStatusPaused');
  }

  protected statusDetail_(): string {
    const state = this.state_;
    if (!state?.enabled) {
      return this.i18n_('uwOrganizationStatusHint');
    }
    if (state.waiting !== Waiting.kNothing) {
      return this.i18n_(WAITING_TEXT[state.waiting]);
    }
    const {decisionModel, languageModel} = state;
    if (!decisionModel) {
      return loadTimeData.getStringF(
          'uwOrganizationStatusDecidesAndNames', languageModel);
    }
    const decides =
        loadTimeData.getStringF('uwOrganizationStatusDecides', decisionModel);
    return languageModel ?
        `${decides} ${
            loadTimeData.getStringF(
                'uwOrganizationStatusNames', languageModel)}` :
        decides;
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

  protected async onEnabledChange_(e: CustomEvent<boolean>) {
    // The event has finished dispatching by the time the answer comes.
    const toggle = e.currentTarget as CrToggleElement;
    this.state_ = await this.browserProxy_.setEnabled(e.detail);
    toggle.checked = this.state_.enabled;
  }

  protected onProvidersClick_() {
    window.open('chrome://providers/');
  }

  protected thresholdRows_(): ThresholdRow[] {
    const thresholds = this.state_?.thresholds;
    if (!thresholds) {
      return [];
    }
    const labels: Array<[keyof Thresholds, string]> = [
      ['move', 'uwOrganizationThresholdMove'],
      ['newTask', 'uwOrganizationThresholdNewTask'],
      ['rename', 'uwOrganizationThresholdRename'],
    ];
    return labels.map(([key, label]) => ({
                        key,
                        label: this.i18n_(label),
                        stops: Math.round(
                            (MAX_THRESHOLD - MIN_THRESHOLD) / THRESHOLD_STEP),
                        stop: Math.round(
                            (thresholds[key] - MIN_THRESHOLD) / THRESHOLD_STEP),
                        shown: thresholds[key].toFixed(2),
                      }));
  }

  protected async onThresholdCrSliderValueChanged_(e: Event) {
    const slider = e.currentTarget as CrSliderElement;
    const state = this.state_;
    if (!state) {
      return;
    }
    const thresholds = await this.browserProxy_.setThresholds({
      ...state.thresholds,
      [slider.dataset['key'] as keyof Thresholds]:
          MIN_THRESHOLD + slider.value * THRESHOLD_STEP,
    });
    this.state_ = {...state, thresholds};
  }

  protected onThemeClick_() {
    this.browserProxy_.openThemePicker();
  }

  protected async onCommandBarChange_(e: CustomEvent<boolean>) {
    await this.browserProxy_.setCommandBar(e.detail);
    this.refresh_();
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
    await this.browserProxy_.setPause(mode);
    this.refresh_();
  }


  private async refresh_() {
    this.state_ = await this.browserProxy_.getState();
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-uw-organization-page': SettingsUwOrganizationPageElement;
  }
}

customElements.define(
    SettingsUwOrganizationPageElement.is, SettingsUwOrganizationPageElement);
