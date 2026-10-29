// The providers page's line to the browser, uw_providers.mojom. The bundle
// step points "uw-providers-mojom" at the bindings the build generates.
import {
  CatalogSource,
  ConnectorState,
  Decides,
  ModelSlot,
  ProviderFormat,
  ProviderPreset,
  ProvidersHandler,
  SkillSource,
  TestOutcome,
  UpgradeNotice,
} from "uw-providers-mojom";
import type {
  Model,
  ModelChoice,
  ModelList,
  NewProvider,
  Provider,
  ProvidersState,
  ModelTestResult,
  SkillInfo,
  ConnectorInfo,
  NewConnector,
  CatalogAnswer,
  CatalogEntry,
  MemoryInfo,
} from "uw-providers-mojom";

export { CatalogSource, ConnectorState, Decides, ModelSlot, ProviderFormat, ProviderPreset, SkillSource, TestOutcome, UpgradeNotice };
export type { Model, ModelChoice, ModelList, NewProvider, Provider, ProvidersState, ModelTestResult, SkillInfo, ConnectorInfo, NewConnector, CatalogAnswer, CatalogEntry, MemoryInfo };

export const providersHandler = ProvidersHandler.getRemote();
