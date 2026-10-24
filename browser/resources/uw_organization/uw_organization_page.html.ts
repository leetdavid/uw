// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {SettingsUwOrganizationPageElement} from './uw_organization_page.js';

export function getHtml(this: SettingsUwOrganizationPageElement) {
  // clang-format off
  return html`<!--_html_template_start_-->
<style>
  .uw-section {
    background: var(--cr-card-background-color);
    border-radius: var(--cr-card-border-radius);
    box-shadow: var(--cr-card-shadow);
    box-sizing: border-box;
    margin-block-end: var(--cr-section-vertical-margin);
    padding: 0 var(--cr-section-padding);
  }
  .uw-row {
    align-items: center;
    display: flex;
    gap: 12px;
    min-height: var(--cr-section-min-height);
    padding: 8px 0;
  }
  .uw-row.uw-wrap {
    flex-wrap: wrap;
  }
  #providersLink,
  #providersLink + .uw-row {
    border-top: var(--cr-separator-line);
  }
  .uw-row[hidden] {
    display: none;
  }
  .uw-row + .uw-row {
    border-top: var(--cr-separator-line);
  }
  .uw-label {
    flex: 0 0 120px;
  }
  .uw-field {
    flex: 1;
  }
  .uw-field[hidden] {
    display: none;
  }
  .uw-slider {
    align-items: center;
    display: flex;
    font-variant-numeric: tabular-nums;
    gap: 8px;
  }
  .uw-slider cr-slider {
    flex: 1;
  }
  .uw-secondary,
  .uw-note {
    color: var(--cr-secondary-text-color);
  }
  .uw-note {
    border-top: var(--cr-separator-line);
    padding: 12px 0 16px;
  }
  h2.uw-heading {
    font-size: 108%;
    font-weight: 500;
    margin: 0;
    padding: 16px 0 0;
  }
</style>
<div class="cr-centered-card-container">
  <div class="uw-section">
    <div class="uw-row">
      <div class="uw-field">
        <div id="status">${this.statusTitle_()}</div>
        <div id="statusDetail" class="uw-secondary">
          ${this.statusDetail_()}
        </div>
      </div>
      <cr-toggle id="enabledToggle" aria-labelledby="status"
          ?checked="${!!this.state_?.enabled}"
          @change="${this.onEnabledChange_}">
      </cr-toggle>
    </div>
    <cr-link-row id="providersLink" external embedded
        label="${this.i18n_('uwOrganizationProvidersTitle')}"
        sub-label="${this.i18n_('uwOrganizationProvidersNote')}"
        @click="${this.onProvidersClick_}">
    </cr-link-row>
    ${this.thresholdRows_().map(row => html`
    <div class="uw-row">
      <div class="uw-label">${row.label}</div>
      <div class="uw-field uw-slider">
        <cr-slider id="${row.key}Threshold" data-key="${row.key}"
            aria-label="${row.label}" .max="${row.stops}"
            .value="${row.stop}" snaps
            @cr-slider-value-changed="${this.onThresholdCrSliderValueChanged_}">
        </cr-slider>
        <div id="${row.key}ThresholdValue" class="uw-secondary">
          ${row.shown}
        </div>
      </div>
    </div>`)}
    <div id="thresholdsNote" class="uw-note">
      ${this.i18n_('uwOrganizationThresholdsNote')}
    </div>
    <div class="uw-note">${this.i18n_('uwOrganizationScope')}</div>
  </div>
  <div id="pauseSection" class="uw-section" ?hidden="${!this.state_?.enabled}">
    <h2 class="uw-heading">${this.i18n_('uwOrganizationPauseTitle')}</h2>
    <div class="uw-row uw-wrap">
      <cr-button id="pause1Hour" @click="${this.onPause1HourClick_}"
          ?disabled="${this.state_?.pauseMode === 1}">
        ${this.i18n_('uwOrganizationPause1Hour')}
      </cr-button>
      <cr-button id="pauseUntilLaunch" @click="${this.onPauseUntilLaunchClick_}"
          ?disabled="${this.state_?.pauseMode === 2}">
        ${this.i18n_('uwOrganizationPauseUntilLaunch')}
      </cr-button>
      <cr-button id="pauseIndefinite" @click="${this.onPauseIndefiniteClick_}"
          ?disabled="${this.state_?.pauseMode === 3}">
        ${this.i18n_('uwOrganizationPauseIndefinite')}
      </cr-button>
      <cr-button id="resume" @click="${this.onResumeClick_}"
          ?disabled="${this.state_?.pauseMode === 0}">
        ${this.i18n_('uwOrganizationResume')}
      </cr-button>
    </div>
  </div>
  <div class="uw-section">
    <cr-link-row id="themeLink" embedded
        label="${this.i18n_('uwOrganizationTheme')}"
        sub-label="${this.i18n_('uwOrganizationThemeDescription')}"
        @click="${this.onThemeClick_}">
    </cr-link-row>
    <div class="uw-row" ?hidden="${!this.state_?.canShowGlass}">
      <div id="glassBehindLabel" class="uw-field">
        ${this.i18n_('uwGlassBehind')}
        <div class="uw-secondary">
          ${this.i18n_('uwGlassBehindDescription')}
        </div>
      </div>
      <cr-toggle id="glassBehindToggle" aria-labelledby="glassBehindLabel"
          ?checked="${!!this.state_?.glassBehind}"
          @change="${this.onGlassBehindChange_}">
      </cr-toggle>
    </div>
  </div>
  <div class="uw-section">
    <h2 class="uw-heading">${this.i18n_('uwCommandBarTitle')}</h2>
    <div class="uw-row">
      <div id="commandBarLabel" class="uw-field">
        ${this.i18n_('uwCommandBarToggle')}
      </div>
      <cr-toggle id="commandBarToggle" aria-labelledby="commandBarLabel"
          ?checked="${!!this.state_?.commandBar}"
          @change="${this.onCommandBarChange_}">
      </cr-toggle>
    </div>
    <div class="uw-row">
      <div id="currencyLabel" class="uw-field">
        ${this.i18n_('uwCommandBarCurrency')}
        <div class="uw-secondary">
          ${this.i18n_('uwCommandBarCurrencyDescription')}
        </div>
      </div>
      <select id="currencySelect" class="md-select"
          aria-labelledby="currencyLabel"
          @change="${this.onCurrencySelectChange_}">
        ${this.currencyOptions_().map(item => html`
          <option value="${item.code}" ?selected="${item.selected}">
            ${item.name}
          </option>`)}
      </select>
    </div>
    <div class="uw-note">${this.i18n_('uwCommandBarNote')}</div>
  </div>
  <div class="uw-section">
    <h2 class="uw-heading">${this.i18n_('uwMouseTitle')}</h2>
    ${this.mouseActions_().map(row => html`
    <div class="uw-row">
      <div id="${row.id}Label" class="uw-field">${this.i18n_(row.label)}</div>
      <cr-toggle id="${row.id}Toggle" data-action="${row.action}"
          aria-labelledby="${row.id}Label" ?checked="${row.on}"
          @change="${this.onMouseActionChange_}">
      </cr-toggle>
    </div>`)}
  </div>
</div>
<!--_html_template_end_-->`;
  // clang-format on
}
