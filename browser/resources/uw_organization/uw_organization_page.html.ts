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
  .uw-row + .uw-row {
    border-top: var(--cr-separator-line);
  }
  .uw-label {
    flex: 0 0 140px;
  }
  .uw-field {
    flex: 1;
  }
  .uw-status-error {
    color: var(--cr-fallback-color-error, red);
  }
  .uw-scope {
    color: var(--cr-secondary-text-color);
    padding: 8px 0 16px;
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
    <h2 class="uw-heading">${this.i18n_('uwOrganizationConnectionTitle')}</h2>
    <div class="uw-row">
      <div class="uw-label">${this.i18n_('uwOrganizationModeLabel')}</div>
      <select id="modeSelect" class="uw-field" aria-label="${
          this.i18n_('uwOrganizationModeLabel')}"
          @change="${this.onModeChange_}">
        <option value="0">${this.i18n_('uwOrganizationModeOllama')}</option>
        <option value="1">${this.i18n_('uwOrganizationModeOpenAi')}</option>
      </select>
    </div>
    <div class="uw-row">
      <div class="uw-label">${this.i18n_('uwOrganizationEndpointLabel')}</div>
      <cr-input id="endpointInput" class="uw-field" type="url"></cr-input>
    </div>
    <div class="uw-row">
      <cr-button id="loadModelsButton" @click="${this.onLoadModelsClick_}">
        ${this.i18n_('uwOrganizationLoadModelsLabel')}
      </cr-button>
      <div>${this.modelsStatus_}</div>
    </div>
    <div class="uw-row">
      <div class="uw-label">${this.i18n_('uwOrganizationModelLabel')}</div>
      <cr-input id="modelInput" class="uw-field"></cr-input>
    </div>
    <div class="uw-row" ?hidden="${this.isOllama_()}">
      <div class="uw-label">${this.i18n_('uwOrganizationKeyLabel')}</div>
      <cr-input id="keyInput" class="uw-field" type="password"></cr-input>
    </div>
    <div class="uw-row" ?hidden="${this.isOllama_()}">
      <div>${this.keyStatus_}</div>
    </div>
    <div class="uw-scope">${this.i18n_('uwOrganizationScope')}</div>
    <div class="uw-row">
      <cr-button id="saveButton" class="action-button"
          @click="${this.onSaveClick_}">
        ${this.i18n_('uwOrganizationSaveLabel')}
      </cr-button>
      <div class="${this.saveError_ ? 'uw-status-error' : ''}">${
          this.saveStatus_}</div>
    </div>
    <div class="uw-row" ?hidden="${!this.enabled_}">
      <div>${this.i18n_('uwOrganizationEnabled')}</div>
    </div>
  </div>
  <div class="uw-section">
    <h2 class="uw-heading">${this.i18n_('uwOrganizationPauseTitle')}</h2>
    <div class="uw-row">
      <cr-button @click="${this.onPause1HourClick_}"
          ?disabled="${this.pauseMode_ === 1}">
        ${this.i18n_('uwOrganizationPause1Hour')}
      </cr-button>
      <cr-button @click="${this.onPauseUntilLaunchClick_}"
          ?disabled="${this.pauseMode_ === 2}">
        ${this.i18n_('uwOrganizationPauseUntilLaunch')}
      </cr-button>
      <cr-button @click="${this.onPauseIndefiniteClick_}"
          ?disabled="${this.pauseMode_ === 3}">
        ${this.i18n_('uwOrganizationPauseIndefinite')}
      </cr-button>
      <cr-button @click="${this.onResumeClick_}"
          ?disabled="${this.pauseMode_ === 0}">
        ${this.i18n_('uwOrganizationResume')}
      </cr-button>
    </div>
  </div>
</div>
<!--_html_template_end_-->`;
  // clang-format on
}
