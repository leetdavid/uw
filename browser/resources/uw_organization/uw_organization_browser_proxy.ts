// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** @fileoverview Handles communication for the uw organization page. */

import type {ConnectionState, ModelsResult, PauseResult, SaveResult} from './uw_organization.mojom-webui.js';
import {OrganizationHandler, OrganizationHandlerRemote} from './uw_organization.mojom-webui.js';

export interface UwOrganizationBrowserProxy {
  getState(): Promise<ConnectionState>;
  loadModels(endpoint: string, key: string): Promise<ModelsResult>;
  save(mode: number, endpoint: string, model: string, key: string):
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

  loadModels(endpoint: string, key: string) {
    return this.handler.loadModels(endpoint, key)
        .then(response => response.result);
  }

  save(mode: number, endpoint: string, model: string, key: string) {
    return this.handler.save(mode, endpoint, model, key)
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
