// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** @fileoverview Handles communication for the uw organization page. */

import type {ConnectionInput, ModelsResult, PauseResult, SaveResult, State, TestResult, TintState} from './uw_organization.mojom-webui.js';
import {OrganizationHandler, OrganizationHandlerRemote} from './uw_organization.mojom-webui.js';

export interface UwOrganizationBrowserProxy {
  getState(): Promise<State>;
  loadModels(decision: boolean, connection: ConnectionInput):
      Promise<ModelsResult>;
  testConnection(
      decision: boolean, connection: ConnectionInput,
      namesOnly: boolean): Promise<TestResult>;
  save(decision: ConnectionInput, language: ConnectionInput):
      Promise<SaveResult>;
  turnOff(): Promise<void>;
  setPause(mode: number): Promise<PauseResult>;
  getTint(): Promise<TintState>;
  setTint(color: string): Promise<TintState>;
  clearTint(): Promise<TintState>;
}

export class UwOrganizationBrowserProxyImpl implements
    UwOrganizationBrowserProxy {
  private handler: OrganizationHandlerRemote =
      OrganizationHandler.getRemote();

  getState() {
    return this.handler.getState().then(response => response.state);
  }

  loadModels(decision: boolean, connection: ConnectionInput) {
    return this.handler.loadModels(decision, connection)
        .then(response => response.result);
  }

  testConnection(
      decision: boolean, connection: ConnectionInput, namesOnly: boolean) {
    return this.handler.testConnection(decision, connection, namesOnly)
        .then(response => response.result);
  }

  save(decision: ConnectionInput, language: ConnectionInput) {
    return this.handler.save(decision, language)
        .then(response => response.result);
  }

  turnOff() {
    return this.handler.turnOff();
  }

  setPause(mode: number) {
    return this.handler.setPause(mode).then(response => response.result);
  }

  getTint() {
    return this.handler.getTint().then(response => response.tint);
  }

  setTint(color: string) {
    return this.handler.setTint(color).then(response => response.tint);
  }

  clearTint() {
    return this.handler.clearTint().then(response => response.tint);
  }

  static getInstance(): UwOrganizationBrowserProxy {
    return instance || (instance = new UwOrganizationBrowserProxyImpl());
  }

  static setInstance(obj: UwOrganizationBrowserProxy) {
    instance = obj;
  }
}

let instance: UwOrganizationBrowserProxy|null = null;
