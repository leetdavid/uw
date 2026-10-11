// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** @fileoverview Handles communication for the uw organization page. */

import type {ConnectionInput, ModelsResult, PauseResult, SaveResult, State} from './uw_organization.mojom-webui.js';
import {OrganizationHandler, OrganizationHandlerRemote} from './uw_organization.mojom-webui.js';

export interface UwOrganizationBrowserProxy {
  getState(): Promise<State>;
  loadModels(decision: boolean, connection: ConnectionInput):
      Promise<ModelsResult>;
  save(decision: ConnectionInput, language: ConnectionInput):
      Promise<SaveResult>;
  setPause(mode: number): Promise<PauseResult>;
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

  save(decision: ConnectionInput, language: ConnectionInput) {
    return this.handler.save(decision, language)
        .then(response => response.result);
  }

  setPause(mode: number) {
    return this.handler.setPause(mode).then(response => response.result);
  }

  static getInstance(): UwOrganizationBrowserProxy {
    return instance || (instance = new UwOrganizationBrowserProxyImpl());
  }

  static setInstance(obj: UwOrganizationBrowserProxy) {
    instance = obj;
  }
}

let instance: UwOrganizationBrowserProxy|null = null;
