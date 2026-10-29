import assert from "node:assert/strict";
import { test } from "node:test";
import { isGoogle, readNetLog, unexpected } from "../scripts/google-check.ts";

const constants = { logEventTypes: { URL_REQUEST_START_JOB: 2, HOST_RESOLVER_MANAGER_REQUEST: 7 } };

function netLog(urls: string[], hosts: string[] = []): string {
  const events = [
    ...urls.map((url) => ({ type: 2, params: { url, method: "GET" } })),
    ...hosts.map((host) => ({ type: 7, params: { host } })),
  ];
  return JSON.stringify({ constants, events });
}

test("Google's hosts are told apart from look-alikes", () => {
  for (const host of ["www.google.com", "google.co.kr", "update.googleapis.com", "edgedl.me.gvt1.com", "www.gstatic.com",
    "play.google.com", "csp.withgoogle.com", "clients2.googleusercontent.com", "WWW.GOOGLE.COM"]) {
    assert.equal(isGoogle(host), true, host);
  }
  for (const host of ["example.com", "notgoogle.com", "google.example.com", "www.ecb.europa.eu", "127.0.0.1"]) {
    assert.equal(isGoogle(host), false, host);
  }
});

test("a net log gives its URL requests and host lookups, also when cut off", () => {
  const whole = netLog(["https://example.com/"], ["https://example.com", "mtalk.google.com:5228"]);
  assert.deepEqual(readNetLog(whole), { urls: ["https://example.com/"], hosts: ["example.com", "mtalk.google.com"] });
  const cut = whole.replace(/]}$/, ",\n");
  assert.deepEqual(readNetLog(cut), readNetLog(whole));
});

test("kept requests pass and every other Google request is named once, without its query or payload", () => {
  const traffic = readNetLog(netLog([
    "https://update.googleapis.com/service/update2/json?cup2key=1",
    "http://edgedl.me.gvt1.com/edgedl/diffgen-puffin/abc/def",
    "https://android.clients.google.com/checkin",
    "https://android.clients.google.com/c2dm/register3",
    "https://accounts.google.com/ListAccounts?gpsia=1",
    "https://www.google.com/async/folae?async=_fmt:pb",
    "https://www.google.com/async/folae?async=_fmt:pb",
    "http://clients2.google.com/time/1/current?cup2key=10",
    "https://content-autofill.googleapis.com/v1/pages/ChRDaHJvbWUvMTUzLjAuODAxMC41MxJ5CZsv51547v2XEg0N",
    "https://content-autofill.googleapis.com/v1/pages/ChRDaHJvbWUvMTUzLjAuODAxMC41MxJ5CZsv51547v2XEg0M",
    "http://127.0.0.1:4000/",
    "data:text/html,x",
  ], ["mtalk.google.com:5228", "https://update.googleapis.com", "https://ogads-pa.clients6.google.com"]));
  assert.deepEqual(unexpected(traffic), [
    "http://clients2.google.com/time/1/current",
    "https://content-autofill.googleapis.com/v1/pages/…",
    "https://www.google.com/async/folae",
    "ogads-pa.clients6.google.com",
  ]);
});
