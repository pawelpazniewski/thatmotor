"use strict";

// Control states mirror sm_state in firmware (index == enum value).
const STATE_NAMES = ["DISARMED", "ARMED", "FAILSAFE", "ESC_CALIBRATION", "DEPLOY"];
const SOURCE_NAMES = ["DEFAULTS", "NVS", "MIXED_RECOVERED"];
// Spot-lock sub-state mirrors spot_lock_substate in firmware (index == value).
const SPOT_LOCK_NAMES = ["off", "active", "paused"];
// Goto sub-state mirrors goto_substate in firmware (index == value); shares the
// spot_lock sub-state enum (off/active/paused).
const GOTO_STATE_NAMES = ["off", "active", "paused"];

// Numeric state aliases (mirror sm_state) for readable comparisons.
const STATE_DISARMED = 0;
const STATE_ARMED = 1;
const STATE_FAILSAFE = 2;
const STATE_DEPLOY = 4;

// arm_reason mapping mirrors sm_arm_reason in firmware (index == enum value).
const ARM_REASON_TEXT = [
  "Ready", // SM_ARM_READY
  "No valid RC signal", // SM_ARM_NO_RC
  "Throttle not at neutral", // SM_ARM_THROTTLE_NOT_NEUTRAL
  "Calibration in progress", // SM_ARM_CALIBRATING
  "Applying settings", // SM_ARM_SETTINGS_APPLYING
];
const ARM_READY = 0;

// How long to wait for telemetry to confirm an arm/disarm before declaring it
// failed. ~1 s comfortably covers the ~10 Hz telemetry + ~50 Hz control loop.
const CMD_CONFIRM_MS = 1000;

// How long a resolved arm/disarm result stays pinned before the live readiness
// hint resumes, so a success/error is readable instead of vanishing next frame.
const RESULT_STICKY_MS = 5000;

function armReasonText(reason) {
  return ARM_REASON_TEXT[reason] || `reason ${reason}`;
}

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
  deploy_servo_us: "Deploy servo (µs)",
  click_window_ms: "CH4 click window (ms)",
  spot_lock_deadband_m: "Spot-lock deadband (m)",
  spot_lock_max_throttle_pct: "Spot-lock max throttle (%)",
  spot_lock_throttle_gain: "Spot-lock throttle gain (norm/m)",
  spot_lock_servo_gain: "Spot-lock servo gain (norm/deg)",
  goto_comms_timeout_ms: "Goto comms timeout (ms)",
  goto_slowdown_distance_m: "Goto: dystans hamowania [m]",
};

let lastState = null;
let lastArmReason = ARM_READY;

// Pending arm/disarm command awaiting telemetry confirmation. null when idle.
// Shape: { kind: "arm" | "disarm", target: stateValue, timer: timeoutId }.
let cmdPending = null;

// Epoch (ms) until which the last command result stays pinned over the live hint.
let cmdResultUntil = 0;

function $(id) { return document.getElementById(id); }

function setText(id, value) {
  const el = $(id);
  if (el) el.textContent = value;
}

// --- Live telemetry over WebSocket (lossy ~10 Hz) ---

function applyTelemetry(t) {
  lastState = t.state;
  lastArmReason = t.arm_reason;
  resolvePendingCommand(t.state);
  setText("state", STATE_NAMES[t.state] || t.state);
  setText("rc_valid", t.rc_valid ? "yes" : "NO");
  setText("ch1_us", t.ch1_us);
  setText("ch2_us", t.ch2_us);
  setText("ch4_us", t.ch4_us);
  setText("ch3_us", t.ch3_us);
  setText("ch1_period_us", t.ch1_period_us);
  setText("ch2_period_us", t.ch2_period_us);
  setText("ch1_valid", t.ch1_valid ? "yes" : "NO");
  setText("ch2_valid", t.ch2_valid ? "yes" : "NO");
  setText("servo_us", t.servo_us);
  setText("esc_us", t.esc_us);
  setText("servo_trim_us", t.servo_trim_us);
  setText("gps_fix", t.gps_fix ? "yes" : "NO");
  setText("gps_sats", t.gps_sats);
  setText("gps_lat", (t.gps_lat_e7 / 1e7).toFixed(6));
  setText("gps_lon", (t.gps_lon_e7 / 1e7).toFixed(6));
  setText("gps_speed", (t.gps_speed_cms / 100).toFixed(1));
  setText("imu_heading", (t.imu_heading_deg10 / 10).toFixed(1) + "°");
  setText("imu_calib", t.imu_calib);
  setText("imu_ok", t.imu_ok ? "yes" : "NO");
  setText("spot_lock_state", SPOT_LOCK_NAMES[t.spot_lock_state] || t.spot_lock_state);
  setText("spot_lock_err", t.spot_lock_err_m + " m");
  setText("spot_lock_bearing", (t.spot_lock_bearing_deg10 / 10).toFixed(1) + "°");
  setText("spot_lock_bow", (t.imu_heading_deg10 / 10).toFixed(1) + "°");
  setText("goto_state", GOTO_STATE_NAMES[t.goto_state] || t.goto_state);
  setText("goto_target_lat", (t.goto_target_lat_e7 / 1e7).toFixed(6));
  setText("goto_target_lon", (t.goto_target_lon_e7 / 1e7).toFixed(6));
  setText("goto_err", t.goto_err_m + " m");
  setText("goto_bearing", (t.goto_bearing_deg10 / 10).toFixed(1) + "°");
  setText("goto_bow", (t.imu_heading_deg10 / 10).toFixed(1) + "°");
  setText("goto_arrived", t.goto_arrived ? "yes" : "no");
  setText("app_link_fresh", t.app_link_fresh ? "yes" : "NO");
  setText("source", SOURCE_NAMES[t.source] || t.source);
  setText("settings_valid", t.settings_valid ? "yes" : "no");
  setText("calibrated", t.calibrated ? "yes" : "NO");
  setText("defaults_used", t.defaults_used ? "yes" : "no");
  setText("nvs_error", t.nvs_error ? "YES" : "no");
  $("uncal_warn").classList.toggle("hidden", t.calibrated);
  updateEditLock(t.state === STATE_DISARMED);
  renderIdleArmHint(t.state, t.arm_reason);
}

// --- Arm/Disarm result status ---

function setCmdStatus(text, kind) {
  const el = $("cmd-status");
  if (!el) return;
  el.textContent = text;
  el.className = kind ? `cmd-status cmd-${kind}` : "cmd-status";
}

// A terminal result (success/failure): show it and pin it for RESULT_STICKY_MS
// so the live readiness hint does not wipe it on the next telemetry frame.
function setCmdResult(text, kind) {
  setCmdStatus(text, kind);
  cmdResultUntil = Date.now() + RESULT_STICKY_MS;
}

// While no command is in flight, show the live readiness derived from telemetry.
function renderIdleArmHint(state, reason) {
  if (cmdPending) return; // a command result is being shown; don't overwrite
  if (Date.now() < cmdResultUntil) return; // keep the last result readable
  if (state !== STATE_DISARMED) {
    setCmdStatus("");
    return;
  }
  if (reason === ARM_READY) {
    setCmdStatus("Ready to arm", "ok");
  } else {
    setCmdStatus(`Cannot arm: ${armReasonText(reason)}`, "warn");
  }
}

// Resolve an in-flight arm/disarm once telemetry shows the intent satisfied.
// Disarm is satisfied by DISARMED *or* FAILSAFE: both mean the drive is stopped
// at neutral, so a disarm in failsafe is a stop, not a "still armed" failure.
// Terminal labels per command kind once telemetry shows the intent satisfied.
const CMD_OK_TEXT = {
  arm: "✓ Armed",
  disarm: "✓ Disarmed",
  deploy: "✓ Deployed — motor raised, drive off",
  stow: "✓ Stowed",
};

function resolvePendingCommand(state) {
  if (!cmdPending) return;
  if (cmdPending.kind === "arm") {
    if (state !== STATE_ARMED) return;
    clearPendingTimer();
    setCmdResult(CMD_OK_TEXT.arm, "ok");
    return;
  }
  if (cmdPending.kind === "deploy") {
    if (state !== STATE_DEPLOY) return;
    clearPendingTimer();
    setCmdResult(CMD_OK_TEXT.deploy, "ok");
    return;
  }
  // disarm / stow: both target DISARMED; a disarm in failsafe is still a stop.
  if (state === STATE_DISARMED) {
    clearPendingTimer();
    setCmdResult(CMD_OK_TEXT[cmdPending.kind] || "✓ Disarmed", "ok");
  } else if (state === STATE_FAILSAFE && cmdPending.kind === "disarm") {
    clearPendingTimer();
    setCmdResult("✓ Drive stopped — failsafe (RC lost)", "warn");
  }
}

// Fired ~1 s after a command if telemetry never confirmed the intent.
function onCommandTimeout() {
  if (!cmdPending) return;
  const kind = cmdPending.kind;
  cmdPending = null;
  if (kind === "arm") {
    setCmdResult(`✗ Not armed — ${armReasonText(lastArmReason)}`, "err");
  } else if (kind === "deploy") {
    setCmdResult("✗ Not deployed — deploy only from DISARMED", "err");
  } else if (kind === "stow") {
    setCmdResult("✗ Not stowed — still deployed", "err");
  } else if (lastState === STATE_FAILSAFE) {
    setCmdResult("✓ Drive stopped — failsafe (RC lost)", "warn");
  } else {
    setCmdResult("✗ Still armed", "err");
  }
}

function clearPendingTimer() {
  if (cmdPending) clearTimeout(cmdPending.timer);
  cmdPending = null;
}

function startPendingCommand(kind, target, pendingText) {
  if (cmdPending) clearTimeout(cmdPending.timer);
  cmdResultUntil = 0; // this flow now owns the status surface
  setCmdStatus(pendingText, "pending");
  const timer = setTimeout(onCommandTimeout, CMD_CONFIRM_MS);
  cmdPending = { kind, target, timer };
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
  const stateName = STATE_NAMES[lastState] || lastState;
  const sent = collectForm();
  status.textContent = `saving… (state: ${stateName})`;
  const res = await fetch("/api/params", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(sent),
  });
  const env = await res.json().catch(() => ({}));
  if (env.error) {
    status.textContent =
      `✗ REJECTED ${res.status}: ${env.error.code} — ${env.error.message} (state ${stateName})`;
    return;
  }
  // Let the 50 Hz loop apply the pending set, then read back the device truth so
  // a silently-unapplied save is visible instead of looking "saved".
  await new Promise((r) => setTimeout(r, 300));
  const verify = await fetch("/api/params").then((r) => r.json()).catch(() => ({}));
  if (!verify.data) {
    status.textContent = `saved, but read-back failed (state ${stateName})`;
    return;
  }
  const diffs = Object.keys(sent).filter(
    (k) => String(verify.data[k]) !== String(sent[k]),
  );
  buildForm(verify.data); // form now mirrors the device exactly
  if (diffs.length === 0) {
    status.textContent =
      `✓ saved & confirmed on device (state ${stateName}). NVS write ~3 s later while DISARMED — don't power-cycle immediately.`;
  } else {
    status.textContent =
      `⚠ NOT applied — ${diffs.join(", ")} reverted (device kept old value). State=${stateName}: editing needs a stable DISARMED.`;
  }
}

// --- Commands ---

// POST a command. Returns the parsed envelope; throws on a non-OK envelope so
// callers can surface error.code/message instead of a bogus "success".
async function sendCommand(cmd) {
  const res = await fetch("/api/command", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ cmd }),
  });
  const env = await res.json().catch(() => ({}));
  if (env.error) {
    throw new Error(`${env.error.code} - ${env.error.message}`);
  }
  return env;
}

// Arm is asynchronous: the POST only acknowledges receipt; the result lands in
// telemetry (state). Show "Arming…", then let telemetry confirm or time out.
async function handleArm() {
  startPendingCommand("arm", STATE_ARMED, "Arming…");
  try {
    await sendCommand("arm");
  } catch (e) {
    clearPendingTimer();
    setCmdResult(`✗ Command rejected: ${e.message}`, "err");
  }
}

async function handleDisarm() {
  startPendingCommand("disarm", STATE_DISARMED, "Disarming…");
  try {
    await sendCommand("disarm");
  } catch (e) {
    clearPendingTimer();
    setCmdResult(`✗ Command rejected: ${e.message}`, "err");
  }
}

// Deploy raises the motor (servo to deploy_servo_us, drive held off). Like arm,
// it is asynchronous: confirm via telemetry state -> DEPLOY.
async function handleDeploy() {
  startPendingCommand("deploy", STATE_DEPLOY, "Deploying…");
  try {
    await sendCommand("deploy");
  } catch (e) {
    clearPendingTimer();
    setCmdResult(`✗ Command rejected: ${e.message}`, "err");
  }
}

// Stow leaves DEPLOY back to DISARMED. Confirm via telemetry state -> DISARMED.
async function handleStow() {
  startPendingCommand("stow", STATE_DISARMED, "Stowing…");
  try {
    await sendCommand("stow");
  } catch (e) {
    clearPendingTimer();
    setCmdResult(`✗ Command rejected: ${e.message}`, "err");
  }
}

// Calibration commands have their own status surface in the calibration card;
// here we only need to report a rejected envelope, not track a target state.
async function sendCalibCommand(cmd) {
  try {
    await sendCommand(cmd);
  } catch (e) {
    setCmdResult(`✗ Command rejected: ${e.message}`, "err");
  }
}

// Servo neutral trim: a live nudge (left/right) or a Save. The new value lands
// back via telemetry (servo_trim_us); we only surface a rejected envelope here.
async function sendTrimCommand(cmd) {
  try {
    await sendCommand(cmd);
  } catch (e) {
    setCmdResult(`✗ Command rejected: ${e.message}`, "err");
  }
}

function handleTrimLeft() { return sendTrimCommand("trim_left"); }
function handleTrimRight() { return sendTrimCommand("trim_right"); }
function handleTrimSave() { return sendTrimCommand("trim_save"); }

function wireButtons() {
  $("btn-arm").onclick = handleArm;
  $("btn-disarm").onclick = handleDisarm;
  $("btn-deploy").onclick = handleDeploy;
  $("btn-stow").onclick = handleStow;
  $("btn-save").onclick = saveParams;
  $("btn-calib-start").onclick = () => sendCalibCommand("calib_start");
  $("btn-calib-next").onclick = () => sendCalibCommand("calib_next");
  $("btn-calib-cancel").onclick = () => sendCalibCommand("calib_cancel");
  $("btn-trim-left").onclick = handleTrimLeft;
  $("btn-trim-right").onclick = handleTrimRight;
  $("btn-trim-save").onclick = handleTrimSave;
  $("calib-ack").onchange = () => updateEditLock(lastState === STATE_DISARMED);
}

window.addEventListener("DOMContentLoaded", () => {
  wireButtons();
  loadParams();
  connectWs();
});
