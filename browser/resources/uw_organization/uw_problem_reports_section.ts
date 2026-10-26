// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Problem reports on Settings, Privacy and security: the switch
 * that holds the answer to "Help fix uw?", which only a release shows, and a
 * link that opens a new GitHub issue. See docs/plans/distribution.md.
 */

import 'chrome://resources/cr_elements/cr_link_row/cr_link_row.js';
import 'chrome://resources/cr_elements/cr_toggle/cr_toggle.js';

import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {ProblemReportsState} from './uw_organization.mojom-webui.js';
import {UwOrganizationBrowserProxyImpl} from './uw_organization_browser_proxy.js';
import {getHtml} from './uw_problem_reports_section.html.js';

export class SettingsUwProblemReportsSectionElement extends CrLitElement {
  static get is() {
    return 'settings-uw-problem-reports-section';
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      state_: {type: Object},
    };
  }

  protected accessor state_: ProblemReportsState|null = null;
  private browserProxy_ = UwOrganizationBrowserProxyImpl.getInstance();

  override connectedCallback() {
    super.connectedCallback();
    this.browserProxy_.getProblemReports().then(state => {
      this.state_ = state;
    });
  }

  i18n_(id: string): string {
    return loadTimeData.getString(id);
  }

  protected async onSendChange_(event: CustomEvent<boolean>) {
    if (!this.state_) {
      return;
    }
    this.state_ = {...this.state_, send: event.detail};
    await this.browserProxy_.setProblemReports(event.detail);
  }

  protected onReportIssueClick_() {
    if (this.state_) {
      window.open(this.state_.newIssueUrl);
    }
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-uw-problem-reports-section':
        SettingsUwProblemReportsSectionElement;
  }
}

customElements.define(
    SettingsUwProblemReportsSectionElement.is,
    SettingsUwProblemReportsSectionElement);
