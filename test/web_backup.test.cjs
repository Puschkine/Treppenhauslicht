const assert = require("node:assert/strict");
const { test } = require("node:test");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const source = fs.readFileSync(path.join(__dirname, "..", "data", "settings.js"), "utf8");

function setup({ available = false, confirm = true, failure, reloadFailure = false } = {}) {
  const elements = new Map();
  const calls = [];
  const prompts = [];
  const document = {
    addEventListener() {},
    getElementById(id) {
      if (!elements.has(id)) elements.set(id, { querySelectorAll: () => [] });
      return elements.get(id);
    }
  };
  const sandbox = vm.createContext({
    document,
    window: { confirm: (text) => { prompts.push(text); return confirm; } },
    lampRequest: async (url, body) => {
      calls.push({ url, body });
      if (body && failure) throw new Error(failure);
      if (url === "/api/config" && reloadFailure) throw new Error("offline");
      if (url === "/api/config") return { lamps: [{ id: "restored" }] };
      return { available, bytes: available ? 2000 : 0, createdAt: 0 };
    }
  });
  vm.runInContext(source, sandbox);
  vm.runInContext(`config = { lamps: [{ id: "current" }] }; backupAvailable = ${available}`, sandbox);
  sandbox.renderOverview = () => {};
  return { sandbox, calls, prompts, elements };
}

test("backup status disables restore when missing and reports status failures explicitly", async () => {
  const { sandbox, elements } = setup();
  await sandbox.loadBackupStatus();
  assert.equal(elements.get("restoreBtn").disabled, true);
  assert.match(elements.get("backupStatus").textContent, /Keine Sicherung/);
  sandbox.lampRequest = async () => { throw new Error("NVS defekt"); };
  await sandbox.loadBackupStatus();
  assert.match(elements.get("backupStatus").textContent, /NVS defekt/);
  assert.equal(elements.get("restoreBtn").disabled, true);
});

test("backup posts explicitly and always confirms replacement even if status was unavailable", async () => {
  const { sandbox, calls, prompts, elements } = setup();
  await sandbox.manageBackup(false);
  assert.equal(calls[0].url, "/api/config/backup");
  assert.deepEqual(JSON.parse(JSON.stringify(calls[0].body)), {});
  assert.equal(prompts.length, 1);
  assert.match(elements.get("saveResult").textContent, /gesichert/);
  const cancelled = setup({ available: true, confirm: false });
  await cancelled.sandbox.manageBackup(false);
  assert.match(cancelled.prompts[0], /ersetzt/);
  assert.equal(cancelled.calls.length, 0);
});

test("restore requires confirmation, reloads configuration and warns about lamps OFF", async () => {
  const { sandbox, calls, prompts, elements } = setup({ available: true });
  await sandbox.manageBackup(true);
  assert.match(prompts[0], /Lampen werden AUS/);
  assert.equal(calls[0].url, "/api/config/restore");
  assert.equal(calls[1].url, "/api/config");
  assert.equal(vm.runInContext("config.lamps[0].id", sandbox), "restored");
  assert.match(elements.get("saveResult").textContent, /Wiederhergestellt/);
});

test("failed restore keeps current configuration and exposes the error", async () => {
  const { sandbox, elements } = setup({ available: true, failure: "Lampenbefehle laufen noch" });
  await sandbox.manageBackup(true);
  assert.equal(vm.runInContext("config.lamps[0].id", sandbox), "current");
  assert.match(elements.get("saveResult").textContent, /Lampenbefehle/);
  assert.equal(vm.runInContext("backupBusy", sandbox), false);
});

test("successful restore with failed reload never presents the stale configuration as current", async () => {
  const { sandbox, elements } = setup({ available: true, reloadFailure: true });
  await sandbox.manageBackup(true);
  assert.equal(vm.runInContext("config", sandbox), null);
  assert.match(elements.get("saveResult").textContent, /Wiederhergestellt.*Seite neu laden/);
});

test("backup operations do not run while editing, saving, busy, or without a restore snapshot", async () => {
  for (const state of ["selection = {}", "saving = true", "backupBusy = true", "backupAvailable = false"]) {
    const { sandbox, calls } = setup({ available: true });
    vm.runInContext(state, sandbox);
    await sandbox.manageBackup(true);
    assert.equal(calls.length, 0);
  }
});
