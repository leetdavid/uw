import { useEffect, useRef, useState } from "react";
import { providersHandler, type ModelList, type Provider } from "./providers-api";

const NONE: Provider[] = [];

// The providers for a model menu, each listed once; one whose list failed is
// asked again the next time the menu opens. `loaded` is false until the
// providers are first read.
export function useModels(providerId: string | undefined) {
  const [providers, setProviders] = useState<Provider[] | null>(null);
  const [lists, setLists] = useState<Record<string, ModelList | undefined>>(
    {},
  );
  const listed = useRef(new Set<string>());
  const listModels = (id: string) => {
    if (listed.current.has(id)) {
      return;
    }
    listed.current.add(id);
    providersHandler.listModels(id).then(({ list }) => {
      if (!list.ok) {
        listed.current.delete(id);
      }
      setLists((before) => ({ ...before, [id]: list }));
    });
  };
  const loadProviders = async () => {
    const { state } = await providersHandler.getState();
    const usable = state.providers.filter((provider) => provider.servesLanguage);
    setProviders(usable);
    return usable;
  };
  useEffect(() => {
    loadProviders();
  }, []);
  useEffect(() => {
    if (providerId) {
      listModels(providerId);
    }
  }, [providerId]);
  return {
    providers: providers ?? NONE,
    loaded: providers !== null,
    lists,
    openModels: async () => {
      for (const provider of await loadProviders()) {
        listModels(provider.id);
      }
    },
  };
}
