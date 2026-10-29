const assert = require("node:assert/strict");
const { test } = require("node:test");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const data = path.join(__dirname, "..", "data");
const script = fs.readFileSync(path.join(data, "settings.js"), "utf8");
const plain = (value) => JSON.parse(JSON.stringify(value));

function settings() {
  const sandbox = vm.createContext({
    document: { addEventListener() {} },
    crypto: { getRandomValues: (array) => array.fill(1234) }
  });
  vm.runInContext(script, sandbox);
  return sandbox;
}

test("lamp detail updates retain ID and do not send unrelated configuration", () => {
  const sandbox = settings();
  const original = plain(sandbox.newDevice("lamp"));
  const changed = { ...original, name: "Neue Treppe", onUrl: "http://lamp/on", offUrl: "http://lamp/off" };
  const fresh = { lamps: [original], maxLamps: 32, motionSensors: [], motionOffDelayMs: 30000 };
  const payload = sandbox.buildDevicePayload(fresh, { kind: "lamp", key: original.id, original }, changed);
  assert.deepEqual(plain(payload), { lamps: [changed] });
  assert.equal(payload.lamps[0].id, original.id);
  assert.equal(fresh.lamps[0].name, original.name);
});

test("local BWM editor sends only local fields and rejects concurrent changes", () => {
  const sandbox = settings();
  const original = { espMotionEnabled: true, espMotionPin: 12, espMotionLedPin: 32, espMotionInvert: false };
  const fresh = { ...original, buttonSources: [], automationEnabled: true };
  const changed = { ...original, espMotionEnabled: false };
  assert.deepEqual(plain(sandbox.buildDevicePayload(fresh, { kind: "local", original }, changed)), changed);
  assert.throws(() => sandbox.buildDevicePayload({ ...fresh, espMotionPin: 14 }, { kind: "local", original }, changed), /inzwischen geaendert/);
  assert.throws(() => sandbox.buildDevicePayload(fresh, { kind: "local", original }, null, true), /nicht entfernt/);
});

test("general settings preserve unrelated devices and automation state", () => {
  const sandbox = settings();
  const fields = vm.runInContext("deviceKinds.general.fields", sandbox);
  const original = Object.fromEntries(fields.map((key) => [key, key === "ntpServer" ? "pool.ntp.org" : 0]));
  const payload = sandbox.buildDevicePayload({ ...original, lamps: [{ id: "keep" }], automationEnabled: false },
    { kind: "general", original }, { ...original, motionOffDelayMs: 30000 });
  assert.equal(payload.motionOffDelayMs, 30000);
  assert.deepEqual(Object.keys(payload).sort(), Array.from(fields).sort());
  assert.equal("automationEnabled" in payload, false);
});

test("conflict checks compare field values, not JSON property order", () => {
  const sandbox = settings();
  assert.equal(sandbox.sameItem({ name: "A", enabled: true }, { enabled: true, name: "A" }), true);
  assert.equal(sandbox.sameItem({ name: "A", enabled: true }, { name: "A", enabled: false }), false);
});

test("overview and detail markup keep forms out of the three device lists", () => {
  const html = fs.readFileSync(path.join(data, "settings.html"), "utf8");
  const overview = html.slice(html.indexOf('<section id="overview"'), html.indexOf('<section class="card" id="detail"'));
  assert.doesNotMatch(overview, /<input|<select|Speichern/);
  for (const kind of ["button", "motion", "lamp"]) {
    assert.match(overview, new RegExp(`id="${kind}List"`));
  }
  assert.match(html, /id="detail" hidden/);
  assert.match(html, /id="cancelBtn"/);
  assert.match(html, /src="\/settings.js"/);
});

test("hidden general controls preserve their saved values instead of submitting invalid drafts", () => {
  const sandbox = settings();
  const fields = vm.runInContext("deviceKinds.general.fields", sandbox);
  const original = Object.fromEntries(fields.map((key) => [key, 0]));
  Object.assign(original, { motionStartMin: 360, motionEndMin: 1200, ntpServer: "old.ntp" });
  sandbox.original = original;
  sandbox.document.getElementById = () => ({
    querySelector(selector) {
      const name = selector.match(/name="([^"]+)"/)[1];
      return {
        disabled: name === "motionStartMin" || name === "motionEndMin",
        type: "text",
        value: name === "ntpServer" ? "  pool.ntp.org  " : name.startsWith("motion") ? "" : "0"
      };
    }
  });

  vm.runInContext('selection = { kind: "general", original }', sandbox);
  const value = sandbox.readDetail();
  assert.equal(value.motionStartMin, 360);
  assert.equal(value.motionEndMin, 1200);
  assert.equal(value.ntpServer, "pool.ntp.org");
});

test("both pages share the dark theme and dashboard diagnostics are collapsed", () => {
  for (const file of ["settings.html", "index.html"]) {
    const html = fs.readFileSync(path.join(data, file), "utf8");
    assert.match(html, /<link rel="stylesheet" href="\/theme.css">/);
    assert.match(html, /name="theme-color" content="#181a1b"/);
  }
  const html = fs.readFileSync(path.join(data, "index.html"), "utf8");
  assert.match(html, /<details class="card" id="systemDetails">/);
  assert.match(html, /<details class="card" id="logDetails">/);
  assert.match(fs.readFileSync(path.join(data, "theme.css"), "utf8"), /color-scheme: dark/);
});

test("failed conflict checks retain the draft but refresh the snapshot for Cancel and reopen", async () => {
  const sandbox = settings();
  const original = { name: "Original", ip: "192.168.178.102", enabled: true, buttonGuard: true };
  const fresh = { buttonSources: [{ ...original, name: "Extern geaendert" }] };
  const elements = new Map();
  sandbox.document.getElementById = (id) => {
    if (!elements.has(id)) elements.set(id, { reportValidity: () => true });
    return elements.get(id);
  };
  sandbox.original = original;
  sandbox.lampRequest = async () => fresh;
  sandbox.readDetail = () => ({ ...original, name: "Entwurf" });
  sandbox.updateDetailOptions = () => {};
  vm.runInContext('selection = { kind: "button", key: original.ip, original }; config = { buttonSources: [original] }', sandbox);
  await sandbox.saveDetail();
  assert.match(elements.get("saveResult").textContent, /inzwischen geaendert/);
  assert.equal(vm.runInContext("config.buttonSources[0].name", sandbox), "Extern geaendert");
  assert.equal(vm.runInContext("selection.original.name", sandbox), "Original");
  assert.equal(vm.runInContext("saving", sandbox), false);
  assert.equal(elements.get("saveBtn").disabled, false);
});

test("Create routes are distinct from valid device IDs named new and reject unknown views", () => {
  const sandbox = settings();
  const elements = new Map();
  sandbox.document.getElementById = (id) => {
    if (!elements.has(id)) elements.set(id, {
      querySelectorAll: () => [],
      replaceChildren() {},
      focus() {},
      content: { cloneNode: () => ({}) }
    });
    return elements.get(id);
  };
  sandbox.location = { hash: "#lamp/new" };
  sandbox.updateDetailOptions = () => {};
  sandbox.renderStatus = () => {};
  vm.runInContext('config = { lamps: [{ id: "new", name: "Existing lamp" }], maxLamps: 32 }', sandbox);
  sandbox.renderRoute();
  assert.equal(vm.runInContext("selection.isNew", sandbox), false);
  assert.equal(vm.runInContext("selection.original.id", sandbox), "new");
  sandbox.location.hash = "#create/lamp";
  sandbox.renderRoute();
  assert.equal(vm.runInContext("selection.isNew", sandbox), true);
  assert.match(vm.runInContext("selection.original.id", sandbox), /^lamp-/);
  sandbox.location.hash = "#constructor";
  sandbox.renderRoute();
  assert.equal(sandbox.location.hash, "overview");
  assert.match(elements.get("saveResult").textContent, /existiert nicht/);
});
