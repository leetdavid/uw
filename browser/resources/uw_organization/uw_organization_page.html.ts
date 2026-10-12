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
  .uw-swatches {
    display: flex;
    flex: 1;
    flex-wrap: wrap;
    gap: 8px;
  }
  .uw-swatch {
    appearance: none;
    block-size: 28px;
    border: 1px solid var(--cr-separator-color);
    border-radius: 50%;
    cursor: pointer;
    inline-size: 28px;
    padding: 0;
  }
  .uw-swatch[aria-pressed='true'] {
    outline: 2px solid var(--cr-focus-outline-color);
    outline-offset: 2px;
  }
  .uw-any-color {
    background: conic-gradient(
        #f7978a, #f7e08a, #cfefdd, #d8e6f9, #e7e1f6, #f7978a);
    box-sizing: border-box;
    display: inline-block;
    overflow: hidden;
  }
  .uw-any-color input {
    block-size: 100%;
    border: none;
    cursor: pointer;
    inline-size: 100%;
    opacity: 0;
    padding: 0;
  }
  .uw-status-error {
    color: var(--cr-fallback-color-error, red);
  }
  .uw-details {
    border-top: var(--cr-separator-line);
    padding: 12px 0;
  }
  .uw-details summary {
    color: var(--cr-secondary-text-color);
    cursor: pointer;
  }
  .uw-details pre {
    font-size: 12px;
    margin: 8px 0 0;
    max-height: 280px;
    overflow: auto;
    overflow-wrap: anywhere;
    white-space: pre-wrap;
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
          ?checked="${this.enabled_}" ?disabled="${this.saving_}"
          @change="${this.onEnabledChange_}">
      </cr-toggle>
    </div>
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
  </div>
  ${this.cards_().map(card => html`
  <div class="uw-section">
    <h2 class="uw-heading">${card.title}</h2>
    <div class="uw-row">
      <div class="uw-label">${card.kindLabel}</div>
      <select id="${card.name}Kind" class="md-select uw-field"
          aria-label="${card.kindLabel}" data-connection="${card.name}"
          .value="${String(card.form.kind)}" @change="${this.onKindChange_}">
        ${card.kinds.map(kind => html`
          <option value="${kind.value}"
              ?selected="${kind.value === card.form.kind}">
            ${kind.label}
          </option>`)}
      </select>
    </div>
    <div class="uw-row" ?hidden="${!card.takesEndpoint}">
      <div class="uw-label">${this.i18n_('uwOrganizationEndpoint')}</div>
      <cr-input id="${card.name}Endpoint" class="uw-field" type="url"
          aria-label="${this.i18n_('uwOrganizationEndpoint')}"
          data-connection="${card.name}" data-field="endpoint"
          .value="${card.form.endpoint}"
          @value-changed="${this.onFieldValueChanged_}">
      </cr-input>
    </div>
    <div class="uw-row" ?hidden="${!card.takesEndpoint}">
      <div class="uw-label">${this.i18n_('uwOrganizationModel')}</div>
      <cr-input id="${card.name}Model" class="uw-field"
          aria-label="${this.i18n_('uwOrganizationModel')}"
          data-connection="${card.name}" data-field="model"
          .value="${card.form.model}"
          @value-changed="${this.onFieldValueChanged_}"
          ?hidden="${card.form.models.length > 0}">
      </cr-input>
      <select id="${card.name}ModelSelect" class="md-select uw-field"
          aria-label="${this.i18n_('uwOrganizationModel')}"
          data-connection="${card.name}" @change="${this.onModelChange_}"
          ?hidden="${card.form.models.length === 0}">
        ${card.form.models.map(model => html`
          <option value="${model}" ?selected="${model === card.form.model}">
            ${model}
          </option>`)}
      </select>
      <cr-button id="${card.name}LoadModels" data-connection="${card.name}"
          @click="${this.onLoadModelsClick_}">
        ${this.i18n_('uwOrganizationLoadModels')}
      </cr-button>
    </div>
    <div class="uw-row" ?hidden="${!card.form.modelsStatus}">
      <div id="${card.name}ModelsStatus" class="uw-secondary">
        ${card.form.modelsStatus}
      </div>
    </div>
    <div class="uw-row" ?hidden="${!card.takesKey}">
      <div class="uw-label">${this.i18n_('uwOrganizationKey')}</div>
      <cr-input id="${card.name}Key" class="uw-field" type="password"
          aria-label="${this.i18n_('uwOrganizationKey')}"
          data-connection="${card.name}" data-field="key"
          .value="${card.form.key}" @value-changed="${this.onFieldValueChanged_}">
      </cr-input>
      <div class="uw-secondary">${this.i18n_(
          card.form.hasKey ? 'uwOrganizationKeySaved' :
                             'uwOrganizationNoKey')}</div>
    </div>
    <div class="uw-row" ?hidden="${!card.takesEndpoint}">
      <cr-button id="${card.name}Test" data-connection="${card.name}"
          ?disabled="${card.form.testing || this.saving_}"
          @click="${this.onTestClick_}">
        ${this.i18n_('uwOrganizationTest')}
      </cr-button>
      <div id="${card.name}TestStatus"
          class="${card.form.testFailed ? 'uw-status-error' : ''}">
        ${card.form.testStatus}
      </div>
    </div>
    <details id="${card.name}TestDetails" class="uw-details"
        ?hidden="${!card.form.testDetail}">
      <summary>${this.i18n_('uwOrganizationTestDetails')}</summary>
      <pre id="${card.name}TestDetail">${card.form.testDetail}</pre>
    </details>
    <div class="uw-note">${card.note}</div>
  </div>`)}
  <div class="uw-section">
    <div class="uw-row">
      <cr-button id="saveButton" class="action-button"
          ?disabled="${this.saving_}" @click="${this.onSaveClick_}">
        ${this.i18n_('uwOrganizationSave')}
      </cr-button>
      <div id="saveStatus"
          class="${this.saveError_ ? 'uw-status-error' : ''}">
        ${this.saveStatus_}
      </div>
    </div>
    <div class="uw-note">${this.i18n_('uwOrganizationScope')}</div>
  </div>
  <div class="uw-section">
    <h2 class="uw-heading">${this.i18n_('uwOrganizationPauseTitle')}</h2>
    <div class="uw-row">
      <cr-button id="pause1Hour" @click="${this.onPause1HourClick_}"
          ?disabled="${this.pauseMode_ === 1}">
        ${this.i18n_('uwOrganizationPause1Hour')}
      </cr-button>
      <cr-button id="pauseUntilLaunch" @click="${this.onPauseUntilLaunchClick_}"
          ?disabled="${this.pauseMode_ === 2}">
        ${this.i18n_('uwOrganizationPauseUntilLaunch')}
      </cr-button>
      <cr-button id="pauseIndefinite" @click="${this.onPauseIndefiniteClick_}"
          ?disabled="${this.pauseMode_ === 3}">
        ${this.i18n_('uwOrganizationPauseIndefinite')}
      </cr-button>
      <cr-button id="resume" @click="${this.onResumeClick_}"
          ?disabled="${this.pauseMode_ === 0}">
        ${this.i18n_('uwOrganizationResume')}
      </cr-button>
    </div>
  </div>
  <div class="uw-section">
    <h2 class="uw-heading">${this.i18n_('uwOrganizationTintTitle')}</h2>
    <div class="uw-row">
      <div class="uw-label">${this.i18n_('uwOrganizationTintColor')}</div>
      <div class="uw-swatches">
        ${this.tintPresets_.map(color => html`
          <button class="uw-swatch" style="background: ${color}"
              data-color="${color}"
              aria-label="${this.swatchLabel_(color)}"
              aria-pressed="${color === this.tint_.picked}"
              @click="${this.onSwatchClick_}">
          </button>`)}
        <label class="uw-swatch uw-any-color"
            title="${this.i18n_('uwOrganizationTintCustom')}">
          <input id="customTint" type="color"
              aria-label="${this.i18n_('uwOrganizationTintCustom')}"
              .value="${this.customTintValue_()}"
              @change="${this.onCustomTintChange_}">
        </label>
      </div>
      <cr-button id="defaultTint" ?disabled="${!this.tint_.picked}"
          @click="${this.onDefaultTintClick_}">
        ${this.i18n_('uwOrganizationTintDefault')}
      </cr-button>
    </div>
    <div class="uw-row">
      <div id="tintStatus" class="uw-secondary">${this.tintStatus_()}</div>
    </div>
    <div class="uw-note">${this.i18n_('uwOrganizationTintNote')}</div>
  </div>
</div>
<!--_html_template_end_-->`;
  // clang-format on
}
