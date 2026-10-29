function lampStatusText(lamp) {
  if (!lamp.enabled) return "Deaktiviert";
  if (lamp.pending) return `${lamp.requestedOn ? "EIN" : "AUS"} angefordert ...`;
  if (lamp.error) return `Fehler bei ${lamp.lastCommandOn ? "EIN" : "AUS"}: ${lamp.error}`;
  if (typeof lamp.lastCommandOn !== "boolean") return "Noch kein Befehl gesendet";
  return `Zuletzt ${lamp.lastCommandOn ? "EIN" : "AUS"} gesendet (HTTP ${lamp.httpCode}); Zustand nicht bestaetigt`;
}

async function lampRequest(url, payload) {
  const options = payload === undefined ? {} : {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload)
  };
  const response = await fetch(url, options);
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || `HTTP ${response.status}`);
  return result;
}
