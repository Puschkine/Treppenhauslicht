const assert = require("node:assert/strict");
const { test } = require("node:test");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const data = path.join(__dirname, "..", "data");
const source = fs.readFileSync(path.join(data, "lamps.js"), "utf8");

function helpers(fetch) {
  const context = vm.createContext({ fetch });
  vm.runInContext(source, context);
  return context;
}

test("lamp status distinguishes pending, failed, unknown and sent commands", () => {
  const { lampStatusText } = helpers();
  assert.equal(lampStatusText({ enabled: false }), "Deaktiviert");
  assert.match(lampStatusText({ enabled: true, pending: true, requestedOn: true, error: "old error" }), /EIN angefordert/);
  assert.match(lampStatusText({ enabled: true, pending: true, requestedOn: false }), /AUS angefordert/);
  assert.match(lampStatusText({ enabled: true, lastCommandOn: true, error: "HTTP 503" }), /Fehler bei EIN: HTTP 503/);
  assert.match(lampStatusText({ enabled: true }), /Noch kein Befehl/);
  for (const on of [true, false]) {
    const text = lampStatusText({ enabled: true, lastCommandOn: on, httpCode: 200 });
    assert.match(text, on ? /EIN gesendet/ : /AUS gesendet/);
    assert.match(text, /Zustand nicht bestaetigt/);
  }
});

test("manual commands send stable IDs and accept HTTP 202 without claiming execution", async () => {
  const calls = [];
  const { lampRequest } = helpers(async (url, options) => {
    calls.push({ url, options });
    return { ok: true, status: 202, json: async () => ({ ok: true, queued: true }) };
  });
  const result = await lampRequest("/api/manual/relay", { id: "additional-lamp", on: false });
  assert.equal(result.queued, true);
  assert.equal(calls[0].url, "/api/manual/relay");
  assert.equal(calls[0].options.method, "POST");
  assert.deepEqual(JSON.parse(calls[0].options.body), { id: "additional-lamp", on: false });
});

test("API validation, persistence, busy and network errors propagate to the caller", async () => {
  for (const status of [400, 409, 500]) {
    const { lampRequest } = helpers(async () => ({
      ok: false, status, json: async () => ({ error: `failure-${status}` })
    }));
    await assert.rejects(lampRequest("/api/config", { lamps: [] }), new RegExp(`failure-${status}`));
  }
  const { lampRequest } = helpers(async () => { throw new Error("offline"); });
  await assert.rejects(lampRequest("/api/status"), /offline/);
});

test("both HTML scripts and the shared helper have valid JavaScript syntax", () => {
  new vm.Script(source);
  new vm.Script(fs.readFileSync(path.join(data, "settings.js"), "utf8"));
  for (const file of ["index.html", "settings.html"]) {
    const html = fs.readFileSync(path.join(data, file), "utf8");
    assert.match(html, /<script src="\/lamps.js"><\/script>/);
    for (const match of html.matchAll(/<script>([\s\S]*?)<\/script>/g)) {
      new vm.Script(match[1], { filename: file });
    }
    assert.doesNotMatch(html, /shelly103OutputChannel|shelly104OutputChannel|data-node=/);
  }
});

test("shipped configuration preserves the five lamps and original automation selection", () => {
  const config = JSON.parse(fs.readFileSync(path.join(data, "config.json"), "utf8"));
  assert.equal(config.lamps.length, 5);
  assert.equal(new Set(config.lamps.map((lamp) => lamp.id)).size, 5);
  assert.deepEqual(config.lamps.filter((lamp) => lamp.automation).map((lamp) => lamp.id),
    ["shelly-102-0", "shelly-103-1", "shelly-104-0"]);
  assert.deepEqual(config.lamps.filter((lamp) => lamp.buttonGuard).map((lamp) => lamp.id), ["shelly-102-0"]);
  for (const lamp of config.lamps) {
    const on = new URL(lamp.onUrl);
    const off = new URL(lamp.offUrl);
    assert.equal(on.protocol, "http:");
    assert.equal(off.host, on.host);
    assert.equal(on.searchParams.get("on"), "true");
    assert.equal(off.searchParams.get("on"), "false");
    assert.equal(off.searchParams.get("id"), on.searchParams.get("id"));
  }
});
