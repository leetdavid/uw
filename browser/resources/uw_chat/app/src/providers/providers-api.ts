// The providers page's line to the browser, uw_providers.mojom. The bundle
// step points "uw-providers-mojom" at the bindings the build generates.
import {
  ModelSlot,
  ProviderFormat,
  ProviderPreset,
  ProvidersHandler,
  TestOutcome,
} from "uw-providers-mojom";
import type {
  ModelChoice,
  ModelList,
  NewProvider,
  Provider,
  ProvidersState,
  ModelTestResult,
} from "uw-providers-mojom";

export { ModelSlot, ProviderFormat, ProviderPreset, TestOutcome };
export type { ModelChoice, ModelList, NewProvider, Provider, ProvidersState, ModelTestResult };

export const providersHandler = ProvidersHandler.getRemote();
