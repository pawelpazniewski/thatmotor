"use strict";

// Control states mirror sm_state in firmware.
const STATE_NAMES = ["DISARMED", "ARMED", "FAILSAFE", "ESC_CALIBRATION"];
const SOURCE_NAMES = ["DEFAULTS", "NVS", "MIXED_RECOVERED"];

const BOOL_KEYS = new Set([
  "servo_reverse",
  "throttle_reverse",
  "ch4_mode_switch_enabled",
]);

// Friendly labels for params whose JSON key alone is not self-explanatory.
// Any key not listed falls back to the raw key (the existing behaviour).
const PARAM_LABELS = {
  max_throttle_fwd_pct: "Max throttle forward (%)",
  max_throttle_rev_pct: "Max throttle reverse (%)",
  ch4_mode_switch_enabled: "CH4 mode switch enabled",
  ch4_switch_threshold_us: "CH4 switch threshold (µs)",
};

let lastState = null;

function $(id) { return document.getElementById(id); }

function setText(id, value) {
  const el = $(id);
  if (el) el.textContent = value;
}

// --- Live telemetry over WebSocket (lossy ~10 Hz) ---

function applyTelemetry(t) {
  lastState = t.state;
  setText("state", STATE_NAMES[t.state] || t.state);
  setText("rc_valid", t.rc_valid ? "yes" : "NO");
  setText("ch1_us", t.ch1_us);
  setText("ch2_us", t.ch2_us);
  setText("ch4_us", t.ch4_us);
  setText("servo_us", t.servo_us);
  setText("esc_us", t.esc_us);
  setText("source", SOURCE_NAMES[t.source] || t.source);
  setText("settings_valid", t.settings_valid ? "yes" : "no");
  setText("calibrated", t.calibrated ? "yes" : "NO");
  setText("defaults_used", t.defaults_used ? "yes" : "no");
  setText("nvs_error", t.nvs_error ? "YES" : "no");
  $("uncal_warn").classList.toggle("hidden", t.calibrated);
  updateEditLock(t.state === 0);
}

function updateEditLock(isDisarmed) {
  $("edit-lock").classList.toggle("hidden", isDisarmed);
  document.querySelectorAll("#params-form input").forEach((el) => {
    el.disabled = !isDisarmed;
  });
  $("btn-save").disabled = !isDisarmed;
  const ack = $("calib-ack").checked;
  $("btn-calib-start").disabled = !(isDisarmed && ack);
}

function connectWs() {
  const ws = new WebSocket(`ws://${location.host}/ws`);
  ws.onopen = () => {
    $("conn").textContent = "connected";
    $("conn").className = "badge badge-on";
  };
  ws.onclose = () => {
    $("conn").textContent = "disconnected";
    $("conn").className = "badge badge-off";
    setTimeout(connectWs, 1000);
  };
  ws.onmessage = (ev) => {
    try { applyTelemetry(JSON.parse(ev.data)); } catch (e) { /* ignore */ }
  };
}

// --- Parameters form ---

async function loadParams() {
  const res = await fetch("/api/params");
  const env = await res.json();
  if (!env.data) return;
  buildForm(env.data);
}

function buildForm(params) {
  const form = $("params-form");
  form.innerHTML = "";
  Object.keys(params).forEach((key) => {
    if (key === "schema_version") return;
    const label = document.createElement("label");
    label.textContent = PARAM_LABELS[key] || key;
    const input = document.createElement("input");
    input.name = key;
    if (BOOL_KEYS.has(key)) {
      input.type = "checkbox";
      input.checked = !!params[key];
    } else {
      input.type = "number";
      input.value = params[key];
    }
    label.appendChild(input);
    form.appendChild(label);
  });
}

function collectForm() {
  const out = {};
  document.querySelectorAll("#params-form input").forEach((el) => {
    out[el.name] = el.type === "checkbox" ? el.checked : Number(el.value);
  });
  return out;
}

async function saveParams() {
  const status = $("save-status");
  status.textContent = "saving...";
  const res = await fetch("/api/params", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(collectForm()),
  });
  const env = await res.json();
  if (env.error) {
    status.textContent = `error (${res.status}): ${env.error.code} - ${env.error.message}`;
  } else {
    status.textContent = "saved (applied while DISARMED, persisted to NVS)";
  }
}

// --- Commands ---

async function sendCommand(cmd) {
  await fetch("/api/command", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ cmd }),
  });
}

function wireButtons() {
  $("btn-arm").onclick = () => sendCommand("arm");
  $("btn-disarm").onclick = () => sendCommand("disarm");
  $("btn-save").onclick = saveParams;
  $("btn-calib-start").onclick = () => sendCommand("calib_start");
  $("btn-calib-next").onclick = () => sendCommand("calib_next");
  $("btn-calib-cancel").onclick = () => sendCommand("calib_cancel");
  $("calib-ack").onchange = () => updateEditLock(lastState === 0);
}

window.addEventListener("DOMContentLoaded", () => {
  wireButtons();
  loadParams();
  connectWs();
});
