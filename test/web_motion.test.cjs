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

test("the two migrated sensors retain channels and legacy source mappings", () => {
  const config = JSON.parse(fs.readFileSync(path.join(data, "config.json"), "utf8"));
  assert.equal(config.espMotionEnabled, true);
  assert.deepEqual(config.motionSensors.map(({ id, pollHost, pollChannel, legacyNode, enabled, pollingEnabled }) =>
    ({ id, pollHost, pollChannel, legacyNode, enabled, pollingEnabled })), [
    { id: "bwm-103", pollHost: "192.168.178.103", pollChannel: 1, legacyNode: "103", enabled: true, pollingEnabled: true },
    { id: "bwm-104", pollHost: "192.168.178.104", pollChannel: 0, legacyNode: "104", enabled: true, pollingEnabled: true }
  ]);
  assert.equal(config.timeWindowEnabled, true);
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

test("adding a BWM works independently of the lamp editor and honors the cap", () => {
  const elements = new Map();
  const pending = () => new Promise(() => {});
  const sandbox = vm.createContext({
    document: {
      getElementById(id) {
        if (!elements.has(id)) elements.set(id, {
          children: [],
          listeners: {},
          addEventListener(event, callback) { this.listeners[event] = callback; }
        });
        return elements.get(id);
      }
    },
    lampRequest: pending,
    fetch: pending,
    setInterval() {},
    crypto: { getRandomValues: (array) => array.fill(1234) }
  });
  const html = fs.readFileSync(path.join(data, "settings.html"), "utf8");
  vm.runInContext(html.match(/<script>([\s\S]*?)<\/script>/)[1], sandbox);
  const button = elements.get("addMotionBtn");
  assert.equal(typeof button?.listeners.click, "function", "BWM button must be wired on initial page load");
  vm.runInContext("maxMotionSensors = 32", sandbox);
  const added = [];
  sandbox.addMotionEditor = (sensor) => added.push(sensor);
  button.listeners.click();
  assert.equal(added.length, 1);
  assert.match(added[0].id, /^bwm-/);
  assert.equal(added[0].pollingEnabled, false);
  assert.equal(added[0].enabled, true);
  elements.get("motionEditors").children = Array(32);
  button.listeners.click();
  assert.equal(added.length, 1);
});
