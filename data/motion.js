function motionEventUrl(origin, id, active) {
  return `${origin}/api/event/motion?id=${encodeURIComponent(id)}&state=${active ? "1" : "0"}`;
}

function motionStatusText(sensor) {
  if (!sensor.enabled) return "Deaktiviert";
  const parts = [sensor.active ? "Bewegung aktiv" : "Keine Bewegung"];
  if (typeof sensor.lastEventAgeMs === "number") {
    const sources = { http: "HTTP", gpio: "GPIO", poll: "Polling" };
    parts.push(`Letzte Meldung vor ${Math.floor(sensor.lastEventAgeMs / 1000)} s (${sources[sensor.lastSource] || sensor.lastSource})`);
  } else {
    parts.push("Noch keine Meldung seit Start");
  }
  if (sensor.timedOut) parts.push("Plausibilitaets-Timeout: auf inaktiv gesetzt");
  if (sensor.pollingEnabled) {
    parts.push(sensor.pollHealthy === false ? "Polling fehlgeschlagen" :
      sensor.pollHealthy === true ? "Polling OK" : "Polling noch nicht erfolgt");
  }
  return parts.join(" | ");
}

function motionSummaryText(sensor, overview = false) {
  if (!sensor.enabled) return "Deaktiviert";
  const errors = [];
  if (sensor.timedOut) errors.push("Keine Meldung: Plausibilitaets-Timeout");
  if (sensor.pollHealthy === false) errors.push("Polling fehlgeschlagen");
  if (errors.length) return errors.join(" | ");
  return overview ? "" : sensor.active ? "Bewegung" : "Keine Bewegung";
}
