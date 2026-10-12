// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview uw settings: the decision model and the language model behind
 * automatic tab organization, how eagerly it moves tabs, the global pause
 * controls, and the window tint. API keys stay in the macOS Keychain; the page
 * only learns whether one is stored.
 */

import 'chrome://resources/cr_elements/cr_button/cr_button.js';
import 'chrome://resources/cr_elements/cr_input/cr_input.js';
import 'chrome://resources/cr_elements/cr_slider/cr_slider.js';
import 'chrome://resources/cr_elements/cr_toggle/cr_toggle.js';

import type {CrSliderElement} from 'chrome://resources/cr_elements/cr_slider/cr_slider.js';
import type {CrToggleElement} from 'chrome://resources/cr_elements/cr_toggle/cr_toggle.js';

import {getCss as getMdSelectLitCss} from 'chrome://resources/cr_elements/md_select_lit.css.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {Connection, ConnectionInput, TestResult, Thresholds, TintState} from './uw_organization.mojom-webui.js';
import {UwOrganizationBrowserProxyImpl} from './uw_organization_browser_proxy.js';
import {getHtml} from './uw_organization_page.html.js';

type ConnectionName = 'decision'|'language';

interface ConnectionForm {
  kind: number;
  endpoint: string;
  model: string;
  key: string;
  hasKey: boolean;
  models: string[];
  modelsStatus: string;
  testing: boolean;
  testStatus: string;
  testFailed: boolean;
  // What the last test sent and what came back.
  testDetail: string;
}

// Everything the template needs to draw one connection.
interface ConnectionCard {
  name: ConnectionName;
  title: string;
  kindLabel: string;
  kinds: Array<{value: number, label: string}>;
  note: string;
  form: ConnectionForm;
  takesEndpoint: boolean;
  takesKey: boolean;
}

interface Defaults {
  endpoint: string;
  model: string;
}

// uw::DecisionProvider and uw::ConnectionMode values.
const NO_DECISION_MODEL = 0;
const OLLAMA = 0;

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

// Lilac, sky, mint, butter, and peach take dark text. Indigo, wine, and forest
// take white text.
const TINT_PRESETS = [
  '#E7E1F6',
  '#D8E6F9',
  '#CFEFDD',
  '#F7E08A',
  '#F9E3D2',
  '#2B2542',
  '#5A1F3A',
  '#1F3A2E',
];

const PROVIDER_LABELS = [
  '',
  'uwOrganizationProviderJev',
  'uwOrganizationProviderOpenai',
  'uwOrganizationOpenaiCompatible',
];

function emptyForm(): ConnectionForm {
  return {
    kind: 0,
    endpoint: '',
    model: '',
    key: '',
    hasKey: false,
    models: [],
    modelsStatus: '',
    testing: false,
    testStatus: '',
    testFailed: false,
    testDetail: '',
  };
}

function defaultsFor(name: ConnectionName, kind: number): Defaults {
  const preset = name === 'decision' ? ['', 'Jev', 'Openai'][kind] :
                                       ['Ollama'][kind];
  if (!preset) {
    return {endpoint: '', model: ''};
  }
  return {
    endpoint: loadTimeData.getString(`uwOrganization${preset}Endpoint`),
    model: name === 'decision' ?
        loadTimeData.getString(`uwOrganization${preset}Model`) :
        '',
  };
}

function formFrom(name: ConnectionName, saved: Connection): ConnectionForm {
  return {
    ...emptyForm(),
    kind: saved.kind,
    endpoint: saved.endpoint || defaultsFor(name, saved.kind).endpoint,
    model: saved.model,
    hasKey: saved.hasKey,
  };
}

function inputFrom(form: ConnectionForm, takesKey: boolean): ConnectionInput {
  return {
    kind: form.kind,
    endpoint: form.endpoint.trim(),
    model: form.model.trim(),
    apiKey: takesKey ? form.key : '',
  };
}

export class SettingsUwOrganizationPageElement extends CrLitElement {
  static get is() {
    return 'settings-uw-organization-page';
  }

  static override get styles() {
    return [getMdSelectLitCss()];
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      decision_: {type: Object},
      language_: {type: Object},
      saved_: {type: Object},
      saveStatus_: {type: String},
      saveError_: {type: Boolean},
      saving_: {type: Boolean},
      enabled_: {type: Boolean},
      pauseMode_: {type: Number},
      thresholds_: {type: Object},
      tint_: {type: Object},
    };
  }

  protected accessor decision_: ConnectionForm = emptyForm();
  protected accessor language_: ConnectionForm = emptyForm();
  // The connections as last saved, for the status line.
  protected accessor saved_: {decisionKind: number, languageModel: string} =
      {decisionKind: NO_DECISION_MODEL, languageModel: ''};
  protected accessor saveStatus_: string = '';
  protected accessor saveError_: boolean = false;
  protected accessor saving_: boolean = false;
  protected accessor enabled_: boolean = false;
  protected accessor pauseMode_: number = 0;
  // Null until the saved thresholds have loaded.
  protected accessor thresholds_: Thresholds|null = null;
  protected accessor tint_: TintState = {picked: '', applied: ''};
  protected tintPresets_ = TINT_PRESETS;
  private browserProxy_ = UwOrganizationBrowserProxyImpl.getInstance();

  override connectedCallback() {
    super.connectedCallback();
    this.refresh_();
  }

  i18n_(id: string): string {
    return loadTimeData.getString(id);
  }

  private form_(name: ConnectionName): ConnectionForm {
    return name === 'decision' ? this.decision_ : this.language_;
  }

  private takesKey_(name: ConnectionName): boolean {
    return name === 'decision' ? this.decision_.kind !== NO_DECISION_MODEL :
                                 this.language_.kind !== OLLAMA;
  }

  protected cards_(): ConnectionCard[] {
    const compatible = this.i18n_('uwOrganizationOpenaiCompatible');
    return [
      {
        name: 'decision',
        title: this.i18n_('uwOrganizationDecisionTitle'),
        kindLabel: this.i18n_('uwOrganizationProvider'),
        kinds: [
          {value: 0, label: this.i18n_('uwOrganizationProviderNone')},
          {value: 1, label: this.i18n_('uwOrganizationProviderJev')},
          {value: 2, label: this.i18n_('uwOrganizationProviderOpenai')},
          {value: 3, label: compatible},
        ],
        note: this.i18n_('uwOrganizationDecisionNote'),
        form: this.decision_,
        takesEndpoint: this.decision_.kind !== NO_DECISION_MODEL,
        takesKey: this.takesKey_('decision'),
      },
      {
        name: 'language',
        title: this.i18n_('uwOrganizationLanguageTitle'),
        kindLabel: this.i18n_('uwOrganizationConnection'),
        kinds: [
          {value: 0, label: this.i18n_('uwOrganizationOllama')},
          {value: 1, label: compatible},
        ],
        note: this.i18n_('uwOrganizationLanguageNote'),
        form: this.language_,
        takesEndpoint: true,
        takesKey: this.takesKey_('language'),
      },
    ];
  }

  protected statusTitle_(): string {
    if (!this.enabled_) {
      return this.i18n_('uwOrganizationStatusOff');
    }
    return this.i18n_(
        this.pauseMode_ === 0 ? 'uwOrganizationStatusOn' :
                                'uwOrganizationStatusPaused');
  }

  protected statusDetail_(): string {
    if (!this.enabled_) {
      return this.i18n_('uwOrganizationStatusHint');
    }
    const {decisionKind, languageModel} = this.saved_;
    if (decisionKind === NO_DECISION_MODEL) {
      return loadTimeData.getStringF(
          'uwOrganizationStatusDecidesAndNames', languageModel);
    }
    const decides = loadTimeData.getStringF(
        'uwOrganizationStatusDecides',
        this.i18n_(PROVIDER_LABELS[decisionKind]!));
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

  private update_(name: ConnectionName, changes: Partial<ConnectionForm>) {
    const form = {...this.form_(name), ...changes};
    if (name === 'decision') {
      this.decision_ = form;
    } else {
      this.language_ = form;
    }
  }

  private connectionOf_(e: Event): ConnectionName {
    return (e.currentTarget as HTMLElement).dataset['connection'] as
        ConnectionName;
  }

  protected onKindChange_(e: Event) {
    const name = this.connectionOf_(e);
    const kind = Number((e.currentTarget as HTMLSelectElement).value);
    this.update_(
        name,
        {kind, ...defaultsFor(name, kind), models: [], modelsStatus: ''});
  }

  protected onFieldValueChanged_(e: CustomEvent<{value: string}>) {
    const field = (e.currentTarget as HTMLElement).dataset['field'];
    const value = e.detail.value;
    this.update_(
        this.connectionOf_(e),
        field === 'endpoint' ? {endpoint: value} :
            field === 'model' ? {model: value} : {key: value});
  }

  protected onModelChange_(e: Event) {
    this.update_(
        this.connectionOf_(e),
        {model: (e.currentTarget as HTMLSelectElement).value});
  }

  protected async onLoadModelsClick_(e: Event) {
    const name = this.connectionOf_(e);
    this.update_(name, {modelsStatus: this.i18n_('uwOrganizationLoading')});
    const response = await this.browserProxy_.loadModels(
        name === 'decision',
        inputFrom(this.form_(name), this.takesKey_(name)));
    if (!response.ok || response.models.length === 0) {
      this.update_(name, {
        models: [],
        modelsStatus: this.i18n_(
            response.ok ? 'uwOrganizationModelsEmpty' :
                          'uwOrganizationModelsError'),
      });
      return;
    }
    const current = this.form_(name).model.trim();
    this.update_(name, {
      models: response.models,
      model: response.models.includes(current) ? current : response.models[0]!,
      modelsStatus: loadTimeData.getStringF(
          'uwOrganizationModelsLoaded', response.models.length),
    });
  }

  private showTest_(name: ConnectionName, result: TestResult|null) {
    this.update_(name, {
      testing: false,
      testStatus: result ? result.summary : '',
      testFailed: !!result && !result.ok,
      testDetail: result ? result.detail : '',
    });
  }

  // Tests one connection as entered, without saving it. Beside a decision
  // model the language model only names tasks, so that is what its test asks.
  protected async onTestClick_(e: Event) {
    const name = this.connectionOf_(e);
    this.update_(name, {
      testing: true,
      testStatus: this.i18n_('uwOrganizationChecking'),
      testFailed: false,
      testDetail: '',
    });
    this.showTest_(
        name,
        await this.browserProxy_.testConnection(
            name === 'decision',
            inputFrom(this.form_(name), this.takesKey_(name)),
            this.decision_.kind !== NO_DECISION_MODEL));
  }

  protected async onSaveClick_() {
    if (this.saving_) {
      return;
    }
    this.saving_ = true;
    this.saveError_ = false;
    this.saveStatus_ = this.i18n_('uwOrganizationChecking');
    const result = await this.browserProxy_.save(
        inputFrom(this.decision_, this.takesKey_('decision')),
        inputFrom(this.language_, this.takesKey_('language')));
    await this.refresh_();
    this.showTest_('decision', result.decision);
    this.showTest_('language', result.language);
    this.saving_ = false;
    this.saveError_ = !result.enabled;
    if (result.enabled) {
      this.saveStatus_ = this.i18n_('uwOrganizationValidated');
      return;
    }
    const errors = [
      result.decision && !result.decision.ok ? 'uwOrganizationDecisionError' :
                                               '',
      result.language && !result.language.ok ? 'uwOrganizationLanguageError' :
                                               '',
    ].filter(id => id);
    this.saveStatus_ =
        (errors.length ? errors : ['uwOrganizationNothingSet'])
            .map(id => this.i18n_(id))
            .join(' ');
  }

  // Turning it on saves and tests the connections, like the save button.
  // Turning it off leaves them as they are.
  protected async onEnabledChange_(e: CustomEvent<boolean>) {
    const toggle = e.currentTarget as CrToggleElement;
    if (e.detail) {
      await this.onSaveClick_();
      this.showWhyOff_();
    } else {
      await this.browserProxy_.turnOff();
      await this.refresh_();
      this.saveStatus_ = '';
      this.saveError_ = false;
    }
    toggle.checked = this.enabled_;
  }

  // Brings the reason automatic organization stayed off into view.
  private showWhyOff_() {
    if (this.enabled_) {
      return;
    }
    const reason = this.decision_.testFailed ?
        'decisionTestStatus' :
        this.language_.testFailed ? 'languageTestStatus' : 'saveStatus';
    this.shadowRoot?.getElementById(reason)?.scrollIntoView({block: 'center'});
  }

  protected thresholdRows_(): ThresholdRow[] {
    const thresholds = this.thresholds_;
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
    const thresholds = this.thresholds_;
    if (!thresholds) {
      return;
    }
    this.thresholds_ = await this.browserProxy_.setThresholds({
      ...thresholds,
      [slider.dataset['key'] as keyof Thresholds]:
          MIN_THRESHOLD + slider.value * THRESHOLD_STEP,
    });
  }

  protected swatchLabel_(color: string): string {
    return loadTimeData.getStringF('uwOrganizationTintSwatch', color);
  }

  // A color input only takes lowercase "#rrggbb".
  protected customTintValue_(): string {
    return (this.tint_.picked ||
            loadTimeData.getString('uwOrganizationDefaultTint'))
        .toLowerCase();
  }

  protected tintStatus_(): string {
    const {picked, applied} = this.tint_;
    if (!picked) {
      return this.i18n_('uwOrganizationTintStatusDefault');
    }
    return picked === applied ?
        loadTimeData.getStringF('uwOrganizationTintStatusPicked', picked) :
        loadTimeData.getStringF(
            'uwOrganizationTintStatusAdjusted', picked, applied);
  }

  protected async onSwatchClick_(e: Event) {
    this.tint_ = await this.browserProxy_.setTint(
        (e.currentTarget as HTMLElement).dataset['color']!);
  }

  protected async onCustomTintChange_(e: Event) {
    this.tint_ = await this.browserProxy_.setTint(
        (e.currentTarget as HTMLInputElement).value.toUpperCase());
  }

  protected async onDefaultTintClick_() {
    this.tint_ = await this.browserProxy_.clearTint();
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
    this.decision_ = {
      ...formFrom('decision', state.decision),
      models: this.decision_.models,
    };
    this.language_ = {
      ...formFrom('language', state.language),
      models: this.language_.models,
    };
    this.saved_ = {
      decisionKind: state.decision.kind,
      languageModel: state.language.model,
    };
    this.enabled_ = state.enabled;
    this.pauseMode_ = state.pauseMode;
    this.thresholds_ = state.thresholds;
    this.tint_ = await this.browserProxy_.getTint();
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-uw-organization-page': SettingsUwOrganizationPageElement;
  }
}

customElements.define(
    SettingsUwOrganizationPageElement.is, SettingsUwOrganizationPageElement);
