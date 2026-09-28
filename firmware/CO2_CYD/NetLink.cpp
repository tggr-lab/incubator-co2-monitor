#include "NetLink.h"

#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>
#include <uri/UriBraces.h>

#include "Config.h"

namespace {

// The live page. Self-contained: no CDN, no fonts, so it works on a lab
// network with no internet. Polls /api/status every 2 s and the history ring
// every 10 s, and draws the chart on a canvas in the instrument's own palette.
const char PAGE[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CO₂ Monitor</title>
<style>
:root{--bg:#000;--card:#101418;--line:#22282e;--txt:#f2f5f7;--dim:#a0a8b0;--faint:#606870;--teal:#1fa3aa;--tealD:#007078;--org:#e08010;--red:#e5484d}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--txt);font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
.wrap{max-width:760px;margin:0 auto;padding:20px 16px 40px}
header{display:flex;align-items:baseline;gap:12px;border-bottom:1px solid var(--line);padding-bottom:10px;margin-bottom:22px}
header .lab{color:var(--teal);font-size:12px;letter-spacing:.14em;text-transform:uppercase}
header h1{margin:0;font-size:18px;font-weight:600}
header .live{margin-left:auto;font-size:12px;letter-spacing:.12em;color:var(--teal)}
header .live.bad{color:var(--red)}
.big{display:flex;align-items:baseline;justify-content:center;gap:10px;margin:8px 0 0}
.big b{font-size:88px;font-weight:600;letter-spacing:-.02em;line-height:1}
.big span{font-size:32px;color:var(--dim)}
.ppm{text-align:center;color:var(--dim);font-size:22px;margin-top:4px}
.trend{text-align:center;color:var(--faint);font-size:13px;margin-top:2px}
.row{display:flex;gap:10px;justify-content:center;flex-wrap:wrap;margin:18px 0 26px}
.pill{padding:6px 16px;border-radius:999px;font-size:13px;letter-spacing:.08em;font-weight:600;color:#000;background:var(--faint)}
.pill.ok{background:var(--teal)}.pill.warn{background:var(--org)}.pill.err{background:var(--red)}
.tile{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px 14px;min-width:120px}
.tile small{display:block;color:var(--faint);font-size:11px;letter-spacing:.1em;text-transform:uppercase}
.tile b{font-size:18px;font-weight:600}
.chart{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px}
.chart .hd{display:flex;align-items:center;gap:8px;margin-bottom:8px}
.chart .hd span{color:var(--faint);font-size:12px;letter-spacing:.1em;text-transform:uppercase}
.chart .hd button{margin-left:auto;background:transparent;border:1px solid var(--line);color:var(--dim);border-radius:8px;padding:4px 10px;font-size:12px;cursor:pointer}
.chart .hd button+button{margin-left:6px}.chart .hd button.on{background:var(--tealD);border-color:var(--teal);color:#fff}
sub{font-size:.7em;line-height:0}
.chart .lg{display:flex;gap:14px;font-size:12px;color:var(--faint);margin:0 0 6px 4px}.chart .lg i{display:inline-block;width:14px;height:3px;vertical-align:middle;margin-right:5px}
canvas{width:100%;height:260px;display:block}
footer{margin-top:22px;color:var(--faint);font-size:12px;display:flex;gap:18px;flex-wrap:wrap}
footer a{color:var(--teal);text-decoration:none}
</style></head><body><div class="wrap">
<header><div><div class="lab">TGGR Lab</div><h1>CO<sub>2</sub> Monitor</h1></div><div class="live" id="live">—</div></header>
<div class="big"><b id="pct">--</b><span>%</span></div>
<div class="ppm" id="ppm">-- ppm</div>
<div class="trend" id="trend"></div>
<div class="row"><span class="pill" id="status">—</span></div>
<div class="row">
 <div class="tile"><small>Session min</small><b id="min">--</b></div>
 <div class="tile"><small>Session max</small><b id="max">--</b></div>
 <div class="tile"><small>Temperature</small><b id="temp">--</b></div>
 <div class="tile"><small>Temp probe</small><b id="probe">--</b></div>
</div>
<div class="chart"><div class="hd"><span>History</span>
 <button data-m="5">5 min</button><button data-m="15" class="on">15 min</button><button data-m="60">60 min</button></div>
 <div class="lg"><span><i style="background:#1fa3aa"></i>CO<sub>2</sub> ppm</span><span><i style="background:#e08010"></i>temperature °C (right axis)</span></div>
 <canvas id="c" width="1400" height="520"></canvas></div>
<div class="chart" style="margin-top:14px"><div class="hd"><span>Log files on card</span></div><div id="fl" style="color:var(--dim);font-size:14px;line-height:1.9">…</div></div>
<footer><span id="up"></span><span id="clock"></span><span id="net"></span><a href="/history.csv">Download CSV</a><a href="/api/status">JSON</a></footer>
</div>
<script>
const $=id=>document.getElementById(id);const fmt=n=>n==null?'--':Math.round(n).toLocaleString();
let hist=[],mins=15,band=[4.5,5.5];
async function status(){try{const r=await fetch('/api/status',{cache:'no-store'});const s=await r.json();
 $('pct').textContent=s.pct==null?'--':(s.pct>=1?s.pct.toFixed(2):s.pct.toFixed(3));
 $('ppm').textContent=fmt(s.ppm)+' ppm';
 $('trend').textContent=s.trend==null?'':(Math.abs(s.trend)<20?'steady':(s.trend>0?'rising ':'falling ')+Math.abs(Math.round(s.trend))+' ppm/min');
 const st=$('status');st.textContent=s.status;st.className='pill '+(s.status=='NORMAL'?'ok':s.status=='SENSOR ERROR'?'err':/CO2/.test(s.status)?'warn':'');
 const lv=$('live');lv.textContent=s.live?'LIVE':'STALE';lv.className='live'+(s.live?'':' bad');
 $('min').textContent=fmt(s.min);$('max').textContent=fmt(s.max);
 $('temp').textContent=s.temp==null?'n/a':s.temp.toFixed(1)+' °C';$('probe').textContent=s.probe;
 $('up').textContent='up '+Math.floor(s.uptime/3600)+'h '+Math.floor(s.uptime%3600/60)+'m';
 $('clock').textContent=s.time?new Date(s.time*1000).toLocaleString()+(s.time_source=='browser'?' (phone clock)':''):'clock not synced';
 $('net').textContent=s.ip+' · '+s.rssi+' dBm';band=[s.low,s.high];
}catch(e){$('live').textContent='OFFLINE';$('live').className='live bad'}}
async function history(){try{const r=await fetch('/api/history.json',{cache:'no-store'});hist=await r.json();draw()}catch(e){}}
function draw(){const c=$('c'),g=c.getContext('2d'),W=c.width,H=c.height;g.clearRect(0,0,W,H);
 const span=mins*60,pts=hist.filter(p=>p[0]<=span&&p[1]!=null);
 if(pts.length<2){g.fillStyle='#606870';g.font='28px system-ui';g.textAlign='center';g.fillText('collecting data…',W/2,H/2);return}
 let lo=Math.min(...pts.map(p=>p[1])),hi=Math.max(...pts.map(p=>p[1]));const mr=Math.max(100,hi*.02);if(hi-lo<mr){const m=(hi+lo)/2;lo=m-mr/2;hi=m+mr/2}
 const st=nice((hi-lo)/3);lo=Math.max(0,Math.floor(lo/st)*st);hi=Math.ceil(hi/st)*st;
 const L=70,R=70,T=16,B=36,X=s=>L+(W-L-R)*(1-s/span),Y=v=>T+(H-T-B)*(1-(v-lo)/(hi-lo));
 const b0=band[0]*1e4,b1=band[1]*1e4;if(b1>lo&&b0<hi){g.fillStyle='rgba(0,112,120,.25)';g.fillRect(L,Y(Math.min(b1,hi)),W-L-R,Y(Math.max(b0,lo))-Y(Math.min(b1,hi)))}
 g.strokeStyle='#22282e';g.lineWidth=1;g.fillStyle='#606870';g.font='20px system-ui';g.textAlign='right';
 for(let v=lo;v<=hi+1e-6;v+=st){g.beginPath();g.moveTo(L,Y(v));g.lineTo(W-R,Y(v));g.stroke();g.fillText(v.toLocaleString(),L-10,Y(v)+7)}
 g.textAlign='center';for(const m of[0,mins/2,mins]){g.fillText(m?'-'+m+' min':'now',X(m*60),H-10)}
 const grd=g.createLinearGradient(0,T,0,H-B);grd.addColorStop(0,'rgba(31,163,170,.45)');grd.addColorStop(1,'rgba(31,163,170,0)');
 g.beginPath();pts.forEach((p,i)=>i?g.lineTo(X(p[0]),Y(p[1])):g.moveTo(X(p[0]),Y(p[1])));
 const last=pts[pts.length-1];g.lineTo(X(last[0]),H-B);g.lineTo(X(pts[0][0]),H-B);g.closePath();g.fillStyle=grd;g.fill();
 g.beginPath();pts.forEach((p,i)=>i?g.lineTo(X(p[0]),Y(p[1])):g.moveTo(X(p[0]),Y(p[1])));g.strokeStyle='#1fa3aa';g.lineWidth=3;g.lineJoin='round';g.stroke();
 g.fillStyle='#fff';g.beginPath();g.arc(X(pts[0][0]),Y(pts[0][1]),6,0,7);g.fill();
 const tp=hist.filter(p=>p[0]<=span&&p[3]!=null);
 if(tp.length>=2){let a=Math.min(...tp.map(p=>p[3])),b=Math.max(...tp.map(p=>p[3]));if(b-a<1){const m=(a+b)/2;a=m-.5;b=m+.5}a=Math.floor(a*2)/2;b=Math.ceil(b*2)/2;
  const YT=v=>T+(H-T-B)*(1-(v-a)/(b-a));g.beginPath();tp.forEach((p,i)=>i?g.lineTo(X(p[0]),YT(p[3])):g.moveTo(X(p[0]),YT(p[3])));
  g.strokeStyle='#e08010';g.lineWidth=2.5;g.lineJoin='round';g.stroke();
  g.fillStyle='#e08010';g.font='20px system-ui';g.textAlign='left';g.fillText(b.toFixed(1)+'°',W-R+4,T+8);g.fillText(a.toFixed(1)+'°',W-R+4,H-B);}
 g.fillStyle='#e08010';pts.filter(p=>p[2]).forEach(p=>{g.beginPath();g.moveTo(X(p[0])-8,T);g.lineTo(X(p[0])+8,T);g.lineTo(X(p[0]),T+14);g.fill()});
}
function nice(r){const m=Math.pow(10,Math.floor(Math.log10(r))),n=r/m;return(n<=1?1:n<=2?2:n<=5?5:10)*m}
document.querySelectorAll('.hd button').forEach(b=>b.onclick=()=>{mins=+b.dataset.m;document.querySelectorAll('.hd button').forEach(x=>x.classList.toggle('on',x==b));draw()});
async function files(){try{const r=await fetch('/sd/',{cache:'no-store'});const s=await r.json();const el=$('fl');
 if(!s.card){el.textContent='no card';return}
 if(!s.files.length){el.textContent='no log files';return}
 el.innerHTML=s.files.sort((a,b)=>b.name.localeCompare(a.name)).map(f=>'<a href="/sd/'+f.name+'" style="color:var(--teal);text-decoration:none">'+f.name+'</a> <span style="color:var(--faint)">'+(f.size/1024).toFixed(0)+' kB'+(f.name==s.current?' · current':'')+'</span>').join('<br>')}catch(e){}}
fetch('/api/time',{method:'POST',body:String(Math.floor(Date.now()/1000))}).catch(()=>{});
status();history();files();setInterval(status,2000);setInterval(history,10000);setInterval(files,60000);
</script></body></html>)HTML";

// Set from the SNTP task when a sync lands; polled from update().
volatile bool g_ntpSynced = false;
void onNtpSync(struct timeval*) { g_ntpSynced = true; }

}  // namespace

// ---------------------------------------------------------------------------

bool NetLink::timeSynced() const { return time(nullptr) > 1600000000; }

void NetLink::loadCredentials() {
  Preferences p;
  if (!p.begin("net", true)) return;
  p.getString("ssid", _ssid, sizeof(_ssid));
  p.getString("pass", _pass, sizeof(_pass));
  p.end();
}

bool NetLink::setCredentials(const char* ssid, const char* pass) {
  if (!ssid || !*ssid) return false;
  Preferences p;
  if (!p.begin("net", false)) return false;
  p.putString("ssid", ssid);
  p.putString("pass", pass ? pass : "");
  p.end();
  strncpy(_ssid, ssid, sizeof(_ssid) - 1);
  strncpy(_pass, pass ? pass : "", sizeof(_pass) - 1);
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, true);
  _state = State::Off;
  _nextRetryMs = 0;
  _everOnline = false;
  _apFallbackAtMs = millis() + config::NET_AP_FALLBACK_MS;   // a fresh chance to join
  return true;
}

void NetLink::clearCredentials() {
  Preferences p;
  if (p.begin("net", false)) { p.clear(); p.end(); }
  _ssid[0] = _pass[0] = '\0';
  WiFi.disconnect(true, true);
  _everOnline = false;
  if (config::NET_AP_ENABLED) {
    startAccessPoint();          // stay reachable rather than going dark
  } else {
    WiFi.mode(WIFI_OFF);
    _state = State::Off;
    refreshSummary();
  }
}

void NetLink::begin(const Instrument* inst) {
  _inst = inst;
  loadCredentials();
  refreshSummary();
  _apFallbackAtMs = millis() + config::NET_AP_FALLBACK_MS;
  if (!hasCredentials()) {
    if (config::NET_AP_ENABLED) {
      Serial.println(F("net: no credentials stored -- starting access point (tools/wifi_setup.py to join a network)"));
      startAccessPoint();
    } else {
      Serial.println(F("net: no credentials stored -- Wi-Fi off (tools/wifi_setup.py to enable)"));
    }
    return;
  }
  startConnect();
}

// The web server and mDNS name are the same whichever side of the link we
// are on, so they start once and survive a switch between modes.
void NetLink::startServices() {
  if (!_mdnsStarted && MDNS.begin(config::NET_HOSTNAME)) {
    MDNS.addService("http", "tcp", config::HTTP_PORT);
    _mdnsStarted = true;
  }
  if (!_serverStarted) {
    _server.on("/", HTTP_GET, [this] { handleRoot(); });
    _server.on("/api/status", HTTP_GET, [this] { handleStatus(); });
    _server.on("/api/history.json", HTTP_GET, [this] { handleHistoryJson(); });
    _server.on("/history.csv", HTTP_GET, [this] { handleHistoryCsv(); });
    _server.on("/sd/", HTTP_GET, [this] { handleSdIndex(); });
    _server.on(UriBraces("/sd/{}"), HTTP_GET, [this] { handleSdFile(); });
    _server.on("/api/time", HTTP_POST, [this] { handleTime(); });
    _server.onNotFound([this] { _server.send(404, "text/plain", "not found"); });
    _server.begin();
    _serverStarted = true;
  }
}

void NetLink::startAccessPoint() {
  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP);
  WiFi.setHostname(config::NET_HOSTNAME);
  const char* pass = config::NET_AP_PASS[0] ? config::NET_AP_PASS : nullptr;
  if (!WiFi.softAP(config::NET_AP_SSID, pass)) {
    Serial.println(F("net: access point failed to start"));
    _state = State::Off;
    refreshSummary();
    return;
  }
  _state = State::AccessPoint;
  startServices();
  refreshSummary();
  Serial.printf("net: access point \"%s\" up  http://%s/  (%s)\n",
                config::NET_AP_SSID, WiFi.softAPIP().toString().c_str(),
                pass ? "WPA2" : "open");
}

void NetLink::startConnect() {
  WiFi.persistent(false);          // credentials live in our own NVS namespace
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(config::NET_HOSTNAME);
  WiFi.setSleep(true);             // modem sleep; we serve a page, not a stream
  WiFi.begin(_ssid, _pass);
  _state = State::Connecting;
  _nextRetryMs = millis() + config::NET_RETRY_MS;
  refreshSummary();
  Serial.printf("net: connecting to \"%s\"\n", _ssid);
}

void NetLink::onOnline() {
  _state = State::Online;
  _everOnline = true;
  startServices();
  esp_sntp_set_time_sync_notification_cb(onNtpSync);
  configTzTime(config::TZ_INFO, config::NTP_SERVER);
  refreshSummary();
  Serial.printf("net: online  http://%s.local/  (%s, %d dBm)\n",
                config::NET_HOSTNAME, WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

void NetLink::refreshSummary() {
  if (_state == State::Online) snprintf(_summary, sizeof(_summary), "%s", WiFi.localIP().toString().c_str());
  else if (_state == State::AccessPoint) snprintf(_summary, sizeof(_summary), "AP %s", WiFi.softAPIP().toString().c_str());
  else if (_state == State::Connecting) snprintf(_summary, sizeof(_summary), "connecting");
  else snprintf(_summary, sizeof(_summary), hasCredentials() ? "down" : "off");
}

void NetLink::update() {
  if (g_ntpSynced) { g_ntpSynced = false; _timeSource = TimeSource::Ntp; }
  if (_state == State::AccessPoint) { _server.handleClient(); return; }
  if (!hasCredentials()) return;
  const uint32_t now = millis();
  const bool connected = WiFi.status() == WL_CONNECTED;

  if (_state == State::Online) {
    if (!connected) {
      _state = State::Connecting;
      _nextRetryMs = now + config::NET_RETRY_MS;
      refreshSummary();
      Serial.println(F("net: link lost, reconnecting"));
    } else {
      _server.handleClient();
    }
    return;
  }

  if (connected) { onOnline(); return; }

  // Never joined since boot (or since new credentials) and the grace period
  // is over: the network is probably one we cannot join at all, so serve
  // directly instead. A link that was up and dropped keeps reconnecting.
  if (config::NET_AP_ENABLED && !_everOnline && (int32_t)(now - _apFallbackAtMs) >= 0) {
    Serial.printf("net: could not join \"%s\" -- falling back to access point\n", _ssid);
    startAccessPoint();
    return;
  }

  // Not connected: let the stack retry on its own schedule, nudging it if it
  // has clearly given up.
  if (_state == State::Off || now >= _nextRetryMs) {
    if (_state == State::Off) startConnect();
    else { WiFi.reconnect(); _nextRetryMs = now + config::NET_RETRY_MS; }
  }
}

// ------------------------------------------------------------ handlers -----

void NetLink::handleRoot() {
  _server.sendHeader("Cache-Control", "no-store");
  _server.send_P(200, "text/html; charset=utf-8", PAGE);
}

void NetLink::handleStatus() {
  const Co2Pwm::Reading& c = _inst->co2();
  const Ds18b20Sensor::Reading& b = _inst->probe();
  const float ppm = _inst->displayPpm(), pct = _inst->displayPercent(), tr = _inst->trendPpmPerMin();
  const time_t now = time(nullptr);

  char num[9][24];
  auto f = [&](int i, float v, const char* fmt) { if (isnan(v)) strcpy(num[i], "null"); else snprintf(num[i], 24, fmt, v); return num[i]; };

  // ppm/pct are corrected; raw_ppm is the last single cycle and raw_avg_ppm
  // the same rolling mean as ppm before correction.
  char out[768];
  snprintf(out, sizeof(out),
    "{\"ppm\":%s,\"pct\":%s,\"raw_ppm\":%s,\"raw_avg_ppm\":%s,\"cal_gain\":%.4f,\"cal\":\"%s\","
    "\"trend\":%s,\"status\":\"%s\",\"live\":%s,"
    "\"min\":%s,\"max\":%s,\"temp\":%s,\"pres\":%s,\"probe\":\"%s\","
    "\"th_ms\":%.1f,\"tl_ms\":%.1f,\"period_ms\":%.1f,\"valid\":%lu,\"rejected\":%lu,\"age_s\":%.1f,"
    "\"low\":%.2f,\"high\":%.2f,\"target\":%.2f,"
    "\"uptime\":%lu,\"time\":%ld,\"time_source\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"heap\":%u,\"fw\":\"%s\"}",
    f(0, ppm, "%.0f"), f(1, pct, "%.4f"), f(2, _inst->rawPpm(), "%.0f"),
    f(8, _inst->rawAveragePpm(), "%.0f"), config::CO2_CAL_GAIN, config::CO2_CAL_LABEL, f(3, tr, "%.1f"),
    Instrument::statusText(_inst->status()), _inst->sensorOk() ? "true" : "false",
    f(4, _inst->history().sessionMin(), "%.0f"), f(5, _inst->history().sessionMax(), "%.0f"),
    f(6, b.valid ? b.temperatureC : NAN, "%.2f"), f(7, NAN, "%.1f"),   // pres: always null now
    !b.present ? "absent" : b.valid ? "ok" : "no data",
    c.thMs, c.tlMs, c.periodMs, (unsigned long)c.validCount, (unsigned long)c.rejectCount, c.ageMs / 1000.0f,
    config::CO2_LOW_PCT, config::CO2_HIGH_PCT, config::CO2_TARGET_PCT,
    (unsigned long)_inst->uptimeSec(), (long)(timeSynced() ? now : 0), timeSourceText(),
    (_state == State::AccessPoint ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str(),
    _state == State::AccessPoint ? 0 : WiFi.RSSI(), (unsigned)ESP.getFreeHeap(), config::FIRMWARE_VERSION);

  _server.sendHeader("Cache-Control", "no-store");
  _server.sendHeader("Access-Control-Allow-Origin", "*");
  _server.send(200, "application/json", out);
}

// [[seconds_ago, ppm|null, event, temp_c|null], ...], newest first.
void NetLink::handleHistoryJson() {
  const History& h = _inst->history();
  const uint16_t n = h.size();
  String out;
  out.reserve(n * 26 + 4);
  out += '[';
  for (uint16_t i = 0; i < n; i++) {
    const float v = h.atFromNewest(i);
    const uint32_t ago = (uint32_t)i * config::HISTORY_INTERVAL_MS / 1000UL;
    char item[40];
    const float tv = h.tempFromNewest(i);
    char tstr[12];
    if (isnan(tv)) strcpy(tstr, "null"); else snprintf(tstr, sizeof(tstr), "%.2f", tv);
    if (isnan(v)) snprintf(item, sizeof(item), "%s[%lu,null,0,%s]", i ? "," : "", (unsigned long)ago, tstr);
    else snprintf(item, sizeof(item), "%s[%lu,%.0f,%d,%s]", i ? "," : "", (unsigned long)ago, v,
                  (h.flagFromNewest(i) & History::FLAG_EVENT) ? 1 : 0, tstr);
    out += item;
  }
  out += ']';
  _server.sendHeader("Cache-Control", "no-store");
  _server.sendHeader("Access-Control-Allow-Origin", "*");
  _server.send(200, "application/json", out);
}

void NetLink::handleHistoryCsv() {
  const History& h = _inst->history();
  const uint16_t n = h.size();
  const time_t now = time(nullptr);
  const bool synced = timeSynced();

  _server.sendHeader("Content-Disposition", "attachment; filename=co2_history.csv");
  _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  _server.send(200, "text/csv", "");
  _server.sendContent("time_utc,seconds_ago,co2_ppm,co2_pct,temp_c,event\n");

  String chunk;
  chunk.reserve(1024);
  for (int i = n - 1; i >= 0; i--) {          // oldest first, as a log reads
    const float v = h.atFromNewest(i);
    const uint32_t ago = (uint32_t)i * config::HISTORY_INTERVAL_MS / 1000UL;
    char stamp[24] = "";
    if (synced) {
      const time_t t = now - ago;
      struct tm tmv; gmtime_r(&t, &tmv);
      strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    }
    const float tv = h.tempFromNewest(i);
    char tstr[12] = "";
    if (!isnan(tv)) snprintf(tstr, sizeof(tstr), "%.2f", tv);
    char line[96];
    if (isnan(v)) snprintf(line, sizeof(line), "%s,%lu,,,%s,%d\n", stamp, (unsigned long)ago, tstr, 0);
    else snprintf(line, sizeof(line), "%s,%lu,%.0f,%.4f,%s,%d\n", stamp, (unsigned long)ago, v, v / 10000.0f,
                  tstr, (h.flagFromNewest(i) & History::FLAG_EVENT) ? 1 : 0);
    chunk += line;
    if (chunk.length() > 900) { _server.sendContent(chunk); chunk = ""; }
  }
  if (chunk.length()) _server.sendContent(chunk);
  _server.sendContent("");
}

// ------------------------------------------------------------- SD files ----

void NetLink::handleSdIndex() {
  Logger::FileInfo files[32];
  const size_t n = _log ? _log->listFiles(files, 32) : 0;
  String out;
  out.reserve(80 + n * 40);
  out += "{\"card\":"; out += (_log && _log->cardPresent()) ? "true" : "false";
  out += ",\"current\":\""; out += _log ? _log->currentFile() : ""; out += "\",\"files\":[";
  for (size_t i = 0; i < n; i++) {
    if (i) out += ',';
    out += "{\"name\":\""; out += files[i].name; out += "\",\"size\":"; out += files[i].size; out += '}';
  }
  out += "]}";
  _server.sendHeader("Cache-Control", "no-store");
  _server.send(200, "application/json", out);
}

void NetLink::handleSdFile() {
  const String name = _server.pathArg(0);
  if (!Logger::validName(name.c_str())) { _server.send(400, "text/plain", "not a log file name"); return; }
  if (!_log || !_log->streamFile(_server, name.c_str())) { _server.send(404, "text/plain", "no such file"); return; }
}

// ---------------------------------------------------------------- clock ----

const char* NetLink::timeSourceText() const {
  switch (_timeSource) {
    case TimeSource::Browser: return "browser";
    case TimeSource::Ntp:     return "ntp";
    default:                  return "none";
  }
}

// The page posts its clock on load. Accepted only while NTP has not synced;
// a wrong phone clock gives a wrong time, which NTP corrects when it can.
void NetLink::handleTime() {
  _server.sendHeader("Cache-Control", "no-store");
  if (_timeSource != TimeSource::Ntp) {
    const long epoch = _server.arg("plain").toInt();
    if (epoch < 1577836800L || epoch > 4102444800L) {
      _server.send(400, "text/plain", "epoch out of range");
      return;
    }
    struct timeval tv;
    tv.tv_sec = (time_t)epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    _timeSource = TimeSource::Browser;
  }
  char out[64];
  snprintf(out, sizeof(out), "{\"time\":%ld,\"source\":\"%s\"}", (long)time(nullptr), timeSourceText());
  _server.send(200, "application/json", out);
}
