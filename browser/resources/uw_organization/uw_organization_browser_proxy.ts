// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** @fileoverview Handles communication for the uw organization page. */

import type {CommandState, MiniPlayerSwitch, MouseAction, PauseResult, State, Thresholds} from './uw_organization.mojom-webui.js';
import {OrganizationHandler, OrganizationHandlerRemote} from './uw_organization.mojom-webui.js';

export interface UwOrganizationBrowserProxy {
  getCommand(): Promise<CommandState>;
  installCommand(): Promise<CommandState>;
  uninstallCommand(): Promise<CommandState>;
  getState(): Promise<State>;
  setEnabled(on: boolean): Promise<State>;
  setPause(mode: number): Promise<PauseResult>;
  setThresholds(thresholds: Thresholds): Promise<Thresholds>;
  openThemePicker(): void;
  setGlassBehind(on: boolean): Promise<void>;
  setCommandBar(on: boolean): Promise<void>;
  setCurrency(code: string): Promise<void>;
  setMouseAction(action: MouseAction, on: boolean): Promise<void>;
  setMiniPlayer(which: MiniPlayerSwitch, on: boolean): Promise<void>;
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

  async setGlassBehind(on: boolean) {
    await this.handler.setGlassBehind(on);
  }

  async setCommandBar(on: boolean) {
    await this.handler.setCommandBar(on);
  }

  async setCurrency(code: string) {
    await this.handler.setCurrency(code);
  }

  async setMouseAction(action: MouseAction, on: boolean) {
    await this.handler.setMouseAction(action, on);
  }

  async setMiniPlayer(which: MiniPlayerSwitch, on: boolean) {
    await this.handler.setMiniPlayer(which, on);
  }

  getCommand() {
    return this.handler.getCommand().then(response => response.state);
  }

  installCommand() {
    return this.handler.installCommand().then(response => response.state);
  }

  uninstallCommand() {
    return this.handler.uninstallCommand().then(response => response.state);
  }

  static getInstance(): UwOrganizationBrowserProxy {
    return instance || (instance = new UwOrganizationBrowserProxyImpl());
  }

  static setInstance(obj: UwOrganizationBrowserProxy) {
    instance = obj;
  }
}

let instance: UwOrganizationBrowserProxy|null = null;
