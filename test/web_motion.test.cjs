const assert = require("node:assert/strict");
const { test } = require("node:test");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const data = path.join(__dirname, "..", "data");
const context = vm.createContext();
vm.runInContext(fs.readFileSync(path.join(data, "motion.js"), "utf8"), context);

test("event URLs identify the sensor and explicitly transmit EIN and AUS", () => {
  for (const active of [true, false]) {
    const url = new URL(context.motionEventUrl("http://192.168.178.112", "bwm-103", active));
    assert.equal(url.pathname, "/api/event/motion");
    assert.equal(url.searchParams.get("id"), "bwm-103");
    assert.equal(url.searchParams.get("state"), active ? "1" : "0");
    assert.equal(url.searchParams.has("channel"), false);
  }
  const encoded = new URL(context.motionEventUrl("http://esp.local:8080", "a&state=0", true));
  assert.equal(encoded.searchParams.get("id"), "a&state=0");
  assert.deepEqual(encoded.searchParams.getAll("state"), ["1"]);
  assert.equal(encoded.port, "8080");
});

test("motion status distinguishes disabled, active, unknown, timed out and failed polling", () => {
  assert.equal(context.motionStatusText({ enabled: false, active: true }), "Deaktiviert");
  assert.match(context.motionStatusText({ enabled: true, active: true }), /Bewegung aktiv/);
  assert.match(context.motionStatusText({ enabled: true, active: false }), /Keine Bewegung.*Noch keine Meldung/);
  for (const [source, label] of [["http", "HTTP"], ["gpio", "GPIO"], ["poll", "Polling"]]) {
    const text = context.motionStatusText({ enabled: true, active: true, lastEventAgeMs: 2499, lastSource: source });
    assert.match(text, new RegExp(`Letzte Meldung vor 2 s \\(${label}\\)`));
  }
  assert.match(context.motionStatusText({ enabled: true, timedOut: true }), /Timeout: auf inaktiv gesetzt/);
  assert.match(context.motionStatusText({ enabled: true, pollingEnabled: true, pollHealthy: false }), /Polling fehlgeschlagen/);
  assert.match(context.motionStatusText({ enabled: true, pollingEnabled: true, pollHealthy: true }), /Polling OK/);
  assert.match(context.motionStatusText({ enabled: true, pollingEnabled: true }), /Polling noch nicht erfolgt/);
});

test("the shipped config contains the original BWM settings or their migrated equivalents", () => {
  const config = JSON.parse(fs.readFileSync(path.join(data, "config.json"), "utf8"));
  if (!("motionSensors" in config)) {
    assert.equal(config.shelly103Ip, "192.168.178.103");
    assert.equal(config.shelly104Ip, "192.168.178.104");
    assert.equal(config.shelly103MotionChannel, 1);
    assert.equal(config.shelly104MotionChannel, 0);
    assert.equal(config.shellyPollingEnabled, true);
    return;
  }
  assert.equal(config.espMotionEnabled, true);
  assert.deepEqual(config.motionSensors.map(({ id, pollHost, pollChannel, legacyNode, enabled, pollingEnabled }) =>
    ({ id, pollHost, pollChannel, legacyNode, enabled, pollingEnabled })), [
    { id: "bwm-103", pollHost: "192.168.178.103", pollChannel: 1, legacyNode: "103", enabled: true, pollingEnabled: true },
    { id: "bwm-104", pollHost: "192.168.178.104", pollChannel: 0, legacyNode: "104", enabled: true, pollingEnabled: true }
  ]);
  assert.equal(typeof config.timeWindowEnabled, "boolean");
});

test("compact motion summaries retain activity and faults without event ages or polling success", () => {
  const sensor = { enabled: true, active: true, pollingEnabled: true, pollHealthy: true, lastEventAgeMs: 2000 };
  assert.equal(context.motionSummaryText(sensor), "Bewegung");
  assert.equal(context.motionSummaryText({ ...sensor, active: false }), "Keine Bewegung");
  assert.equal(context.motionSummaryText(sensor, true), "");
  assert.equal(context.motionSummaryText({ enabled: false }, true), "Deaktiviert");
  assert.match(context.motionSummaryText({ ...sensor, pollHealthy: false }, true), /Polling fehlgeschlagen/);
  assert.match(context.motionSummaryText({ ...sensor, timedOut: true }, true), /Timeout/);
});

test("motion helper is loaded by both pages and old hard-coded BWM fields are removed", () => {
  for (const file of ["index.html", "settings.html"]) {
    const html = fs.readFileSync(path.join(data, file), "utf8");
    assert.match(html, /<script src="\/motion.js"><\/script>/);
    for (const match of html.matchAll(/<script>([\s\S]*?)<\/script>/g)) {
      new vm.Script(match[1], { filename: file });
    }
    assert.doesNotMatch(html, /shelly103Ip|shelly104Ip|shelly103MotionChannel|shelly104MotionChannel|shellyPollingEnabled/);
  }
});

test("new BWM drafts are independent and saving retains legacy mappings and fresh siblings", () => {
  const sandbox = vm.createContext({
    document: { addEventListener() {} },
    crypto: { getRandomValues: (array) => array.fill(1234) }
  });
  vm.runInContext(fs.readFileSync(path.join(data, "settings.js"), "utf8"), sandbox);
  const draft = sandbox.newDevice("motion");
  assert.match(draft.id, /^bwm-/);
  assert.equal(draft.pollingEnabled, false);
  assert.equal(draft.enabled, true);
  const original = { ...draft, id: "bwm-103", legacyNode: "103", pollChannel: 1 };
  const sibling = { ...draft, id: "bwm-104", legacyNode: "104", enabled: false };
  const changed = { ...original, name: "Gang hinten", pollingEnabled: true, pollHost: "192.168.178.103" };
  const payload = sandbox.buildDevicePayload({ motionSensors: [original, sibling], maxMotionSensors: 32 },
    { kind: "motion", key: original.id, original, isNew: false }, changed);
  assert.deepEqual(JSON.parse(JSON.stringify(payload)), { motionSensors: [changed, sibling] });
});
