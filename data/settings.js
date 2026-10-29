const q = (id) => document.getElementById(id);
const deviceKinds = {
  button: { list: "buttonSources", limit: "maxButtonSources", title: "Tasteingang", fields: ["name", "ip", "enabled", "buttonGuard"] },
  motion: { list: "motionSensors", limit: "maxMotionSensors", title: "Bewegungsmelder", fields: ["name", "enabled", "pollingEnabled", "pollHost", "pollChannel"] },
  lamp: { list: "lamps", limit: "maxLamps", title: "Lampe", fields: ["name", "onUrl", "offUrl", "enabled", "automation", "buttonGuard"] },
  local: { title: "Interner BWM", fields: ["espMotionEnabled", "espMotionPin", "espMotionLedPin", "espMotionInvert"] },
  general: { title: "Allgemeine Einstellungen", fields: [
    "motionOffDelayMs", "buttonShortOverrideMs", "motionEventTimeoutMs", "shellyPollIntervalMs",
    "timeWindowEnabled", "windowUseSun", "motionStartMin", "motionEndMin", "sunsetOffsetMin",
    "sunriseOffsetMin", "latitude", "longitude", "sunOnlineLookup", "ntpServer", "gmtOffsetSec", "daylightOffsetSec"
  ] }
};
const booleanFields = new Set(["enabled", "buttonGuard", "pollingEnabled", "automation",
  "espMotionEnabled", "espMotionInvert", "timeWindowEnabled", "windowUseSun", "sunOnlineLookup"]);
const stringFields = new Set(["name", "ip", "onUrl", "offUrl", "pollHost", "ntpServer"]);
let config;
let selection = null;
let saving = false;
let latestStatus;
let statusLoading = false;

function itemKey(kind, item) {
  return kind === "button" ? item.ip : item.id;
}

function selectFields(source, fields) {
  return Object.fromEntries(fields.map((field) => [field, source[field]]));
}

function sameItem(a, b) {
  const keys = [...new Set([...Object.keys(a), ...Object.keys(b)])].sort();
  return keys.every((key) => a[key] === b[key]);
}

function buildDevicePayload(fresh, edit, value, remove = false) {
  const kind = deviceKinds[edit.kind];
  if (!kind.list) {
    if (remove) throw new Error("Dieser Eintrag kann nicht entfernt werden.");
    if (!sameItem(selectFields(fresh, kind.fields), edit.original)) {
      throw new Error("Die Einstellungen wurden inzwischen geaendert. Bitte abbrechen und neu oeffnen.");
    }
    return selectFields(value, kind.fields);
  }
  const list = fresh[kind.list].slice();
  if (edit.isNew) {
    if (remove) throw new Error("Der Eintrag ist noch nicht gespeichert.");
    if (list.length >= fresh[kind.limit]) throw new Error("Die maximale Anzahl ist erreicht.");
  } else {
    const index = list.findIndex((item) => itemKey(edit.kind, item) === edit.key);
    if (index < 0 || !sameItem(list[index], edit.original)) {
      throw new Error("Dieser Eintrag wurde inzwischen geaendert oder entfernt. Bitte abbrechen und neu oeffnen.");
    }
    if (remove) list.splice(index, 1);
    else list[index] = value;
  }
  if (!remove) {
    if (list.some((item) => itemKey(edit.kind, item) === itemKey(edit.kind, value) &&
      (edit.isNew || item !== value))) throw new Error("Diese IP-Adresse oder Kennung ist bereits vorhanden.");
    if (edit.isNew) list.push(value);
  }
  return { [kind.list]: list };
}

function notify(text, error = false) {
  q("saveResult").textContent = text;
  q("saveResult").className = `notice ${error ? "err" : "ok"}`;
}

function field(name) {
  return q("detailFields").querySelector(`[name="${name}"]`);
}

function readDetail() {
  const value = { ...selection.original };
  for (const name of deviceKinds[selection.kind].fields) {
    const input = field(name);
    if (selection.kind === "general" && input.disabled) continue;
    if (input.type === "checkbox") value[name] = input.checked;
    else if (booleanFields.has(name)) value[name] = input.value === "true";
    else if (name === "motionStartMin" || name === "motionEndMin") {
      const [hours, minutes] = input.value.split(":").map(Number);
      value[name] = hours * 60 + minutes;
    } else value[name] = stringFields.has(name) ? input.value.trim() : Number(input.value);
  }
  return value;
}

function makeItem(kind, key, name, summary, statusKind, statusId) {
  const button = document.createElement("button");
  button.type = "button";
  button.className = "device-item";
  button.setAttribute("aria-label", `${name}: Einstellungen`);
  const info = document.createElement("span");
  info.className = "device-info";
  const title = document.createElement("span");
  title.className = "device-name";
  title.textContent = name;
  const subtitle = document.createElement("span");
  subtitle.className = "device-summary";
  subtitle.textContent = summary;
  info.append(title, subtitle);
  if (statusKind) {
    const status = document.createElement("span");
    status.className = "device-status";
    status.dataset.kind = statusKind;
    status.dataset.id = statusId;
    info.append(status);
  }
  const arrow = document.createElement("span");
  arrow.className = "device-arrow";
  arrow.textContent = ">";
  arrow.setAttribute("aria-hidden", "true");
  button.append(info, arrow);
  button.addEventListener("click", () => { location.hash = `${kind}/${encodeURIComponent(key)}`; });
  return button;
}

function renderOverview() {
  for (const name of ["button", "motion", "lamp"]) {
    const kind = deviceKinds[name];
    const items = config[kind.list];
    q(`${name}List`).replaceChildren();
    q(`${name}Limit`).textContent = `${items.length} / ${config[kind.limit]}${name === "motion" ? " extern" : ""}`;
    q(`add${name[0].toUpperCase() + name.slice(1)}Btn`).disabled = items.length >= config[kind.limit];
    for (const item of items) {
      const enabled = item.enabled ? "Aktiviert" : "Deaktiviert";
      const summary = name === "button" ? `${enabled} | ${item.ip}` :
        name === "motion" ? `${enabled} | ${item.pollingEnabled ? `Shelly-Polling: ${item.pollHost}` : "HTTP-Events"}` :
        `${enabled} | ${item.automation ? "Automatik" : "Nur manuell"}`;
      q(`${name}List`).append(makeItem(name, itemKey(name, item), item.name, summary,
        name === "button" ? null : name, item.id));
    }
    if (!items.length) {
      const empty = document.createElement("p");
      empty.className = "hint";
      empty.textContent = "Noch keine Eintraege. Mit Create einen neuen Eintrag anlegen.";
      q(`${name}List`).append(empty);
    }
  }
  q("motionList").prepend(makeItem("local", "", "Interner BWM",
    `${config.espMotionEnabled ? "Aktiviert" : "Deaktiviert"} | ESP GPIO ${config.espMotionPin}`, "motion", "esp"));
  q("generalBtn").disabled = false;
  renderStatus();
}

function newDevice(kind) {
  if (kind === "button") return { name: "Neuer Tasteingang", ip: "", enabled: true, buttonGuard: false };
  const random = crypto.getRandomValues(new Uint32Array(2));
  const id = `${kind === "motion" ? "bwm" : "lamp"}-${random[0].toString(16)}-${random[1].toString(16)}`;
  if (kind === "motion") return { id, name: "Neuer BWM", enabled: true, pollingEnabled: false, pollHost: "", pollChannel: 0, legacyNode: "" };
  return { id, name: "Neue Lampe", onUrl: "", offUrl: "", enabled: true, automation: true, buttonGuard: false };
}

function renderRoute() {
  if (!config) return;
  if (saving) {
    history.replaceState(null, "", `#${selection.route}`);
    return;
  }
  const route = location.hash.slice(1);
  if (!route || route === "overview") {
    selection = null;
    q("detail").hidden = true;
    q("overview").hidden = false;
    renderOverview();
    return;
  }
  const [routeKind, encodedKey = ""] = route.split("/");
  const isNew = routeKind === "create";
  const name = isNew ? encodedKey : routeKind;
  const kind = Object.prototype.hasOwnProperty.call(deviceKinds, name) ? deviceKinds[name] : null;
  let key;
  try { key = isNew ? "" : decodeURIComponent(encodedKey); } catch { key = null; }
  if (!kind || key === null || (isNew && !kind.list)) {
    notify("Diese Einstellungsansicht existiert nicht.", true);
    location.hash = "overview";
    return;
  }
  const original = kind.list ? (isNew ? newDevice(name) :
    config[kind.list].find((item) => itemKey(name, item) === key)) : selectFields(config, kind.fields);
  if (!original || (isNew && config[kind.list].length >= config[kind.limit])) {
    notify("Eintrag nicht vorhanden oder maximale Anzahl erreicht.", true);
    location.hash = "overview";
    return;
  }
  selection = { kind: name, key, isNew, original: { ...original }, route };
  q("overview").hidden = true;
  q("detail").hidden = false;
  q("detailTitle").textContent = isNew ? `${kind.title} erstellen` : original.name || kind.title;
  q("detailHint").textContent = "Speichern uebernimmt nur diesen Eintrag. Abbrechen oder Zurueck verwirft die Aenderungen. Speichern initialisiert die Steuerung neu und fordert Lampen AUS an.";
  q("detailFields").replaceChildren(q(`${name}Template`).content.cloneNode(true));
  for (const input of q("detailFields").querySelectorAll("[name]")) {
    const value = original[input.name];
    if (input.type === "checkbox") input.checked = value;
    else if (input.type === "time") {
      input.value = `${String(Math.floor(value / 60)).padStart(2, "0")}:${String(value % 60).padStart(2, "0")}`;
    } else input.value = String(value);
  }
  q("deleteBtn").hidden = !kind.list || isNew;
  q("detailStatus").textContent = "";
  if (name === "button") {
    for (const type of ["short", "long"]) q("detailFields").querySelector(`[data-url="${type}"]`).value = `${location.origin}/api/event/button?type=${type}`;
  }
  if (name === "motion") {
    field("id").value = original.id;
    q("legacyHint").textContent = original.legacyNode ? `Bisherige URLs mit node=${original.legacyNode} funktionieren weiterhin. Der Eingangskanal bleibt deren Kanalfilter.` : "";
    for (const on of [true, false]) {
      const type = on ? "on" : "off";
      q("detailFields").querySelector(`[data-url="${type}"]`).value = motionEventUrl(location.origin, original.id, on);
      q("detailFields").querySelector(`[data-copy="${type}"]`).addEventListener("click", () => copyUrl(type));
    }
  }
  if (name === "lamp") {
    for (const button of q("detailFields").querySelectorAll("[data-test]")) {
      button.addEventListener("click", () => testLamp(button.dataset.test === "true"));
    }
  }
  updateDetailOptions();
  renderStatus();
  q("detailTitle").focus();
}

function updateDetailOptions() {
  if (!selection) return;
  if (selection.kind === "motion") {
    const polling = field("pollingEnabled").checked;
    field("pollHost").disabled = !polling;
    field("pollHost").required = polling;
    field("pollChannel").disabled = !polling && !selection.original.legacyNode;
  }
  if (selection.kind === "lamp") {
    const value = readDetail();
    const canTest = !selection.isNew && sameItem(value, selection.original) && value.enabled && !saving;
    for (const button of q("detailFields").querySelectorAll("[data-test]")) button.disabled = !canTest;
    field("onUrl").required = value.enabled;
    field("offUrl").required = value.enabled;
    q("testHint").textContent = canTest ?
      "Test schaltet die echte Lampe. HTTP-Erfolg bestaetigt nicht den tatsaechlichen Zustand." :
      "Tests sind nur fuer aktivierte, bereits gespeicherte und unveraenderte Lampen moeglich.";
  }
  if (selection.kind === "general") {
    const sun = field("windowUseSun").value === "true";
    q("fixedWindowRow").hidden = sun;
    q("sunFields").hidden = !sun;
    for (const input of q("fixedWindowRow").querySelectorAll("input, select")) input.disabled = sun;
    for (const input of q("sunFields").querySelectorAll("input, select")) input.disabled = !sun;
  }
}

async function copyUrl(type) {
  const input = q("detailFields").querySelector(`[data-url="${type}"]`);
  input.focus();
  input.select();
  if (!navigator.clipboard || !window.isSecureContext) {
    notify("URL markiert. Mit Strg+C oder dem Kopiermenue kopieren.");
    return;
  }
  try {
    await navigator.clipboard.writeText(input.value);
    notify("URL kopiert.");
  } catch (error) {
    notify(`Kopieren fehlgeschlagen: ${error.message}. URL ist zum manuellen Kopieren markiert.`, true);
  }
}

function setSaving(value) {
  saving = value;
  for (const id of ["detailFields", "saveBtn", "cancelBtn", "backBtn", "deleteBtn"]) q(id).disabled = value;
}

async function saveDetail(remove = false) {
  if (saving || !selection) return;
  if (!remove && !q("elementForm").reportValidity()) return;
  if (remove && !window.confirm(`"${selection.original.name}" wirklich entfernen?`)) return;
  const edit = selection;
  const value = remove ? null : readDetail();
  setSaving(true);
  try {
    const fresh = await lampRequest("/api/config");
    config = fresh;
    const payload = buildDevicePayload(fresh, edit, value, remove);
    await lampRequest("/api/config", payload);
    // Keep an authoritative local snapshot without turning a successful POST into a failed save.
    config = { ...fresh, ...payload };
    notify(remove ? "Eintrag entfernt." : "Gespeichert. Lampen werden AUS geschaltet.");
    setSaving(false);
    location.hash = "overview";
    renderRoute();
  } catch (error) {
    notify(error.message, true);
  } finally {
    setSaving(false);
    updateDetailOptions();
  }
}

async function testLamp(on) {
  if (saving || !selection || selection.kind !== "lamp" || selection.isNew) return;
  const edit = selection;
  if (!sameItem(readDetail(), edit.original) || !edit.original.enabled) return;
  const buttons = q("detailFields").querySelectorAll("[data-test]");
  buttons.forEach((button) => { button.disabled = true; });
  try {
    await lampRequest("/api/manual/relay", { id: edit.original.id, on });
    notify(`${edit.original.name}: ${on ? "EIN" : "AUS"} angefordert.`);
    await loadStatus();
  } catch (error) {
    notify(error.message, true);
  } finally {
    if (selection === edit) updateDetailOptions();
  }
}

function renderStatus() {
  if (!latestStatus) return;
  for (const element of q("overview").querySelectorAll("[data-kind]")) {
    const motion = element.dataset.kind === "motion";
    const state = (motion ? latestStatus.motionSensors : latestStatus.lamps).find((item) => item.id === element.dataset.id);
    element.textContent = state ? (motion ? motionStatusText(state) : lampStatusText(state)) : "Status noch nicht verfuegbar";
    element.classList.toggle("err", Boolean(state && state.enabled && (motion ? state.timedOut || state.pollHealthy === false : state.error)));
  }
  if (!selection) return;
  const { kind, original, isNew } = selection;
  let text = "";
  if (kind === "lamp" || kind === "motion" || kind === "local") {
    const list = kind === "lamp" ? latestStatus.lamps : latestStatus.motionSensors;
    const state = list.find((item) => item.id === (kind === "local" ? "esp" : original.id));
    text = isNew ? "Noch nicht gespeichert." : state ? (kind === "lamp" ? lampStatusText(state) : motionStatusText(state)) : "Status noch nicht verfuegbar";
  }
  q("detailStatus").textContent = text;
  if (kind === "general" && latestStatus.sunrise && latestStatus.sunset) {
    q("sunHint").textContent = `Heute: Sonnenaufgang ${latestStatus.sunrise}, Sonnenuntergang ${latestStatus.sunset}`;
  }
}

async function loadStatus() {
  if (statusLoading) return;
  statusLoading = true;
  try {
    latestStatus = await lampRequest("/api/status");
    renderStatus();
    q("statusError").hidden = true;
  } catch (error) {
    q("statusError").textContent = `Status konnte nicht geladen werden: ${error.message}`;
    q("statusError").hidden = false;
  } finally {
    statusLoading = false;
  }
}

async function initSettings() {
  for (const kind of ["button", "motion", "lamp"]) {
    q(`add${kind[0].toUpperCase() + kind.slice(1)}Btn`).addEventListener("click", () => { location.hash = `create/${kind}`; });
  }
  q("generalBtn").addEventListener("click", () => { location.hash = "general"; });
  for (const id of ["backBtn", "cancelBtn"]) q(id).addEventListener("click", () => { location.hash = "overview"; });
  q("elementForm").addEventListener("submit", (event) => { event.preventDefault(); saveDetail(); });
  q("detailFields").addEventListener("input", updateDetailOptions);
  q("detailFields").addEventListener("change", updateDetailOptions);
  q("deleteBtn").addEventListener("click", () => saveDetail(true));
  window.addEventListener("hashchange", renderRoute);
  window.addEventListener("beforeunload", (event) => {
    if (selection && (saving || selection.isNew || !sameItem(readDetail(), selection.original))) {
      event.preventDefault();
      event.returnValue = "";
    }
  });
  try {
    config = await lampRequest("/api/config");
    notify("Eintrag auswaehlen oder mit Create neu anlegen.");
    renderOverview();
    renderRoute();
  } catch (error) {
    notify(`Einstellungen konnten nicht geladen werden: ${error.message}. Seite bitte neu laden.`, true);
  }
  loadStatus();
  setInterval(loadStatus, 3000);
}

document.addEventListener("DOMContentLoaded", initSettings);
