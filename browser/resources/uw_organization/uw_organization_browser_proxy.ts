// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** @fileoverview Handles communication for the uw organization page. */

import type {PauseResult, State, Thresholds} from './uw_organization.mojom-webui.js';
import {OrganizationHandler, OrganizationHandlerRemote} from './uw_organization.mojom-webui.js';

export interface UwOrganizationBrowserProxy {
  getState(): Promise<State>;
  setEnabled(on: boolean): Promise<State>;
  setPause(mode: number): Promise<PauseResult>;
  setThresholds(thresholds: Thresholds): Promise<Thresholds>;
  openThemePicker(): void;
  setCommandBar(on: boolean): Promise<void>;
  setCurrency(code: string): Promise<void>;
}

export class UwOrganizationBrowserProxyImpl implements
    UwOrganizationBrowserProxy {
  private handler: OrganizationHandlerRemote =
      OrganizationHandler.getRemote();

  getState() {
    return this.handler.getState().then(response => response.state);
  }

  setEnabled(on: boolean) {
    return this.handler.setEnabled(on).then(response => response.state);
  }

  setPause(mode: number) {
    return this.handler.setPause(mode).then(response => response.result);
  }

  setThresholds(thresholds: Thresholds) {
    return this.handler.setThresholds(thresholds).then(
        response => response.thresholds);
  }

  openThemePicker() {
    this.handler.openThemePicker();
  }

  async setCommandBar(on: boolean) {
    await this.handler.setCommandBar(on);
  }

  async setCurrency(code: string) {
    await this.handler.setCurrency(code);
  }

  static getInstance(): UwOrganizationBrowserProxy {
    return instance || (instance = new UwOrganizationBrowserProxyImpl());
  }

  static setInstance(obj: UwOrganizationBrowserProxy) {
    instance = obj;
  }
}

let instance: UwOrganizationBrowserProxy|null = null;
