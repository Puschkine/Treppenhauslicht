const assert = require("node:assert/strict");
const { test } = require("node:test");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const script = fs.readFileSync(path.join(__dirname, "..", "data", "settings.js"), "utf8");

function settings() {
  const sandbox = vm.createContext({ document: { addEventListener() {} } });
  vm.runInContext(script, sandbox);
  return sandbox;
}

const original = { name: "Treppe", ip: "192.168.178.102", enabled: true, buttonGuard: true };
const other = { name: "Eingang", ip: "192.168.178.120", enabled: true, buttonGuard: false };
const plain = (value) => JSON.parse(JSON.stringify(value));

test("saving a button edits only that source and preserves fresh sibling settings", () => {
  const sandbox = settings();
  const fresh = { buttonSources: [original, other], maxButtonSources: 32, motionOffDelayMs: 30000 };
  const edit = { kind: "button", key: original.ip, original, isNew: false };
  const value = { ...original, name: "Treppen-Taster", ip: "192.168.178.121", enabled: false };
  const payload = sandbox.buildDevicePayload(fresh, edit, value);
  assert.deepEqual(plain(payload), { buttonSources: [value, other] });
  assert.equal(fresh.buttonSources[0], original);
});

test("new button sources default to enabled without guard and respect duplicates and limits", () => {
  const sandbox = settings();
  const added = sandbox.newDevice("button");
  assert.deepEqual(plain(added), { name: "Neuer Tasteingang", ip: "", enabled: true, buttonGuard: false });
  const edit = { kind: "button", isNew: true };
  assert.throws(() => sandbox.buildDevicePayload({ buttonSources: [original], maxButtonSources: 32 }, edit, original), /bereits vorhanden/);
  assert.throws(() => sandbox.buildDevicePayload({ buttonSources: Array(32).fill(original), maxButtonSources: 32 }, edit, other), /maximale Anzahl/);
  assert.deepEqual(plain(sandbox.buildDevicePayload({ buttonSources: [original], maxButtonSources: 32 }, edit, other)),
    { buttonSources: [original, other] });
});

test("removing the last button sends an explicitly empty list and detects concurrent edits", () => {
  const sandbox = settings();
  const edit = { kind: "button", key: original.ip, original, isNew: false };
  assert.deepEqual(plain(sandbox.buildDevicePayload({ buttonSources: [original] }, edit, null, true)), { buttonSources: [] });
  assert.throws(() => sandbox.buildDevicePayload({ buttonSources: [{ ...original, enabled: false }] }, edit, original), /inzwischen geaendert/);
  assert.throws(() => sandbox.buildDevicePayload({ buttonSources: [] }, edit, null, true), /inzwischen geaendert/);
});
