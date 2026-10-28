"use client";

import { useSearchContext } from "fumadocs-ui/contexts/search";
import { Search } from "lucide-react";

export function SearchButton() {
  const { setOpenSearch } = useSearchContext();
  return (
    <button
      type="button"
      className="help-home-search"
      onClick={() => setOpenSearch(true)}
    >
      <Search />
      <span>Search Help</span>
      <kbd>⌘ K</kbd>
    </button>
  );
}
