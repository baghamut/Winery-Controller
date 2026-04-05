// =============================================================================
// web_ui.cpp – Full embedded Web UI
//
// Shared-string/state alignment:
// • Uses shared labels and offline thresholds from /state.
// • Avoids hardcoded Room/Tank/Pillar/Offline/-100 sentinel logic.
// • Keeps backend command formats unchanged.
// =============================================================================
#include "web_ui.h"
#include "config.h"
#include "state.h"
#include "control.h"

static void handleRoot(WebServer& server)
{
    const char* html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Cask & Crown Winery Controller</title>
  <script src="https://cdn.tailwindcss.com"></script>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600&display=swap');
    body { font-family: Inter, system-ui, sans-serif; }
    .card { background: #202020; border: 1px solid #333; border-radius: 12px; }
    .sensor-offline { color: #e02424; }
    .sensor-warn { color: #ff7a1a; }
    .sensor-danger { color: #e02424; animation: pulse 1s infinite; }
    @keyframes pulse { 50% { opacity: 0.4; } }
  </style>
</head>
<body class="bg-[#111111] text-[#f5f5f5] min-h-screen">
  <div class="max-w-6xl mx-auto p-6">

    <div class="flex items-center justify-between mb-8 bg-[#202020] border border-[#333] rounded-2xl px-6 py-4">
      <div class="flex items-center gap-3">
        <div class="w-9 h-9 rounded-xl flex items-center justify-center bg-orange-500/15 border border-orange-400/30 text-orange-400">
          <svg viewBox="0 0 24 24" class="w-6 h-6" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
            <path d="M7 5.5c1.6-.8 3.3-1.2 5-1.2s3.4.4 5 1.2"/>
            <path d="M7 18.5c1.6.8 3.3 1.2 5 1.2s3.4-.4 5-1.2"/>
            <path d="M7 5.5c-1.2 1.7-1.8 4-1.8 6.5S5.8 16.8 7 18.5"/>
            <path d="M17 5.5c1.2 1.7 1.8 4 1.8 6.5s-.6 4.8-1.8 6.5"/>
            <path d="M9 4.9c-.9 1.8-1.4 4.3-1.4 7.1s.5 5.3 1.4 7.1"/>
            <path d="M15 4.9c.9 1.8 1.4 4.3 1.4 7.1s-.5 5.3-1.4 7.1"/>
            <path d="M8 8h8"/>
            <path d="M7.4 12h9.2"/>
            <path d="M8 16h8"/>
          </svg>
        </div>
        <div>
          <h1 id="app-title" class="text-2xl font-semibold tracking-tight">Cask &amp; Crown</h1>
          <p class="text-xs text-gray-400 -mt-1"><span id="app-subtitle">Winery Controller</span> • FW <span id="fw">1.0.0</span></p>
        </div>
      </div>
      <div class="flex items-center gap-8 text-sm">
        <div id="hdr-status" class="flex items-center gap-2 font-medium"></div>
        <div id="hdr-t1" class="flex items-center gap-1"></div>
        <div id="hdr-max" class="flex items-center gap-1 text-gray-400"></div>
        <div id="hdr-total" class="flex items-center gap-1 text-gray-400"></div>
        <div onclick="showWifiModal()" class="cursor-pointer flex items-center gap-1 hover:text-orange-400">
          <span id="hdr-ip" class="font-mono"></span>
        </div>
      </div>
    </div>

    <div id="screen-0" class="screen">
      <div class="grid grid-cols-2 gap-6">
        <div onclick="sendCommand('MODE:1')" class="card p-8 hover:border-orange-400 cursor-pointer transition-colors flex flex-col items-center justify-center text-center">
          <div class="text-6xl mb-4">🥃</div>
          <h2 id="mode-dist-title" class="text-3xl font-semibold text-orange-400">DISTILLATION</h2>
          <p class="text-gray-400 mt-2">SSR1–3</p>
        </div>
        <div onclick="sendCommand('MODE:2')" class="card p-8 hover:border-orange-400 cursor-pointer transition-colors flex flex-col items-center justify-center text-center">
          <div class="text-6xl mb-4">⚗️</div>
          <h2 id="mode-rect-title" class="text-3xl font-semibold text-orange-400">RECTIFICATION</h2>
          <p class="text-gray-400 mt-2">SSR4–5</p>
        </div>
      </div>
    </div>

    <div id="screen-1" class="screen hidden">
      <div class="card p-6">
        <div class="flex justify-between items-baseline mb-6">
          <h2 id="ctrl-title" class="text-xl font-semibold"></h2>
          <div class="flex items-center gap-2">
            <button onclick="showSensorMapper()" class="text-xs px-3 py-1.5 rounded-full border border-gray-500/60 text-gray-300 hover:bg-gray-700">
              Sensors
            </button>
            <button onclick="showValveScreen()" class="text-xs px-3 py-1.5 rounded-full border border-orange-400/60 text-orange-300 hover:bg-orange-500/10">
              Valves Control
            </button>
          </div>
        </div>

        <div class="bg-[#181818] rounded-2xl px-5 py-4 mb-4">
          <div class="flex items-center gap-4">
            <div id="power-label-control" class="text-sm font-medium text-orange-400 w-32">Power</div>
            <input type="range" min="0" max="100" value="0" id="master-slider" class="flex-1 accent-orange-400">
            <div class="w-14 text-right font-mono text-sm" id="master-pct">0%</div>
          </div>
          <p id="power-help-text" class="text-xs text-gray-500 mt-2 ml-32">All active SSRs receive this duty cycle simultaneously.</p>
        </div>

        <div class="mt-6 pt-5 border-t border-gray-700">
          <div id="limits-danger-title" class="text-xs uppercase tracking-widest mb-3 text-gray-400">Sensor Max Thresholds Danger</div>
          <div id="limits-container" class="space-y-2 text-sm"></div>
        </div>

        <div class="flex gap-3 mt-6 pt-4 border-t border-gray-700">
          <button onclick="sendCommand('MODE:0')" id="btn-back" class="flex-1 py-3 bg-red-600 hover:bg-red-500 rounded-2xl text-white font-medium">BACK</button>
          <button onclick="sendCommand('START')" id="btn-start" class="flex-1 py-3 bg-green-600 hover:bg-green-500 rounded-2xl text-white font-medium disabled:opacity-40 disabled:cursor-not-allowed">START</button>
        </div>
      </div>
    </div>

    <div id="screen-2" class="screen hidden">
      <div class="card p-6">
        <div id="mon-sensors" class="grid grid-cols-2 gap-x-8 gap-y-3 mb-5 text-lg"></div>

        <div class="bg-[#181818] rounded-2xl px-5 py-4 mb-4">
          <div class="flex items-center gap-4">
            <div id="power-label-monitor" class="text-sm font-medium text-orange-400 w-32">Power</div>
            <input type="range" min="0" max="100" value="0" id="mon-load-slider" class="flex-1 accent-orange-400">
            <div class="w-14 text-right font-mono text-sm" id="mon-load-pct">0%</div>
          </div>
        </div>

        <button onclick="sendCommand('STOP')" id="btn-stop" class="w-full py-4 bg-red-600 hover:bg-red-500 rounded-2xl text-white font-medium">STOP</button>
      </div>
    </div>

    <div id="screen-3" class="screen hidden">
      <div class="card p-6">
        <div class="flex justify-between items-baseline mb-5">
          <h2 id="valves-title" class="text-xl font-semibold">Valves Control</h2>
          <button onclick="backFromValveScreen()" class="text-xs px-3 py-1.5 rounded-full border border-gray-500 text-gray-300 hover:bg-gray-700">BACK</button>
        </div>
        <div id="valve-rules-container" class="space-y-3"></div>
      </div>
    </div>

    <div id="wifi-modal" class="hidden fixed inset-0 bg-black/80 flex items-center justify-center z-50">
      <div class="bg-[#202020] border border-[#333] rounded-3xl p-8 w-full max-w-md mx-4">
        <h2 id="wifi-title" class="text-2xl font-semibold mb-6 text-orange-400">WiFi Setup</h2>
        <div class="space-y-6">
          <div>
            <label id="wifi-ssid-label" class="block text-gray-400 text-sm mb-2">SSID</label>
            <input id="wifi-ssid" type="text" class="w-full bg-[#181818] border border-[#333] rounded-2xl px-5 py-4 text-white focus:outline-none focus:border-orange-400">
          </div>
          <div>
            <label id="wifi-pass-label" class="block text-gray-400 text-sm mb-2">PASS</label>
            <input id="wifi-pass" type="password" class="w-full bg-[#181818] border border-[#333] rounded-2xl px-5 py-4 text-white focus:outline-none focus:border-orange-400">
          </div>
          <div class="flex gap-4 mt-10">
            <button onclick="hideWifiModal()" id="btn-cancel" class="flex-1 py-4 bg-gray-700 hover:bg-gray-600 rounded-2xl font-medium">Cancel</button>
            <button onclick="saveWifiConfig()" id="btn-save" class="flex-1 py-4 bg-orange-500 hover:bg-orange-400 rounded-2xl font-medium text:white">SAVE</button>
          </div>
        </div>
      </div>
    </div>

    <div id="sensor-mapper-modal" class="hidden fixed inset-0 bg-black/80 flex items-center justify-center z-50">
      <div class="bg-[#202020] border border-[#333] rounded-3xl p-6 w-full max-w-2xl mx-4 max-h-[90vh] flex flex-col">
        <div class="flex items-center justify-between mb-4">
          <h2 class="text-xl font-semibold text-orange-400">Sensor Mapping</h2>
          <button onclick="hideSensorMapper()" class="text-xs px-3 py-1.5 rounded-full border border-gray-500 text-gray-300 hover:bg-gray-700">CLOSE</button>
        </div>
        <div class="flex items-center gap-3 mb-4">
          <button onclick="scanSensorBus()" id="btn-scan" class="px-4 py-2 bg-orange-500 hover:bg-orange-400 rounded-xl text-sm font-medium text-white">SCAN BUS</button>
          <span id="scan-status" class="text-sm text-gray-400"></span>
        </div>
        <div id="sensor-mapper-rows" class="overflow-y-auto space-y-2 flex-1"></div>
      </div>
    </div>
  </div>

  <script>
    let currentState = null;
    const REFRESH_MS = 600;

    // Optimistic master-power locking.
    // When the user drags a slider, we freeze both sliders at the local value
    // so polls don't snap them back. The lock clears as soon as the server
    // confirms the new value, or after a 3-second safety timeout.
    let mPwrPending    = false;   // true while user interaction is unconfirmed
    let mPwrPendingVal = 0;       // value the user set
    let mPwrTimer      = null;    // fallback release timer

    // Map backend ValveOp numeric values to command strings and display symbols.
    // Numeric values must match the ValveOp enum in state.h.
    const OP_CMD  = { 0:'NONE', 1:'GT', 2:'LT', 3:'GTE', 4:'LTE', 5:'EQ' };
    const OP_SYM  = { 'NONE':'--', 'GT':'>', 'LT':'<', 'GTE':'≥', 'LTE':'≤', 'EQ':'=' };

    function str(key, fallback = '') {
      return currentState && currentState[key] ? currentState[key] : fallback;
    }

    function tempOfflineThresh() {
      return currentState?.tempOfflineThresh ?? -900;
    }

    function pressureOfflineThresh() {
      return currentState?.pressureOfflineThresh ?? -900;
    }

    function flowOfflineThresh() {
      return currentState?.flowOfflineThresh ?? -900;
    }

    // Look up a sensor label from the ruleSensors catalog by its RuleSensorId.
    // Used for non-DS18B20 sensors (flow, pressure, level) whose labels are not
    // in the sensorNameN series.  Falls back to the provided string so the UI
    // degrades gracefully before the first /state fetch.
    // Future sensors become available automatically once enabled in g_ruleSensors
    // on the device — no JS change required.
    function ruleLabel(id, fallback) {
      const s = currentState?.ruleSensors?.find(s => s.id === id);
      return (s && s.label) ? s.label : fallback;
    }

    function isPressureSensorId(id) {
        const s = currentState?.ruleSensors?.find(s => s.id === id);
        return !!(s && s.kind === 2);
    }

    function sendCommand(cmd) {
      const form = new FormData();
      form.append('plain', cmd);
      fetch('/', { method: 'POST', body: form })
        .then(() => setTimeout(fetchState, 250))
        .catch(err => console.error('Command error:', err));
    }

    async function fetchState() {
      try {
        const res = await fetch('/state');
        currentState = await res.json();
        renderAll();
      } catch (e) {
        console.error('State fetch failed:', e);
      }
    }

    function releaseMPwrLock() {
      mPwrPending = false;
      clearTimeout(mPwrTimer);
      mPwrTimer = null;
    }

    function onMasterSliderChange(value) {
      mPwrPendingVal = parseInt(value, 10);
      mPwrPending    = true;
      clearTimeout(mPwrTimer);
      // Sync the OTHER slider immediately so both stay in visual agreement
      const ctrlSlider = document.getElementById('master-slider');
      const ctrlPct    = document.getElementById('master-pct');
      const monSlider  = document.getElementById('mon-load-slider');
      const monPct     = document.getElementById('mon-load-pct');
      if (ctrlSlider) ctrlSlider.value   = mPwrPendingVal;
      if (ctrlPct)   ctrlPct.textContent = mPwrPendingVal + '%';
      if (monSlider) monSlider.value     = mPwrPendingVal;
      if (monPct)   monPct.textContent   = mPwrPendingVal + '%';
    }

    function setupMasterSlider() {
      const slider = document.getElementById('master-slider');
      const pct    = document.getElementById('master-pct');
      if (!slider) return;
      // oninput: visual update + lock while dragging
      slider.oninput = () => {
        pct.textContent = slider.value + '%';
        mPwrPendingVal  = parseInt(slider.value, 10);
        mPwrPending     = true;
        clearTimeout(mPwrTimer);
      };
      // onchange: fires on release — send command and arm 3s fallback timer
      slider.onchange = () => {
        onMasterSliderChange(slider.value);
        sendCommand('MASTER:' + slider.value);
        mPwrTimer = setTimeout(releaseMPwrLock, 3000);
      };
    }

    function setupMonSlider() {
      const slider = document.getElementById('mon-load-slider');
      const pct    = document.getElementById('mon-load-pct');
      if (!slider) return;
      slider.oninput = () => {
        pct.textContent = slider.value + '%';
        mPwrPendingVal  = parseInt(slider.value, 10);
        mPwrPending     = true;
        clearTimeout(mPwrTimer);
      };
      slider.onchange = () => {
        onMasterSliderChange(slider.value);
        sendCommand('MASTER:' + slider.value);
        mPwrTimer = setTimeout(releaseMPwrLock, 3000);
      };
    }

    function applySharedStrings() {
      if (!currentState) return;
      document.getElementById('app-title').textContent = str('appTitle', 'Cask & Crown');
      document.getElementById('app-subtitle').textContent = str('appSubtitle', 'Winery Controller');
      document.getElementById('mode-dist-title').textContent = str('procDist', 'DISTILLATION');
      document.getElementById('mode-rect-title').textContent = str('procRect', 'RECTIFICATION');
      document.getElementById('power-label-control').textContent = str('powerLabel', 'Power');
      document.getElementById('power-label-monitor').textContent = str('powerLabel', 'Power');
      document.getElementById('power-help-text').textContent = str('powerHelpText', 'All active SSRs receive this duty cycle simultaneously.');
      document.getElementById('limits-danger-title').textContent = str('limitsDangerTitle', 'Sensor Max Thresholds Danger');
      document.getElementById('valves-title').textContent = str('titleValves', 'Valves Control');
      document.getElementById('btn-start').textContent = str('btnStart', 'START');
      document.getElementById('btn-stop').textContent = str('btnStop', 'STOP');
      document.getElementById('btn-back').textContent = str('btnBack', 'BACK');
      document.getElementById('btn-save').textContent = str('btnSave', 'SAVE');
      document.getElementById('btn-cancel').textContent = str('btnCancel', 'Cancel');
      document.getElementById('wifi-title').textContent = str('titleWifiSetup', 'WiFi Setup');
      document.getElementById('wifi-ssid-label').textContent = str('wifiSsidLabel', 'SSID');
      document.getElementById('wifi-pass-label').textContent = str('wifiPassLabel', 'PASS');
    }

    function showWifiModal() {
      const ssid = currentState?.ssid || '';
      document.getElementById('wifi-ssid').value = ssid;
      document.getElementById('wifi-pass').value = '';
      document.getElementById('wifi-modal').classList.remove('hidden');
    }

    function hideWifiModal() {
      document.getElementById('wifi-modal').classList.add('hidden');
    }

    function saveWifiConfig() {
      const ssid = document.getElementById('wifi-ssid').value.trim();
      const pass = document.getElementById('wifi-pass').value.trim();
      if (!ssid) {
        alert(str('wifiEmptySsid', 'SSID cannot be empty'));
        return;
      }
      sendCommand(`WIFI:SET:${encodeURIComponent(ssid)}:${encodeURIComponent(pass)}`);
      hideWifiModal();
      setTimeout(() => alert(str('wifiSavedMsg', 'WiFi settings saved! Device is reconnecting...')), 400);
    }

    function showValveScreen() {
      document.getElementById('screen-0').classList.add('hidden');
      document.getElementById('screen-1').classList.add('hidden');
      document.getElementById('screen-2').classList.add('hidden');
      document.getElementById('screen-3').classList.remove('hidden');
      renderValveScreen();
    }

    function backFromValveScreen() {
      if (!currentState) {
        document.getElementById('screen-3').classList.add('hidden');
        document.getElementById('screen-0').classList.remove('hidden');
        return;
      }
      const pm = currentState.processMode || 0;
      const running = !!currentState.isRunning;
      document.getElementById('screen-3').classList.add('hidden');
      if (pm === 0) document.getElementById('screen-0').classList.remove('hidden');
      else if (!running) document.getElementById('screen-1').classList.remove('hidden');
      else document.getElementById('screen-2').classList.remove('hidden');
    }

    function buildSensorOptions(selectedId) {
      if (!currentState?.ruleSensors) return '<option value="0">--</option>';
      let h = '<option value="0">--</option>';
      for (const s of currentState.ruleSensors) {
        if (!s.enabled) continue;
        const sel = s.id === selectedId ? ' selected' : '';
        const u = s.unit ? ` (${s.unit})` : '';
        h += `<option value="${s.id}"${sel}>${s.label}${u}</option>`;
      }
      return h;
    }

    function buildOpOptions(selectedOpNum) {
      const sel = OP_CMD[selectedOpNum] || 'NONE';
      return ['NONE','GT','LT','GTE','LTE','EQ'].map(op =>
        `<option value="${op}"${op === sel ? ' selected' : ''}>${OP_SYM[op]}</option>`
      ).join('');
    }

    function renderValveScreen() {
      if (!currentState) return;
      const container = document.getElementById('valve-rules-container');
      if (!container) return;
      const names = currentState.valveNames || Array.from({length: 5}, (_, i) => `Valve ${i + 1}`);
      const rules = currentState.valveRules || [];
      const opens = currentState.valveOpen  || [];

      const condRow = (i, type, cond) => {
          const isPress  = isPressureSensorId(cond.sensorId);
          const dispVal  = isPress
              ? (cond.value * 100).toFixed(1)
              : cond.value.toFixed(1);
          const unitHint = isPress ? ' kPa' : '';
          return `
          <div class="flex items-center gap-2 flex-wrap">
              <span class="text-gray-400 w-24 shrink-0 text-xs">${type === 'open' ? 'Open when' : 'Close when'}:</span>
              <select id="v${i}-${type}-sensor"
                  class="bg-[#111] border border-[#333] rounded-xl px-2 py-1.5 text-xs text-gray-100 focus:outline-none focus:border-orange-400">
                  ${buildSensorOptions(cond.sensorId)}
              </select>
              <select id="v${i}-${type}-op"
                  class="bg-[#111] border border-[#333] rounded-xl px-2 py-1.5 text-xs text-gray-100 w-14 focus:outline-none focus:border-orange-400">
                  ${buildOpOptions(cond.op)}
              </select>
              <input type="number" step="0.1" id="v${i}-${type}-val"
                  class="bg-[#111] border border-[#333] rounded-xl px-2 py-1.5 text-xs text-gray-100 w-20 focus:outline-none focus:border-orange-400"
                  value="${dispVal}">
              <span class="text-xs text-gray-400 w-6">${unitHint}</span>
              <button onclick="applyValveCondition(${i},'${type}')"
                  class="px-3 py-1.5 bg-orange-500 hover:bg-orange-400 rounded-xl text-xs font-medium text-white">SET</button>
          </div>`;
      };

      container.innerHTML = names.map((name, i) => {
        const rule = rules[i] || { openWhen: {sensorId:0, op:0, value:0}, closeWhen: {sensorId:0, op:0, value:0} };
        const isOpen = opens[i] || false;
        const badgeCls = isOpen
          ? 'px-2 py-0.5 text-xs bg-green-600 text-white rounded-full font-medium'
          : 'px-2 py-0.5 text-xs bg-gray-600 text-white rounded-full font-medium';
        const badge = `<span id="v${i}-badge" class="${badgeCls}">${isOpen ? 'OPEN' : 'CLOSED'}</span>`;
        return `
          <div class="bg-[#181818] rounded-2xl px-5 py-4 space-y-3">
            <div class="flex items-center justify-between">
              <span class="font-medium text-gray-100">${name}</span>
              ${badge}
            </div>
            ${condRow(i, 'open',  rule.openWhen)}
            ${condRow(i, 'close', rule.closeWhen)}
          </div>`;
      }).join('');
    }

    function applyValveCondition(valveIdx, type) {
        const sensorId = parseInt(document.getElementById(`v${valveIdx}-${type}-sensor`).value, 10);
        const op       = document.getElementById(`v${valveIdx}-${type}-op`).value;
        let   val      = parseFloat(document.getElementById(`v${valveIdx}-${type}-val`).value);
        if (isNaN(val)) { alert('Invalid threshold value'); return; }
        // Input is in kPa for pressure sensors — convert back to bar for storage
        if (isPressureSensorId(sensorId)) val = val / 100;
        const cmdType = type === 'open' ? 'OPENCFG' : 'CLOSECFG';
        sendCommand(`VALVE:${valveIdx}:${cmdType}:${sensorId}:${op}:${val.toFixed(4)}`);
    }

    // Lightweight badge-only update – called on every poll when screen-3 is visible.
    // Updates only the OPEN/CLOSED spans by stable ID, leaving all form controls intact.
    function updateValveBadges() {
      if (!currentState) return;
      const opens = currentState.valveOpen || [];
      opens.forEach((isOpen, i) => {
        const el = document.getElementById(`v${i}-badge`);
        if (!el) return;
        el.textContent = isOpen ? 'OPEN' : 'CLOSED';
        el.className = isOpen
          ? 'px-2 py-0.5 text-xs bg-green-600 text-white rounded-full font-medium'
          : 'px-2 py-0.5 text-xs bg-gray-600 text-white rounded-full font-medium';
      });
    }

    function renderLimits() {
      const container = document.getElementById('limits-container');
      if (!container || !currentState) return;
      container.innerHTML = '';
      const pm = currentState.processMode || 0;
      if (pm === 0) return;

      const thr = pm === 2 ? currentState.threshRect : currentState.threshDist;
      const addRow = (label, value, unit, onClick) => {
        const row = document.createElement('div');
        row.className = 'flex justify-between items-center bg-[#181818] rounded-2xl px-5 py-3 cursor-pointer hover:bg-[#222]';
        row.innerHTML = `<span class="text-gray-300">${label}</span><span class="font-medium text-orange-400">${value}${unit}</span>`;
        row.onclick = onClick;
        container.appendChild(row);
      };

      addRow(currentState.smaxLabel1 || str('labelPressure', 'Pressure'), (thr.pressDanger * 100).toFixed(1), ` ${str('unitBar', 'kPa')}`, () => {
          const label = currentState.smaxLabel1 || str('labelPressure', 'Pressure');
          const newVal = prompt(`New ${label} danger threshold (kPa):`, (thr.pressDanger * 100).toFixed(1));
          if (newVal === null) return;
          const num = parseFloat(newVal);
          if (isNaN(num) || num < 0 || num > 10) {
              alert(str('promptEnter03Bar', 'Enter 0-10 kPa'));
              return;
          }
          const isRect = pm === 2;
          sendCommand((isRect ? 'THRESH:R:PD:' : 'THRESH:D:PD:') + (num / 100).toFixed(4));
      });

      addRow(currentState.smaxLabel2 || str('sensorName2', 'Kettle'),  thr.tempDanger[1].toFixed(1), str('unitDegC', '°C'), () => editThreshold(2, thr.tempDanger[1], currentState.smaxLabel2 || str('sensorName2', 'Kettle')));
      addRow(currentState.smaxLabel3 || str('sensorName3', 'Pillar 1'), thr.tempDanger[2].toFixed(1), str('unitDegC', '°C'), () => editThreshold(3, thr.tempDanger[2], currentState.smaxLabel3 || str('sensorName3', 'Pillar 1')));
    }

    function editThreshold(sensor, currentVal, label) {
      const newVal = prompt(`${str('promptNewDanger', 'New danger threshold for')} ${label} (${str('unitDegC', '°C')}):`, currentVal.toFixed(1));
      if (newVal === null) return;
      const num = parseFloat(newVal);
      if (isNaN(num) || num < 0 || num > 200) {
        alert(str('promptEnter0200', 'Enter 0-200'));
        return;
      }
      sendCommand(`TMAX:${sensor}:SET:${num.toFixed(1)}`);
    }

    function renderMonitorValues() {
      if (!currentState) return;
      const thr    = currentState.processMode === 2 ? currentState.threshRect : currentState.threshDist;
      const tempOff = tempOfflineThresh();
      const pressOff = pressureOfflineThresh();
      const flowOff  = flowOfflineThresh();
      const unitDegC = str('unitDegC', '°C');
      const unitBar  = str('unitBar', 'bar');
      const unitLpm  = str('unitLpm', 'L/min');
      const unitL    = str('unitLiters', 'L');
      const offline  = str('labelOffline', 'Offline');

      // Build a sensor row HTML string.
      // cls: colour class applied to the value span.
      const row = (label, valHtml, cls = '') =>
        `<div class="flex justify-between items-center">
           <span class="text-gray-400">${label}</span>
           <span class="${cls}">${valHtml}</span>
         </div>`;

      // Temperature row helper – returns '' (hidden) for extended sensors that are offline.
      const tempRow = (label, val, warn, danger, alwaysShow) => {
        if (val == null || isNaN(val) || val <= tempOff) {
          if (!alwaysShow) return '';
          return row(label, offline, 'sensor-offline');
        }
        const cls = val >= danger ? 'sensor-danger' : val >= warn ? 'sensor-warn' : '';
        return row(label, `${val.toFixed(1)}${unitDegC}`, cls);
      };

      // Flow row helper – returns '' for extended sensors that are offline.
      const flowRow = (label, val, alwaysShow) => {
        if (val == null || isNaN(val) || val <= flowOff) {
          if (!alwaysShow) return '';
          return row(label, offline, 'sensor-offline');
        }
        return row(label, `${val.toFixed(2)} ${unitLpm}`);
      };

      // -----------------------------------------------------------------------
      // Collect all rows. The grid is 2-column so rows alternate left/right
      // automatically via CSS grid auto-placement.
      // -----------------------------------------------------------------------
      const rows = [];

      // Core temperature sensors – always shown (alwaysShow = true)
      rows.push(tempRow(str('sensorName1', 'Room'),    currentState.roomTemp,    thr.tempWarn[0], thr.tempDanger[0], true));
      rows.push(tempRow(str('sensorName2', 'Kettle'),  currentState.kettleTemp,  thr.tempWarn[1], thr.tempDanger[1], true));
      rows.push(tempRow(str('sensorName3', 'Pillar 1'),currentState.pillar1Temp, thr.tempWarn[2], thr.tempDanger[2], true));

      // Extended temperature sensors – appear only when online
      // thr.tempWarn/Danger for extended slots fall back to safetyTempMaxC (no per-sensor threshold configured)
      const extTempOff  = currentState.safetyTempMaxC  || 95;
      const extTempWarn = extTempOff * 0.92;   // 92% of limit as visual warn
      rows.push(tempRow(str('sensorName4', 'Pillar 2'),     currentState.pillar2Temp,  extTempWarn, extTempOff, false));
      rows.push(tempRow(str('sensorName5', 'Pillar 3'),     currentState.pillar3Temp,  extTempWarn, extTempOff, false));
      rows.push(tempRow(str('sensorName6', 'Dephlegmator'), currentState.dephlegmTemp, extTempWarn, extTempOff, false));
      rows.push(tempRow(str('sensorName7', 'Reflux Cond.'), currentState.refluxTemp,   extTempWarn, extTempOff, false));
      rows.push(tempRow(str('sensorName8', 'Product Cooler'),currentState.productTemp, extTempWarn, extTempOff, false));

      // Total volume – rendered in the header bar, not the sensor grid

      // Pressure – always shown
      if (currentState.pressureBar <= pressOff) {
        rows.push(row(str('labelPressure', 'Pressure'), offline, 'sensor-offline'));
      } else {
        const pCls = currentState.pressureBar >= thr.pressDanger ? 'sensor-danger'
                   : currentState.pressureBar >= thr.pressWarn   ? 'sensor-warn' : '';
        rows.push(row(str('labelPressure', 'Pressure'), `${(currentState.pressureBar * 100).toFixed(1)} ${unitBar}`, pCls));
      }

      // Level – always shown
      const lvlOk = currentState.levelHigh;
      rows.push(row(str('labelLevel', 'Level'),
        lvlOk ? str('labelLevelOk', 'Level OK') : str('labelLevelLow', 'Level LOW'),
        lvlOk ? 'text-green-400' : 'text-red-400'));

      // Product flow – always shown
      rows.push(flowRow(str('labelFlow', 'Flow'), currentState.flowRateLPM, true));

      // Extended flow sensors – appear only when online.
      // Labels come from the ruleSensors catalog (already in /state) so new
      // sensors become visible automatically when enabled on the device.
      // RuleSensorId: Water Dephl = 14, Water Cond = 15, Water Cooler = 16.
      rows.push(flowRow(ruleLabel(14, 'Water Dephl.'), currentState.waterDephlLpm,  false));
      rows.push(flowRow(ruleLabel(15, 'Water Cond.'),  currentState.waterCondLpm,   false));
      rows.push(flowRow(ruleLabel(16, 'Water Cooler'), currentState.waterCoolerLpm, false));

      // Render into the 2-column grid; empty strings are filtered out
      const container = document.getElementById('mon-sensors');
      if (container) container.innerHTML = rows.filter(r => r !== '').join('');

      // Slider — only sync from server when no user interaction is pending
      if (!mPwrPending) {
        const monSlider = document.getElementById('mon-load-slider');
        const monPct    = document.getElementById('mon-load-pct');
        if (monSlider && monPct) {
          const pwr = Math.round(currentState.masterPower || 0);
          if (parseInt(monSlider.value, 10) !== pwr) monSlider.value = pwr;
          monPct.textContent = pwr + '%';
        }
      }
    }

    function renderAll() {
      if (!currentState) return;

      applySharedStrings();

      const st = document.getElementById('hdr-status');
      if (currentState.safetyTripped) {
        st.innerHTML = `<span class="px-3 py-1 bg-red-600 text-white text-xs rounded-full font-medium">${str('statusSafety', 'SAFETY TRIP')}</span>`;
      } else if (currentState.isRunning) {
        st.innerHTML = `<span class="px-3 py-1 bg-green-600 text-white text-xs rounded-full font-medium">${str('statusRunning', 'RUNNING')}</span>`;
      } else {
        st.innerHTML = `<span class="px-3 py-1 bg-gray-600 text-white text-xs rounded-full font-medium">${str('statusStopped', 'STOPPED')}</span>`;
      }

      document.getElementById('hdr-t1').innerHTML =
        currentState.roomTemp > tempOfflineThresh()
          ? `${str('sensorName1', 'Room')} <span class="font-medium">${currentState.roomTemp.toFixed(1)}${str('unitDegC', '°C')}</span>`
          : `${str('sensorName1', 'Room')} <span class="sensor-offline">${str('labelOffline', 'Offline')}</span>`;

      let maxTxt = `${str('maxPrefix', 'Max')} --`;
      if (currentState.safetyTripped && currentState.safetyMessage) {
        maxTxt = currentState.safetyMessage;
      } else {
        const allTemps = [
          currentState.roomTemp, currentState.kettleTemp, currentState.pillar1Temp,
          currentState.pillar2Temp, currentState.pillar3Temp,
          currentState.dephlegmTemp, currentState.refluxTemp, currentState.productTemp
        ].filter(v => v != null && v > tempOfflineThresh());
        if (allTemps.length) maxTxt = `${str('maxPrefix', 'Max')} ${Math.max(...allTemps).toFixed(1)}${str('unitDegC', '°C')}`;
      }
      document.getElementById('hdr-max').textContent = maxTxt;
      document.getElementById('hdr-ip').textContent = currentState.ip || '0.0.0.0';
      document.getElementById('fw').textContent = currentState.fw || '1.0.0';

      const totalVol = (currentState.totalVolumeLiters || 0).toFixed(3);
      const hdrTotal = document.getElementById('hdr-total');
      if (hdrTotal) {
        hdrTotal.textContent = currentState.isRunning || currentState.totalVolumeLiters > 0
          ? `${totalVol} ${str('unitLiters', 'L')}`
          : '';
      }

      const title = document.getElementById('ctrl-title');
      title.textContent = currentState.processMode === 1 ? str('titleCtrlDist', 'Distillation Control') : currentState.processMode === 2 ? str('titleCtrlRect', 'Rectification Control') : str('titleCtrl', 'Control');

      const srvPwr = Math.round(currentState.masterPower || 0);

      // Release the pending lock early once the server confirms our value
      if (mPwrPending && srvPwr === mPwrPendingVal) releaseMPwrLock();

      // Only sync sliders from server when no user interaction is pending
      if (!mPwrPending) {
        const slider = document.getElementById('master-slider');
        const pct    = document.getElementById('master-pct');
        if (slider && parseInt(slider.value, 10) !== srvPwr) slider.value = srvPwr;
        if (pct) pct.textContent = srvPwr + '%';
      }

      // ── Screen switching ──────────────────────────────────────────────────
      // Done BEFORE renderMonitorValues/renderLimits so that a render error
      // never prevents the correct screen from showing.
      const modeScreen    = document.getElementById('screen-0');
      const controlScreen = document.getElementById('screen-1');
      const monitorScreen = document.getElementById('screen-2');
      const valveScreen   = document.getElementById('screen-3');

      if (!valveScreen.classList.contains('hidden')) {
        updateValveBadges();
        renderMonitorValues();   // keep monitor data fresh even when valve screen is open
        return;
      }

      if (currentState.processMode === 0) {
        modeScreen.classList.remove('hidden');
        controlScreen.classList.add('hidden');
        monitorScreen.classList.add('hidden');
      } else if (!currentState.isRunning) {
        modeScreen.classList.add('hidden');
        controlScreen.classList.remove('hidden');
        monitorScreen.classList.add('hidden');
      } else {
        modeScreen.classList.add('hidden');
        controlScreen.classList.add('hidden');
        monitorScreen.classList.remove('hidden');
      }

      // While a slider change is pending, use the local value so START can
      // be enabled immediately without waiting for server confirmation.
      const effectivePower = mPwrPending ? mPwrPendingVal : srvPwr;

      const canStart =
        (currentState.processMode === 1 || currentState.processMode === 2) &&
        !currentState.isRunning &&
        !currentState.safetyTripped &&
        effectivePower > 0;

      document.getElementById('btn-start').disabled = !canStart;

      // ── Content rendering (after screen is correct) ───────────────────────
      renderMonitorValues();
      renderLimits();
    }

    // -----------------------------------------------------------------------
    // Sensor Mapper
    // -----------------------------------------------------------------------
    let scannedRoms = [];   // array of ROM hex strings from last bus scan

    function showSensorMapper() {
      document.getElementById('sensor-mapper-modal').classList.remove('hidden');
      renderSensorMapperRows();
    }

    function hideSensorMapper() {
      document.getElementById('sensor-mapper-modal').classList.add('hidden');
    }

    async function scanSensorBus() {
      const btn = document.getElementById('btn-scan');
      const status = document.getElementById('scan-status');
      btn.disabled = true;
      status.textContent = 'Scanning...';
      try {
        const res = await fetch('/api/sensor_scan', { method: 'POST' });
        const data = await res.json();
        scannedRoms = data.roms || [];
        status.textContent = `${data.count} device(s) found`;
        renderSensorMapperRows();
      } catch (e) {
        status.textContent = 'Scan failed';
        console.error('Scan error:', e);
      }
      btn.disabled = false;
    }

    function buildRomOptions(currentRom) {
      // Current ROM is always shown first so the user can see what's assigned
      // even if it wasn't found in the last scan.
      const UNSET = '0000000000000000';
      let html = `<option value="${UNSET}">-- unassigned --</option>`;
      const shown = new Set();

      // Add currently assigned ROM (may not be in scannedRoms if sensor was absent)
      if (currentRom && currentRom !== '' && currentRom !== UNSET) {
        const sel = ' selected';
        html += `<option value="${currentRom}"${sel}>${currentRom}</option>`;
        shown.add(currentRom);
      }

      // Add all scanned ROMs not already shown
      for (const rom of scannedRoms) {
        if (shown.has(rom)) continue;
        const sel = (rom === currentRom) ? ' selected' : '';
        html += `<option value="${rom}"${sel}>${rom}</option>`;
        shown.add(rom);
      }
      return html;
    }

    function renderSensorMapperRows() {
      const container = document.getElementById('sensor-mapper-rows');
      if (!container) return;
      const slotNames = currentState?.tempSensorSlotNames || [];
      const sensorRoms = currentState?.sensorRoms || [];
      const count = Math.max(slotNames.length, 8);

      container.innerHTML = Array.from({ length: count }, (_, i) => {
        const name    = slotNames[i]  || `Slot ${i}`;
        const romHex  = sensorRoms[i] || '';
        const isSet   = romHex !== '';
        const badge   = isSet
          ? `<span class="text-xs text-green-400 font-mono truncate max-w-[9rem]" title="${romHex}">${romHex.slice(0, 8)}…</span>`
          : `<span class="text-xs text-gray-500">unassigned</span>`;
        return `
          <div class="flex items-center gap-3 bg-[#181818] rounded-2xl px-4 py-3">
            <span class="text-sm text-gray-200 w-36 shrink-0">${name}</span>
            ${badge}
            <select id="rom-sel-${i}"
              class="flex-1 bg-[#111] border border-[#333] rounded-xl px-2 py-1.5 text-xs text-gray-100 focus:outline-none focus:border-orange-400 font-mono">
              ${buildRomOptions(romHex)}
            </select>
            <button onclick="applyRomMapping(${i})"
              class="px-3 py-1.5 bg-orange-500 hover:bg-orange-400 rounded-xl text-xs font-medium text-white shrink-0">SET</button>
          </div>`;
      }).join('');
    }

    function applyRomMapping(slotIdx) {
      const sel = document.getElementById(`rom-sel-${slotIdx}`);
      if (!sel) return;
      const romHex = sel.value;
      sendCommand(`SENSOR:MAP:${slotIdx}:${romHex}`);
    }

    function initWebUI() {
      setupMasterSlider();
      setupMonSlider();
      fetchState();
      setInterval(fetchState, REFRESH_MS);
    }

    window.onload = initWebUI;
  </script>
</body>
</html>
)rawliteral";

    server.sendHeader("Cache-Control", "no-store, no-cache");
    server.send(200, "text/html", html);
}

void webUiRegisterHandlers(WebServer& server)
{
    server.on("/", HTTP_GET, [&server]() { handleRoot(server); });
    Serial.println("[WebUI] Embedded web UI ready – Master Power unified + Valves Control");
}

void webUiStartFetchTask()
{
    Serial.println("[WebUI] Embedded UI active (no remote fetch needed)");
}
