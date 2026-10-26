// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {SettingsUwProblemReportsSectionElement} from './uw_problem_reports_section.js';

export function getHtml(this: SettingsUwProblemReportsSectionElement) {
  // clang-format off
  return html`<!--_html_template_start_-->
<style>
  :host {
    display: block;
  }
  h3 {
    font-size: 13px;
    font-weight: 600;
    margin: 0;
    padding: 16px var(--cr-section-padding) 0;
  }
  .row {
    align-items: center;
    display: flex;
    gap: 16px;
    min-height: var(--cr-section-min-height);
    padding: 12px var(--cr-section-padding);
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
  ul {
    margin: 6px 0 0;
    padding-inline-start: 16px;
  }
  cr-link-row {
    border-top: var(--cr-separator-line);
  }
</style>
<h3>${this.i18n_('uwProblemReportsTitle')}</h3>
${this.state_?.available ? html`
  <div class="row" id="sendRow">
    <div class="text">
      <div id="sendLabel">${this.i18n_('uwProblemReportsSend')}</div>
      <div class="secondary">${this.i18n_('uwProblemReportsSendNote')}</div>
      <ul class="secondary">
        <li>${this.i18n_('uwProblemReportsCrashes')}</li>
        <li>${this.i18n_('uwProblemReportsPages')}</li>
        <li>${this.i18n_('uwProblemReportsWebsites')}</li>
      </ul>
    </div>
    <cr-toggle id="sendToggle" aria-labelledby="sendLabel"
        ?checked="${this.state_.send}"
        @change="${this.onSendChange_}">
    </cr-toggle>
  </div>
` : ''}
<cr-link-row id="reportIssue" external
    label="${this.i18n_('uwReportIssue')}"
    sub-label="${this.i18n_('uwReportIssueNote')}"
    @click="${this.onReportIssueClick_}">
</cr-link-row>
<!--_html_template_end_-->`;
  // clang-format on
}
