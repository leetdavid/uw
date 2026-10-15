// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** @fileoverview Handles communication for the uw organization page. */

import type {ConnectionInput, ConnectionSlot, ModelsResult, PauseResult, SaveResult, State, TestResult, Thresholds, TintState} from './uw_organization.mojom-webui.js';
import {OrganizationHandler, OrganizationHandlerRemote} from './uw_organization.mojom-webui.js';

export interface UwOrganizationBrowserProxy {
  getState(): Promise<State>;
  loadModels(slot: ConnectionSlot, connection: ConnectionInput):
      Promise<ModelsResult>;
  testConnection(
      slot: ConnectionSlot, connection: ConnectionInput,
      namesOnly: boolean): Promise<TestResult>;
  save(
      decision: ConnectionInput, language: ConnectionInput,
      agent: ConnectionInput): Promise<SaveResult>;
  turnOff(): Promise<void>;
  setPause(mode: number): Promise<PauseResult>;
  setThresholds(thresholds: Thresholds): Promise<Thresholds>;
  getTint(): Promise<TintState>;
  setTint(color: string): Promise<TintState>;
  clearTint(): Promise<TintState>;
  setCommandBar(on: boolean): Promise<void>;
}

export class UwOrganizationBrowserProxyImpl implements
    UwOrganizationBrowserProxy {
  private handler: OrganizationHandlerRemote =
      OrganizationHandler.getRemote();

  getState() {
    return this.handler.getState().then(response => response.state);
  }

  loadModels(slot: ConnectionSlot, connection: ConnectionInput) {
    return this.handler.loadModels(slot, connection)
        .then(response => response.result);
  }

  testConnection(
      slot: ConnectionSlot, connection: ConnectionInput, namesOnly: boolean) {
    return this.handler.testConnection(slot, connection, namesOnly)
        .then(response => response.result);
  }

  save(
      decision: ConnectionInput, language: ConnectionInput,
      agent: ConnectionInput) {
    return this.handler.save(decision, language, agent)
        .then(response => response.result);
  }

  turnOff() {
    return this.handler.turnOff();
  }

  setPause(mode: number) {
    return this.handler.setPause(mode).then(response => response.result);
  }

  setThresholds(thresholds: Thresholds) {
    return this.handler.setThresholds(thresholds).then(
        response => response.thresholds);
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

  async setCommandBar(on: boolean) {
    await this.handler.setCommandBar(on);
  }

  static getInstance(): UwOrganizationBrowserProxy {
    return instance || (instance = new UwOrganizationBrowserProxyImpl());
  }

  static setInstance(obj: UwOrganizationBrowserProxy) {
    instance = obj;
  }
}

let instance: UwOrganizationBrowserProxy|null = null;
