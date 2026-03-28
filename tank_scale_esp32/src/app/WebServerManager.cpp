#include "WebServerManager.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>
#include "iot/IotConfig.h"

static constexpr uint16_t DNS_PORT = 53;

namespace {

bool isAllowedReadingMultiplier(float value) {
  static constexpr float kAllowed[] = {0.01f, 0.1f, 1.0f, 10.0f, 100.0f};
  for (float allowed : kAllowed) {
    if (fabsf(value - allowed) < 0.0001f) return true;
  }
  return false;
}

void addTelemetryConfigJson(JsonObject doc, const TankTelemetryConfig& cfg) {
  doc["stateEvalPeriodMs"] = cfg.stateEvalPeriodMs;
  doc["readingMultiplier"] = cfg.readingMultiplier;
  doc["idleRateThreshold"] = cfg.idleRateThreshold;
  doc["fillRateThreshold"] = cfg.fillRateThreshold;
  doc["dischargeRateThreshold"] = cfg.dischargeRateThreshold;
  doc["flowConfirmMs"] = cfg.flowConfirmMs;
  doc["idleConfirmMs"] = cfg.idleConfirmMs;
  doc["flowMinDelta"] = cfg.flowMinDelta;
  doc["cleaningDetectEnabled"] = cfg.cleaningDetectEnabled;
  doc["cleaningWindowMs"] = cfg.cleaningWindowMs;
  doc["cleaningMinSignFlips"] = cfg.cleaningMinSignFlips;
  doc["cleaningMinAbsDelta"] = cfg.cleaningMinAbsDelta;
  doc["cleaningConfirmMs"] = cfg.cleaningConfirmMs;
  doc["telemetryIdleMs"] = cfg.telemetryIdleMs;
  doc["telemetryFillingMs"] = cfg.telemetryFillingMs;
  doc["telemetryDischargingMs"] = cfg.telemetryDischargingMs;
  doc["telemetryCleaningMs"] = cfg.telemetryCleaningMs;
  doc["telemetryUnknownMs"] = cfg.telemetryUnknownMs;
  doc["heartbeatPeriodMs"] = cfg.heartbeatPeriodMs;
  doc["scaleActiveTimeoutMs"] = cfg.scaleActiveTimeoutMs;
}

bool applyTelemetryConfigPatch(JsonObjectConst doc, TankTelemetryConfig& cfg, String& errOut) {
  auto positiveU32 = [&](const char* key, uint32_t& target) -> bool {
    if (!doc.containsKey(key)) return true;
    uint32_t v = doc[key].as<uint32_t>();
    if (v == 0) {
      errOut = String("invalid ") + key;
      return false;
    }
    target = v;
    return true;
  };

  auto nonNegativeFloat = [&](const char* key, float& target) -> bool {
    if (!doc.containsKey(key)) return true;
    float v = doc[key].as<float>();
    if (isnan(v) || v < 0.0f) {
      errOut = String("invalid ") + key;
      return false;
    }
    target = v;
    return true;
  };

  auto boolValue = [&](const char* key, bool& target) -> bool {
    if (!doc.containsKey(key)) return true;
    target = doc[key].as<bool>();
    return true;
  };

  auto u8Positive = [&](const char* key, uint8_t& target) -> bool {
    if (!doc.containsKey(key)) return true;
    uint32_t v = doc[key].as<uint32_t>();
    if (v == 0 || v > 255) {
      errOut = String("invalid ") + key;
      return false;
    }
    target = (uint8_t)v;
    return true;
  };

  if (!positiveU32("stateEvalPeriodMs", cfg.stateEvalPeriodMs)) return false;
  if (doc.containsKey("readingMultiplier")) {
    float v = doc["readingMultiplier"].as<float>();
    if (isnan(v) || !isAllowedReadingMultiplier(v)) {
      errOut = "invalid readingMultiplier";
      return false;
    }
    cfg.readingMultiplier = v;
  }
  if (!nonNegativeFloat("idleRateThreshold", cfg.idleRateThreshold)) return false;
  if (!nonNegativeFloat("fillRateThreshold", cfg.fillRateThreshold)) return false;
  if (!nonNegativeFloat("dischargeRateThreshold", cfg.dischargeRateThreshold)) return false;
  if (!positiveU32("flowConfirmMs", cfg.flowConfirmMs)) return false;
  if (!positiveU32("idleConfirmMs", cfg.idleConfirmMs)) return false;
  if (!nonNegativeFloat("flowMinDelta", cfg.flowMinDelta)) return false;
  if (!boolValue("cleaningDetectEnabled", cfg.cleaningDetectEnabled)) return false;
  if (!positiveU32("cleaningWindowMs", cfg.cleaningWindowMs)) return false;
  if (!u8Positive("cleaningMinSignFlips", cfg.cleaningMinSignFlips)) return false;
  if (!nonNegativeFloat("cleaningMinAbsDelta", cfg.cleaningMinAbsDelta)) return false;
  if (!positiveU32("cleaningConfirmMs", cfg.cleaningConfirmMs)) return false;
  if (!positiveU32("telemetryIdleMs", cfg.telemetryIdleMs)) return false;
  if (!positiveU32("telemetryFillingMs", cfg.telemetryFillingMs)) return false;
  if (!positiveU32("telemetryDischargingMs", cfg.telemetryDischargingMs)) return false;
  if (!positiveU32("telemetryCleaningMs", cfg.telemetryCleaningMs)) return false;
  if (!positiveU32("telemetryUnknownMs", cfg.telemetryUnknownMs)) return false;
  if (!positiveU32("heartbeatPeriodMs", cfg.heartbeatPeriodMs)) return false;
  if (!positiveU32("scaleActiveTimeoutMs", cfg.scaleActiveTimeoutMs)) return false;

  return true;
}

} // namespace

WebServerManager::~WebServerManager() {
  if (server_) {
    server_->stop();
    server_->close();
    delete server_;
    server_ = nullptr;
  }
}

void WebServerManager::beginApPortal(WifiManager& wifi, AppState& state) {
  wifi_ = &wifi;
  state_ = &state;
  apPortal_ = true;

  // DNS hijack for captive portal feel
  dns_.start(DNS_PORT, "*", WiFi.softAPIP());

  resetServer_();
  setupRoutesAp_();
  server_->begin();
}

void WebServerManager::beginStaServer(WifiManager& wifi, AppState& state) {
  wifi_ = &wifi;
  state_ = &state;
  apPortal_ = false;

  dns_.stop();

  resetServer_();
  setupRoutesSta_();
  server_->begin();
}

void WebServerManager::loop() {
  if (apPortal_) dns_.processNextRequest();
  if (server_) server_->handleClient();
}

void WebServerManager::markCommRx_() {
  if (state_) state_->commRxMs = millis();
}

void WebServerManager::markCommTx_() {
  if (state_) state_->commTxMs = millis();
}

void WebServerManager::resetServer_() {
  if (server_) {
    server_->stop();
    server_->close();
    delete server_;
    server_ = nullptr;
  }
  server_ = new WebServer(80);
}

// -------------------- Captive portal helpers --------------------

void WebServerManager::sendCaptiveRedirect_() {
  markCommRx_();
  // Force OS captive probes to land on our setup page
  String host = "http://" + WiFi.softAPIP().toString() + "/";

  server_->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server_->sendHeader("Pragma", "no-cache");
  server_->sendHeader("Expires", "-1");
  server_->sendHeader("Location", host, true);
  markCommTx_();
  server_->send(302, "text/plain", "");
}

void WebServerManager::addCaptivePortalRoutes_() {
  // Android
  server_->on("/generate_204", HTTP_ANY, [&]() { sendCaptiveRedirect_(); });
  server_->on("/gen_204", HTTP_ANY, [&]() { sendCaptiveRedirect_(); });

  // iOS / macOS
  server_->on("/hotspot-detect.html", HTTP_ANY, [&]() { sendCaptiveRedirect_(); });
  server_->on("/library/test/success.html", HTTP_ANY, [&]() { sendCaptiveRedirect_(); });

  // Windows
  server_->on("/ncsi.txt", HTTP_ANY, [&]() { sendCaptiveRedirect_(); });
  server_->on("/connecttest.txt", HTTP_ANY, [&]() { sendCaptiveRedirect_(); });
  server_->on("/redirect", HTTP_ANY, [&]() { sendCaptiveRedirect_(); });
  server_->on("/fwlink", HTTP_ANY, [&]() { sendCaptiveRedirect_(); });
}

// -------------------- File serving --------------------

void WebServerManager::serveFile_(const char* path, const char* contentType) {
  if (!LittleFS.begin(false)) {
    // If you already mount in App::begin(), this will succeed anyway.
    // If not mounted, we try here without formatting.
  }

  File f = LittleFS.open(path, "r");
  if (!f) {
    server_->send(404, "text/plain", "Not found");
    return;
  }
  server_->sendHeader("Cache-Control", "no-cache");
  markCommTx_();
  server_->streamFile(f, contentType);
  f.close();
}

// -------------------- AP setup routes --------------------

void WebServerManager::setupRoutesAp_() {
  addCaptivePortalRoutes_();

  // External CSS
  server_->on("/style.css", HTTP_GET, [&]() {
    serveFile_("/style.css", "text/css");
  });

  // Root setup page
  server_->on("/", HTTP_GET, [&]() {
    server_->send(200, "text/html", htmlPortal_());
  });

  // Start scan (async)
  server_->on("/api/scan/start", HTTP_POST, [&]() {
    wifi_->startScanAsync();
    server_->send(200, "application/json", "{\"ok\":true}");
  });

  // Scan status + results
  server_->on("/api/scan", HTTP_GET, [&]() {
    StaticJsonDocument<256> meta;
    //JsonDocument doc;
    meta["running"] = wifi_->isScanRunning();
    meta["count"] = wifi_->scanCount();

    String networks = wifi_->hasScanResults() ? wifi_->scanResultsJson() : "[]";

    String out;
    out.reserve(256 + networks.length());
    serializeJson(meta, out);
    out.remove(out.length() - 1); // remove trailing }
    out += ",\"networks\":";
    out += networks;
    out += "}";

    server_->send(200, "application/json", out);
  });

  // Save credentials
  server_->on("/save", HTTP_POST, [&]() {
    String ssid = server_->arg("ssid");
    String pass = server_->arg("pass");

    if (!ssid.length()) {
      server_->send(400, "text/plain", "Missing SSID");
      return;
    }

    wifi_->saveCredentialsAndConnect(ssid, pass);

    server_->send(200, "text/html",
      "<!doctype html><html><body>"
      "<h3>Saved.</h3><p>Rebooting...</p>"
      "</body></html>");

    delay(250);
    ESP.restart();
  });

  // Anything else: redirect to portal
  server_->onNotFound([&]() {
    sendCaptiveRedirect_();
  });
}

// -------------------- STA normal routes --------------------

void WebServerManager::setupRoutesSta_() {
  server_->on("/", HTTP_GET, [&]() {
    markCommRx_();
    markCommTx_();
    server_->send(200, "text/html", htmlHome_());
  });

  server_->on("/api/status", HTTP_GET, [&]() {
    markCommRx_();
    JsonDocument doc;
    doc["mode"] = "sta";
    doc["connected"] = wifi_->isConnected();
    doc["ip"] = wifi_->ipString();
    doc["hostname"] = wifi_->hostname();
    doc["ssid"] = wifi_->savedSsid();
    doc["deviceId"] = deviceId();

    // ---- time ----
    time_t now = time(nullptr);
    struct tm tm{};
    localtime_r(&now, &tm);

    bool timeValid = (tm.tm_year >= (2020 - 1900));
    doc["time_valid"] = timeValid;

    if (timeValid) {
        char buf[24];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        doc["time"] = buf;
    } else {
        doc["time"] = "syncing...";
    }

    String out;
    serializeJson(doc, out);
    markCommTx_();
    server_->send(200, "application/json", out);
  });

  server_->on("/api/settings", HTTP_GET, [&]() {
    markCommRx_();
    JsonDocument doc;
    if (!telemetryCfg_) {
      markCommTx_();
      server_->send(500, "application/json", "{\"error\":\"telemetry config not attached\"}");
      return;
    }

    JsonObject current = doc["current"].to<JsonObject>();
    addTelemetryConfigJson(current, *telemetryCfg_);
    JsonObject defaults = doc["defaults"].to<JsonObject>();
    TankTelemetryConfig dflt;
    addTelemetryConfigJson(defaults, dflt);

    String out;
    serializeJson(doc, out);
    markCommTx_();
    server_->send(200, "application/json", out);
  });

  server_->on("/api/settings", HTTP_POST, [&]() {
    markCommRx_();
    if (!server_->hasArg("plain")) {
      markCommTx_();
      server_->send(400, "application/json", "{\"error\":\"missing body\"}");
      return;
    }

    // StaticJsonDocument<256> doc;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server_->arg("plain"));
    if (err) {
      markCommTx_();
      server_->send(400, "application/json", "{\"error\":\"bad json\"}");
      return;
    }

    if (!telemetryCfg_) {
      markCommTx_();
      server_->send(500, "application/json", "{\"error\":\"telemetry config not attached\"}");
      return;
    }

    TankTelemetryConfig next = *telemetryCfg_;
    String errMsg;
    if (!applyTelemetryConfigPatch(doc.as<JsonObjectConst>(), next, errMsg)) {
      String out = String("{\"error\":\"") + errMsg + "\"}";
      markCommTx_();
      server_->send(400, "application/json", out);
      return;
    }
    *telemetryCfg_ = next;

    markCommTx_();
    server_->send(200, "application/json", "{\"ok\":true}");
  });

  server_->on("/api/settings/reset", HTTP_POST, [&]() {
    markCommRx_();
    if (!telemetryCfg_) {
      markCommTx_();
      server_->send(500, "application/json", "{\"error\":\"telemetry config not attached\"}");
      return;
    }
    *telemetryCfg_ = TankTelemetryConfig{};
    markCommTx_();
    server_->send(200, "application/json", "{\"ok\":true}");
  });

  server_->on("/api/clear_wifi", HTTP_POST, [&]() {
    markCommRx_();
    state_->requestClearWifi = true;
    markCommTx_();
    server_->send(200, "application/json", "{\"ok\":true}");
  });

  server_->onNotFound([&]() {
    server_->send(404, "text/plain", "Not found");
  });
}

// -------------------- Pages --------------------

String WebServerManager::htmlPortal_() const {
  return R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"/>
  <title>ESP32 Setup</title>
  <link rel="stylesheet" href="/style.css">
</head>

<body>
  <div class="container">
    <header class="header">
      <h1>ESP32 Wi-Fi Setup</h1>
      <p>Connect this device to your network</p>
    </header>

    <main class="card">
      <div class="row">
        <button id="refreshBtn" class="btn btn-secondary" type="button" onclick="startScan()">Refresh</button>
        <span id="scanStatus" class="status">Scanning…</span>
      </div>

      <label for="ssidSelect">Available networks</label>
      <select id="ssidSelect" onchange="pickSsid()">
        <option value="">Scanning…</option>
      </select>

      <form method="POST" action="/save">
        <label for="ssidInput">Wi-Fi name (SSID)</label>
        <input id="ssidInput" name="ssid" placeholder="Select or enter network" required />

        <label for="passInput">Password</label>
        <div class="pw">
          <input id="passInput" name="pass" placeholder="Wi-Fi password" type="password" />
          <button class="btn btn-ghost" type="button" onclick="togglePw()">Show</button>
        </div>

        <button class="btn btn-primary" type="submit">Save & Restart</button>
      </form>
    </main>

    <footer class="footer">
      After setup, open <b>http://esp32-xxxx.local</b> or check the OLED.
    </footer>
  </div>

<script>
  let scanTimer = null;

  function pickSsid(){
    const v = document.getElementById('ssidSelect').value;
    if(v) document.getElementById('ssidInput').value = v;
  }

  function togglePw(){
    const inp = document.getElementById('passInput');
    const btn = inp.parentElement.querySelector('button');
    const isPw = inp.type === 'password';
    inp.type = isPw ? 'text' : 'password';
    btn.textContent = isPw ? 'Hide' : 'Show';
  }

  function setScanningUI(scanning){
    const btn = document.getElementById('refreshBtn');
    btn.disabled = scanning;
    btn.style.opacity = scanning ? '0.6' : '1.0';
    btn.style.cursor = scanning ? 'not-allowed' : 'pointer';
  }

  // RSSI -> icon (no dBm shown)
  function rssiIcon(rssi){
  // RSSI is negative; closer to 0 = stronger
  if (rssi >= -55) return '▂▄▆█';   // excellent
  if (rssi >= -65) return '▂▄▆';    // good
  if (rssi >= -75) return '▂▄';     // fair
  return '▂';                       // weak
}

  async function startScan(){
    setScanningUI(true);
    document.getElementById('scanStatus').textContent = 'Scanning…';

    // start scan
    await fetch('/api/scan/start', {method:'POST'});

    if(scanTimer) clearInterval(scanTimer);
    scanTimer = setInterval(pollScan, 700);
    pollScan();
  }

  async function pollScan(){
    const r = await fetch('/api/scan');
    const j = await r.json();

    if(j.running){
      document.getElementById('scanStatus').textContent = 'Scanning…';
      return;
    }

    clearInterval(scanTimer);
    scanTimer = null;
    setScanningUI(false);

    const sel = document.getElementById('ssidSelect');
    sel.innerHTML = '';

    const networks = (j.networks || []);
    const opt0 = document.createElement('option');
    opt0.value = '';
    opt0.textContent = networks.length ? '-- select network --' : '-- no networks found --';
    sel.appendChild(opt0);

    networks.forEach(n => {
      const o = document.createElement('option');
      o.value = n.ssid;

      const lock = n.open ? '' : ' 🔒';
      const sig  = rssiIcon(n.rssi);

      // No (xx dBm) anymore — icon only
      o.textContent = `${sig}  ${n.ssid}${lock}`;
      sel.appendChild(o);
    });

    document.getElementById('scanStatus').textContent =
      networks.length ? `Found ${networks.length} networks` : 'No networks found';
  }

  // Auto-scan on load
  startScan();
</script>
</body>
</html>
)HTML";
}
/*
String WebServerManager::htmlHome_() const {
  return R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width,initial-scale=1"/>
  <title>ESP32 Status</title>
  <style>
    body{font-family:system-ui;margin:24px;max-width:720px}
    pre{background:#f6f6f6;padding:12px;border-radius:12px;overflow:auto}
    button{padding:10px 14px;margin-right:8px}
  </style>
</head>
<body>
  <h2>ESP32 Status</h2>

  <button onclick="refresh()">Refresh</button>
  <button onclick="clearWifi()">Clear Wi-Fi (reboot)</button>

  <h3>/api/status</h3>
  <pre id="out">Loading...</pre>

  <script>
    async function refresh(){
      const r = await fetch('/api/status');
      document.getElementById('out').textContent = JSON.stringify(await r.json(), null, 2);
    }
    async function clearWifi(){
      if(!confirm('Clear Wi-Fi credentials and reboot?')) return;
      await fetch('/api/clear_wifi', {method:'POST'});
      alert('OK. Rebooting...');
    }
    refresh();
  </script>
</body>
</html>
)HTML";
}
*/
String WebServerManager::htmlHome_() const {
  return R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"/>
  <title>Device Settings</title>
  <style>
    :root{
      --bg:#f3f7fb;
      --panel:#ffffff;
      --text:#0f172a;
      --muted:#64748b;
      --line:#dbe4ef;
      --accent:#0f766e;
      --accent2:#115e59;
      --danger:#b42318;
      --danger-bg:#fef3f2;
      --shadow:0 18px 40px rgba(15,23,42,.08);
      --radius:16px;
    }
    *{box-sizing:border-box}
    body{
      margin:0;
      color:var(--text);
      font-family:ui-sans-serif,system-ui,-apple-system,Segoe UI,Roboto,sans-serif;
      background:
        radial-gradient(900px 380px at 10% 0%, #dff7f3, transparent 55%),
        radial-gradient(900px 420px at 90% 0%, #eef7ff, transparent 60%),
        var(--bg);
    }
    .wrap{max-width:980px;margin:0 auto;padding:18px 14px 28px}
    .hero{display:flex;justify-content:space-between;gap:12px;align-items:flex-start;margin-bottom:14px}
    .hero h1{margin:0;font-size:1.35rem;letter-spacing:-.02em}
    .hero p{margin:6px 0 0;color:var(--muted);font-size:.95rem}
    .grid{display:grid;grid-template-columns:1fr;gap:14px}
    .status-card{order:1}
    .settings-card{order:2}
    .card{
      background:var(--panel);
      border:1px solid rgba(219,228,239,.95);
      border-radius:var(--radius);
      box-shadow:var(--shadow);
      padding:14px;
    }
    .card h2{margin:0 0 12px;font-size:1rem}
    .meta{
      display:grid;
      grid-template-columns:repeat(2,minmax(0,1fr));
      gap:10px;
      margin-bottom:10px;
    }
    .pill{
      border:1px solid var(--line);
      border-radius:14px;
      background:#fbfdff;
      padding:10px 12px;
      min-height:58px;
    }
    .pill .k{display:block;color:var(--muted);font-size:.78rem;margin-bottom:4px}
    .pill .v{font-weight:650;word-break:break-word}
    .copyline{display:flex;align-items:center;gap:8px}
    .copyline .v{flex:1;min-width:0}
    .mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:.88rem}
    .status-dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px;background:#94a3b8}
    .ok{background:#16a34a}
    .warn{background:#f59e0b}
    .row{display:flex;gap:10px;flex-wrap:wrap}
    .btn{
      border:1px solid var(--line);
      background:#fff;
      color:var(--text);
      border-radius:12px;
      padding:10px 12px;
      cursor:pointer;
      font-weight:600;
    }
    .btn.icon{
      padding:8px 10px;
      min-width:38px;
      line-height:1;
      font-size:1rem;
    }
    .btn.primary{
      border-color:#0f766e22;
      color:#fff;
      background:linear-gradient(180deg,var(--accent),var(--accent2));
      box-shadow:0 10px 18px rgba(15,118,110,.2);
    }
    .btn.danger{
      color:var(--danger);
      background:var(--danger-bg);
      border-color:#fecaca;
    }
    .btn:disabled{opacity:.55;cursor:not-allowed}
    .muted{color:var(--muted);font-size:.88rem}
    form .section{
      border-top:1px solid #eef2f7;
      padding-top:12px;
      margin-top:12px;
    }
    form .section:first-of-type{border-top:0;margin-top:0;padding-top:0}
    .section h3{margin:0 0 8px;font-size:.95rem}
    .fields{
      display:grid;
      grid-template-columns:repeat(2,minmax(0,1fr));
      gap:10px;
    }
    label{
      display:block;
      font-size:.8rem;
      color:var(--muted);
      margin:0 0 6px;
    }
    .field{
      border:1px solid #edf2f7;
      background:#fbfdff;
      border-radius:12px;
      padding:10px;
    }
    .input-wrap{display:flex;align-items:center;gap:8px}
    .toggle{
      display:flex;align-items:center;justify-content:space-between;gap:10px;
      border:1px solid #edf2f7;background:#fbfdff;border-radius:12px;padding:10px 12px;
    }
    .toggle .k{font-size:.82rem;color:var(--muted)}
    .toggle input[type="checkbox"]{width:18px;height:18px;accent-color:#0f766e}
    .stepper{
      display:flex;align-items:center;justify-content:space-between;gap:10px;
      border:1px solid #edf2f7;background:#fbfdff;border-radius:12px;padding:10px 12px;
    }
    .stepper .copy{min-width:0}
    .stepper .copy .k{display:block;font-size:.8rem;color:var(--muted);margin-bottom:4px}
    .stepper .copy .v{font-weight:650}
    .stepper .controls{display:flex;align-items:center;gap:8px}
    .stepper .controls .btn{min-width:38px}
    input{
      width:100%;
      border:1px solid var(--line);
      border-radius:10px;
      padding:9px 10px;
      font:inherit;
      background:#fff;
    }
    input:focus{outline:none;border-color:#0ea5a0;box-shadow:0 0 0 3px rgba(14,165,160,.12)}
    .unit{font-size:.78rem;color:var(--muted);white-space:nowrap}
    .actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:14px}
    .toast{
      margin-top:10px;
      border-radius:10px;
      padding:10px 12px;
      font-size:.9rem;
      display:none;
    }
    .toast.ok{display:block;background:#ecfdf5;color:#065f46;border:1px solid #a7f3d0}
    .toast.err{display:block;background:#fef2f2;color:#991b1b;border:1px solid #fecaca}
    pre{
      margin:0;
      border-radius:12px;
      border:1px solid #edf2f7;
      background:#f8fafc;
      padding:12px;
      overflow:auto;
      font-size:.82rem;
      max-height:360px;
    }
    @media (min-width: 861px){
      .grid{grid-template-columns:.95fr 1.05fr}
      .status-card{order:2}
      .settings-card{order:1}
    }
    @media (max-width: 860px){
      .meta,.fields{grid-template-columns:1fr}
      .wrap{padding:12px 10px 20px}
      .hero{align-items:stretch}
      .hero .btn{width:auto}
      .actions .btn{width:100%}
    }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="hero">
      <div>
        <h1>Tank Telemetry Device</h1>
        <p>Runtime status and telemetry behavior settings (applies immediately).</p>
      </div>
      <button class="btn" type="button" onclick="refreshAll()">Refresh</button>
    </div>

    <div class="grid">
      <section class="card settings-card">
        <h2>Telemetry Settings</h2>
        <p class="muted">Tune state detection tolerance and publish frequency for your tank size and process speed.</p>

        <form id="settingsForm" onsubmit="saveSettings(event)">
          <div class="section">
            <h3>Scale Reading</h3>
            <div class="stepper">
              <div class="copy">
                <span class="k">Reading multiplier</span>
                <div class="v"><span id="readingMultiplierValue">1</span>x</div>
              </div>
              <div class="controls">
                <button class="btn icon" id="readingMultiplierDown" type="button" onclick="stepReadingMultiplier(-1)">-</button>
                <button class="btn icon" id="readingMultiplierUp" type="button" onclick="stepReadingMultiplier(1)">+</button>
              </div>
            </div>
            <p class="muted" style="margin:8px 0 0">Use this when the scale shifts the decimal place. Steps: 0.01, 0.1, 1, 10, 100.</p>
          </div>

          <div class="section">
            <h3>State Detection</h3>
            <div class="fields" id="stateFields"></div>
          </div>

          <div class="section">
            <h3>Cleaning Detection (Initial Heuristic)</h3>
            <p class="muted" style="margin:0 0 8px">Assumption-based noisy/oscillation detection. Tune after observing real cleaning sessions.</p>
            <div class="toggle">
              <div class="k">Enable cleaning detection</div>
              <input id="cleaningDetectEnabled" type="checkbox" />
            </div>
            <div class="fields" id="cleaningFields" style="margin-top:10px"></div>
          </div>

          <div class="section">
            <h3>Publish Intervals</h3>
            <div class="fields" id="publishFields"></div>
          </div>

          <div class="section">
            <h3>Heartbeat / Signal</h3>
            <div class="fields" id="healthFields"></div>
          </div>

          <div class="actions">
            <button class="btn primary" id="saveBtn" type="submit">Save Settings</button>
            <button class="btn" type="button" onclick="restoreDefaults()">Restore Defaults</button>
            <button class="btn danger" type="button" onclick="clearWifi()">Clear Wi-Fi & Reboot</button>
          </div>
          <div id="toast" class="toast"></div>
        </form>
      </section>

      <section class="card status-card">
        <h2>Device Status</h2>
        <div class="meta">
          <div class="pill">
            <span class="k">Device ID</span>
            <div class="copyline">
              <div class="v mono" id="deviceId">—</div>
              <button class="btn icon" type="button" title="Copy device ID" aria-label="Copy device ID" onclick="copyDeviceId()">⧉</button>
            </div>
          </div>
          <div class="pill">
            <span class="k">Time</span>
            <div class="v" id="time">—</div>
          </div>
          <div class="pill">
            <span class="k">IP Address</span>
            <div class="v mono" id="ip">—</div>
          </div>
          <div class="pill">
            <span class="k">Hostname</span>
            <div class="v mono" id="host">—</div>
          </div>
          <div class="pill">
            <span class="k">Wi-Fi</span>
            <div class="v"><span id="wifiDot" class="status-dot"></span><span id="wifiState">—</span></div>
          </div>
          <div class="pill">
            <span class="k">SSID</span>
            <div class="v" id="ssid">—</div>
          </div>
        </div>

        <h2 style="margin-top:10px">Live API Snapshot</h2>
        <pre id="out">Loading...</pre>
      </section>
    </div>
  </div>

  <script>
    const fieldDefs = [
      {key:'stateEvalPeriodMs', label:'State evaluation period', unit:'ms', group:'stateFields', step:'100', min:'0'},
      {key:'idleRateThreshold', label:'Idle rate threshold', unit:'kg/s', group:'stateFields', step:'0.01', min:'0'},
      {key:'fillRateThreshold', label:'Fill rate threshold', unit:'kg/s', group:'stateFields', step:'0.01', min:'0'},
      {key:'dischargeRateThreshold', label:'Discharge rate threshold', unit:'kg/s', group:'stateFields', step:'0.01', min:'0'},
      {key:'flowConfirmMs', label:'Flow confirm time', unit:'ms', group:'stateFields', step:'500', min:'0'},
      {key:'idleConfirmMs', label:'Idle confirm time', unit:'ms', group:'stateFields', step:'500', min:'0'},
      {key:'flowMinDelta', label:'Min flow delta to confirm', unit:'kg', group:'stateFields', step:'0.1', min:'0'},

      {key:'cleaningWindowMs', label:'Cleaning observation window', unit:'ms', group:'cleaningFields', step:'1000', min:'0'},
      {key:'cleaningMinSignFlips', label:'Min direction flips', unit:'count', group:'cleaningFields', step:'1', min:'1'},
      {key:'cleaningMinAbsDelta', label:'Min cumulative movement', unit:'kg', group:'cleaningFields', step:'0.1', min:'0'},
      {key:'cleaningConfirmMs', label:'Cleaning confirm time', unit:'ms', group:'cleaningFields', step:'500', min:'0'},

      {key:'telemetryIdleMs', label:'Telemetry interval (idle)', unit:'ms', group:'publishFields', step:'1000', min:'0'},
      {key:'telemetryFillingMs', label:'Telemetry interval (filling)', unit:'ms', group:'publishFields', step:'500', min:'0'},
      {key:'telemetryDischargingMs', label:'Telemetry interval (discharging)', unit:'ms', group:'publishFields', step:'500', min:'0'},
      {key:'telemetryCleaningMs', label:'Telemetry interval (cleaning)', unit:'ms', group:'publishFields', step:'500', min:'0'},
      {key:'telemetryUnknownMs', label:'Telemetry interval (unknown)', unit:'ms', group:'publishFields', step:'500', min:'0'},

      {key:'heartbeatPeriodMs', label:'Heartbeat interval', unit:'ms', group:'healthFields', step:'1000', min:'0'},
      {key:'scaleActiveTimeoutMs', label:'Scale active timeout', unit:'ms', group:'healthFields', step:'1000', min:'0'}
    ];
    const readingMultiplierSteps = [0.01, 0.1, 1, 10, 100];

    let currentSettings = null;
    let defaultSettings = null;
    let readingMultiplierIndex = readingMultiplierSteps.indexOf(1);

    function showToast(kind, msg){
      const el = document.getElementById('toast');
      el.className = 'toast ' + (kind === 'error' ? 'err' : 'ok');
      el.textContent = msg;
      clearTimeout(showToast._t);
      showToast._t = setTimeout(() => {
        el.className = 'toast';
        el.textContent = '';
      }, 2800);
    }

    function buildFields(){
      if (document.querySelector('[data-key]')) return;
      for (const d of fieldDefs){
        const host = document.getElementById(d.group);
        const box = document.createElement('div');
        box.className = 'field';
        box.innerHTML = `
          <label for="${d.key}">${d.label}</label>
          <div class="input-wrap">
            <input id="${d.key}" data-key="${d.key}" type="number" step="${d.step}" min="${d.min}">
            <span class="unit">${d.unit}</span>
          </div>
        `;
        host.appendChild(box);
      }
    }

    function fillForm(values){
      currentSettings = values;
      setReadingMultiplier(values.readingMultiplier ?? 1);
      const cleanToggle = document.getElementById('cleaningDetectEnabled');
      if (cleanToggle) cleanToggle.checked = !!values.cleaningDetectEnabled;
      for (const d of fieldDefs){
        const inp = document.getElementById(d.key);
        if (!inp) continue;
        if (values[d.key] !== undefined) inp.value = values[d.key];
      }
    }

    function collectForm(){
      const out = {
        readingMultiplier: readingMultiplierSteps[readingMultiplierIndex]
      };
      for (const d of fieldDefs){
        const inp = document.getElementById(d.key);
        if (!inp) continue;
        out[d.key] = inp.step && inp.step.includes('.') ? Number(inp.value) : parseInt(inp.value, 10);
      }
      out.cleaningDetectEnabled = !!document.getElementById('cleaningDetectEnabled')?.checked;
      return out;
    }

    function setReadingMultiplier(value){
      const idx = readingMultiplierSteps.findIndex(v => Math.abs(v - value) < 0.0001);
      readingMultiplierIndex = idx >= 0 ? idx : readingMultiplierSteps.indexOf(1);
      document.getElementById('readingMultiplierValue').textContent = readingMultiplierSteps[readingMultiplierIndex];
      document.getElementById('readingMultiplierDown').disabled = readingMultiplierIndex === 0;
      document.getElementById('readingMultiplierUp').disabled = readingMultiplierIndex === readingMultiplierSteps.length - 1;
    }

    function stepReadingMultiplier(direction){
      const nextIndex = Math.max(0, Math.min(readingMultiplierSteps.length - 1, readingMultiplierIndex + direction));
      if (nextIndex === readingMultiplierIndex) return;
      setReadingMultiplier(readingMultiplierSteps[nextIndex]);
    }

    async function refreshStatus(){
      const r = await fetch('/api/status', {cache:'no-store'});
      const j = await r.json();
      document.getElementById('out').textContent = JSON.stringify(j, null, 2);
      document.getElementById('deviceId').textContent = j.deviceId || '—';
      document.getElementById('ip').textContent = j.ip || '—';
      document.getElementById('host').textContent = j.hostname || '—';
      document.getElementById('ssid').textContent = j.ssid || '—';
      document.getElementById('time').textContent = j.time || '—';
      const wifiDot = document.getElementById('wifiDot');
      const wifiState = document.getElementById('wifiState');
      wifiDot.className = 'status-dot ' + (j.connected ? 'ok' : 'warn');
      wifiState.textContent = j.connected ? 'Connected' : 'Disconnected';
    }

    async function refreshSettings(){
      const r = await fetch('/api/settings', {cache:'no-store'});
      const j = await r.json();
      defaultSettings = j.defaults || null;
      fillForm(j.current || {});
    }

    async function refreshAll(){
      await Promise.all([refreshStatus(), refreshSettings()]);
    }

    async function saveSettings(ev){
      ev.preventDefault();
      const btn = document.getElementById('saveBtn');
      btn.disabled = true;
      try {
        const payload = collectForm();
        const r = await fetch('/api/settings', {
          method:'POST',
          headers:{'Content-Type':'application/json'},
          body: JSON.stringify(payload)
        });
        const j = await r.json().catch(() => ({}));
        if (!r.ok) throw new Error(j.error || 'Save failed');
        showToast('ok', 'Settings saved');
        await refreshStatus();
      } catch (e) {
        showToast('error', e.message || 'Save failed');
      } finally {
        btn.disabled = false;
      }
    }

    async function restoreDefaults(){
      if (!confirm('Restore telemetry settings to defaults?')) return;
      try{
        const r = await fetch('/api/settings/reset', {method:'POST'});
        const j = await r.json().catch(() => ({}));
        if (!r.ok) throw new Error(j.error || 'Reset failed');
        await refreshSettings();
        showToast('ok', 'Defaults restored');
      }catch(e){
        showToast('error', e.message || 'Reset failed');
      }
    }

    async function clearWifi(){
      if(!confirm('Clear Wi-Fi credentials and reboot?')) return;
      await fetch('/api/clear_wifi', {method:'POST'});
      alert('OK. Rebooting...');
    }

    async function copyDeviceId(){
      const text = (document.getElementById('deviceId').textContent || '').trim();
      if(!text || text === '—') return;
      try{
        if (navigator.clipboard && navigator.clipboard.writeText) {
          await navigator.clipboard.writeText(text);
        } else {
          const ta = document.createElement('textarea');
          ta.value = text;
          document.body.appendChild(ta);
          ta.select();
          document.execCommand('copy');
          ta.remove();
        }
        showToast('ok', 'Device ID copied');
      }catch(e){
        showToast('error', 'Copy failed');
      }
    }

    buildFields();
    refreshAll();
    setInterval(refreshStatus, 2000);
  </script>
</body>
</html>
)HTML";
}
