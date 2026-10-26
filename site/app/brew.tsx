"use client";

import { useState } from "react";

const COMMAND = "brew install --cask leetdavid/uw/uw";

function CopyIcon() {
  return (
    <svg
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="2"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      <rect width="14" height="14" x="8" y="8" rx="2" />
      <path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2" />
    </svg>
  );
}

function CheckIcon() {
  return (
    <svg
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="2.4"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      <path d="M20 6 9 17l-5-5" />
    </svg>
  );
}

export function Brew() {
  const [copied, setCopied] = useState(false);

  async function copy() {
    await navigator.clipboard.writeText(COMMAND);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  }

  return (
    <button
      type="button"
      className="brew"
      onClick={copy}
      aria-label={copied ? "Copied" : `Copy ${COMMAND}`}
    >
      <code>
        <span>$</span> {COMMAND}
      </code>
      <i>{copied ? <CheckIcon /> : <CopyIcon />}</i>
    </button>
  );
}
