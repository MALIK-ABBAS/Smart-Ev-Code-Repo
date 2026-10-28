#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>

// ==================== PIN CONFIGURATION ====================
#define SS_PIN        5   // RFID SDA/SS
#define RST_PIN       22  // RFID RST
#define SERVO_PIN     13  // Barrier Servo Motor
#define IR_PIN_1      14  // IR Proximity Sensor Slot 1
#define RELAY_PIN_1   26  // Relay Slot 1 (Charging Pad)
#define IR_PIN_2      27  // IR Proximity Sensor Slot 2
#define RELAY_PIN_2   25  // Relay Slot 2 (Charging Pad)
#define SOLAR_PIN     34  // Analog Pin for Solar Voltage Divider
#define BUZZER_PIN    4   // Transistor Base Control for Buzzer
#define GREEN_LED_PIN 16  // Green Status LED (Access Granted) - RX2 pin
#define RED_LED_PIN   17  // Red Status LED (Access Denied) - TX2 pin

// ==================== WI-FI CREDENTIALS ====================
const char* ssid = "POCO X5 5G";        
const char* password = "786786786";    

// ==================== SYSTEM VARIABLES ====================
Servo gateServo;
MFRC522 rfc(SS_PIN, RST_PIN);
WebServer server(80);

// Slot & System States
bool isSlot1Occupied = false;
bool isSlot2Occupied = false;
bool isChargingActive1 = false;
bool isChargingActive2 = false;
bool isGateOpen = false;

// Robust Timing Tracking for IR Charging Trigger
unsigned long slot1StartTime = 0;
unsigned long slot2StartTime = 0;
const unsigned long IR_DELAY_TIME = 1000; // Exact 1 second delay to turn ON

// User Wallets & Billing Telemetry
float malikBalance = 25000.0;   // Malik Wallet
float irfanBalance = 20000.0; // Irfan Wallet
float jafferBalance = 15000.0;  // Jaffer Wallet (Deniable)
const float CHARGE_COST = 500.0;

String lastScannedUID = "None";
String lastScannedName = "None";
float solarVoltage = 0.0;

// Charging timer variables for automatic cutoff (10 seconds demo duration)
unsigned long chargeStartTime1 = 0;
unsigned long chargeStartTime2 = 0;
const unsigned long CHARGE_DURATION = 10000; 

// Gate Auto-Close Timers (3 seconds delay)
unsigned long gateOpenTimestamp = 0;
const unsigned long GATE_OPEN_DURATION = 3000; 
bool gateAutoCloseActive = false;

// Resistor divider values for Solar monitoring (20k top, 10k bottom)
const float r_top = 20000.0;
const float r_bottom = 10000.0;

// ==================== BUZZER FUNCTIONS ====================
void beepSuccess() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(150);
  digitalWrite(BUZZER_PIN, LOW);
}

void beepGateOpen() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(100);
  digitalWrite(BUZZER_PIN, LOW);
  delay(50);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(100);
  digitalWrite(BUZZER_PIN, LOW);
}

void beepDenied() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(80);
    digitalWrite(BUZZER_PIN, LOW);
    delay(80);
  }
}

// ==================== WEB DASHBOARD HTML ====================
const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="theme-color" content="#123c2d">
<title>Smart EV Hub | Smart EV Charging & Parking</title>
<style>
:root{
  --bg:#f4f7f2;--paper:#fff;--ink:#172b22;--muted:#718078;--line:#e3ebe4;
  --green:#176b48;--green2:#dff4e7;--lime:#c9f36a;--amber:#e9a23b;
  --red:#d94f4f;--shadow:0 12px 32px rgba(27,61,42,.07);--radius:22px
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font-family:Inter,ui-sans-serif,system-ui,-apple-system,"Segoe UI",sans-serif}
button{font:inherit;cursor:pointer}
.shell{max-width:1220px;margin:auto;padding:26px}
.topbar{display:flex;align-items:center;justify-content:space-between;gap:18px;margin-bottom:24px}
.brand{display:flex;align-items:center;gap:13px}
.logo{width:48px;height:48px;border-radius:16px;background:var(--green);color:white;display:grid;place-items:center;font-size:24px;box-shadow:0 8px 18px #176b4828}
.brand h1{font-size:20px;letter-spacing:-.5px;margin:0}
.brand p{margin:4px 0 0;color:var(--muted);font-size:12px}
.live{display:flex;align-items:center;gap:8px;background:white;border:1px solid var(--line);padding:10px 14px;border-radius:999px;color:var(--muted);font-size:12px;font-weight:700}
.dot{width:8px;height:8px;border-radius:50%;background:#24a66a;box-shadow:0 0 0 4px #24a66a1c}
.hero{background:var(--green);color:white;border-radius:28px;padding:28px 30px;display:flex;align-items:center;justify-content:space-between;gap:20px;position:relative;overflow:hidden;margin-bottom:20px}
.hero:after{content:"";position:absolute;width:260px;height:260px;border:1px solid #ffffff18;border-radius:50%;right:8%;top:-120px;box-shadow:0 0 0 32px #ffffff08,0 0 0 65px #ffffff06}
.eyebrow{text-transform:uppercase;letter-spacing:2px;font-size:10px;font-weight:800;color:#c9f36a;margin-bottom:10px}
.hero h2{font-size:clamp(25px,4vw,38px);line-height:1.08;letter-spacing:-1.2px;margin:0;max-width:570px}
.hero p{color:#d2e6d9;font-size:13px;line-height:1.6;margin:12px 0 0;max-width:520px}
.hero-stat{z-index:1;min-width:150px;background:#ffffff12;border:1px solid #ffffff25;border-radius:20px;padding:17px 20px}
.hero-stat small{display:block;color:#cfe2d4;font-size:11px;margin-bottom:7px}
.hero-stat strong{font-size:29px;letter-spacing:-1px}
.hero-stat span{display:block;font-size:11px;color:#cfe2d4;margin-top:4px}
.section-head{display:flex;align-items:end;justify-content:space-between;gap:12px;margin:25px 2px 13px}
.section-head h3{margin:0;font-size:16px;letter-spacing:-.3px}
.section-head p{margin:4px 0 0;color:var(--muted);font-size:12px}
.grid{display:grid;grid-template-columns:repeat(12,minmax(0,1fr));gap:16px}
.card{background:var(--paper);border:1px solid var(--line);border-radius:var(--radius);padding:20px;box-shadow:var(--shadow);min-width:0}
.solar{grid-column:span 4}.gate{grid-column:span 4}.slot{grid-column:span 4}
.card-top{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:17px}
.card-title{display:flex;align-items:center;gap:10px;font-weight:800;font-size:13px}
.icon{width:34px;height:34px;display:grid;place-items:center;border-radius:12px;background:var(--green2);font-size:17px}
.pill{display:inline-flex;align-items:center;gap:6px;border-radius:999px;padding:6px 9px;font-size:10px;font-weight:800;letter-spacing:.4px;background:#edf1ee;color:#66736a;white-space:nowrap}
.pill.good{background:#e2f6e9;color:#167548}.pill.warn{background:#fff0d8;color:#9b6413}.pill.bad{background:#ffebeb;color:#b63838}.pill.blue{background:#e9f1ff;color:#2f62a9}
.metric{font-size:34px;font-weight:850;letter-spacing:-1.4px;line-height:1.1;margin:5px 0 7px}
.metric em{font-style:normal;font-size:15px;color:var(--muted);font-weight:700;letter-spacing:0}
.sub{font-size:12px;color:var(--muted);line-height:1.5}
.mini-track{height:7px;background:#edf2ed;border-radius:99px;margin:17px 0 9px;overflow:hidden}
.mini-track span{display:block;height:100%;width:0;background:var(--lime);border-radius:99px;transition:width .4s}
.info-line{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:11px 0;font-size:12px;color:var(--muted)}
.info-line strong{color:var(--ink);font-size:12px}
.actions{display:flex;gap:8px;margin-top:16px}
.btn{border:0;border-radius:12px;padding:11px 13px;font-size:12px;font-weight:800;transition:transform .15s,filter .15s}
.btn:hover{filter:brightness(.97);transform:translateY(-1px)}.btn:disabled{opacity:.5;cursor:wait;transform:none}
.btn-primary{background:var(--green);color:white}.btn-soft{background:var(--green2);color:var(--green)}.btn-danger{background:#ffeded;color:#b63838}
.actions .btn{flex:1}
.slot-state{display:flex;align-items:center;gap:10px;background:#f7faf7;border:1px solid var(--line);padding:12px;border-radius:14px;margin-top:12px}
.slot-symbol{width:38px;height:38px;border-radius:12px;background:white;border:1px solid var(--line);display:grid;place-items:center;font-size:18px}
.slot-state strong{display:block;font-size:12px}.slot-state small{display:block;color:var(--muted);font-size:11px;margin-top:3px}
.wallet-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:16px}
.wallet{position:relative;overflow:hidden}
.wallet:after{content:"";position:absolute;right:-22px;top:-27px;width:95px;height:95px;border-radius:50%;background:var(--green2);opacity:.65}
.wallet-head{display:flex;align-items:center;gap:11px;position:relative;z-index:1}
.avatar{width:42px;height:42px;border-radius:15px;background:#e4f5e9;color:var(--green);display:grid;place-items:center;font-weight:900}
.wallet-name{font-weight:850;font-size:14px}.wallet-label{font-size:11px;color:var(--muted);margin-top:3px}
.wallet-balance{font-size:27px;font-weight:850;letter-spacing:-1px;margin:22px 0 4px;position:relative;z-index:1}
.wallet-foot{display:flex;justify-content:space-between;gap:8px;color:var(--muted);font-size:11px;margin-top:15px;padding-top:13px;border-top:1px solid var(--line)}
.activity{grid-column:span 7}.billing{grid-column:span 5}
.scan-banner{display:flex;align-items:center;gap:14px;padding:16px;border-radius:17px;background:#f5f9f4;border:1px solid var(--line)}
.scan-icon{width:46px;height:46px;border-radius:15px;background:var(--green2);display:grid;place-items:center;font-size:21px;flex-shrink:0}
.scan-banner strong{font-size:14px;display:block}.scan-banner p{font-size:12px;color:var(--muted);margin:5px 0 0;line-height:1.5}
.scan-banner .pill{margin-left:auto}
.uid{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:11px;letter-spacing:.5px;overflow-wrap:anywhere}
.notice{font-size:11px;color:var(--muted);line-height:1.6;margin-top:13px}
.billing-amount{font-size:30px;font-weight:850;letter-spacing:-1px;margin:15px 0 3px}
.footer{display:flex;justify-content:space-between;gap:12px;color:#8b978f;font-size:10px;padding:24px 3px 5px}
#toast{position:fixed;bottom:20px;left:50%;transform:translate(-50%,20px);opacity:0;pointer-events:none;background:#183c2d;color:white;padding:12px 17px;border-radius:12px;font-size:12px;box-shadow:var(--shadow);transition:.2s;z-index:5;max-width:90vw}
#toast.show{opacity:1;transform:translate(-50%,0)}
@media(max-width:900px){.solar,.gate,.slot{grid-column:span 6}.slot:last-child{grid-column:span 6}.activity,.billing{grid-column:span 12}.wallet-grid{grid-template-columns:1fr 1fr}}
@media(max-width:560px){.shell{padding:15px}.topbar{align-items:flex-start}.brand h1{font-size:17px}.live{font-size:0;padding:12px}.live .dot{font-size:initial}.hero{padding:22px;display:block}.hero-stat{margin-top:20px;display:inline-block}.solar,.gate,.slot{grid-column:span 12}.wallet-grid{grid-template-columns:1fr}.section-head{margin-top:22px}.card{padding:17px}.footer{flex-direction:column}}
</style>
</head>
<body>
<div class="shell">
  <header class="topbar">
    <div class="brand"><div class="logo">ϟ</div><div><h1>Smart <span style="color:var(--green)">EV Hub</span></h1><p>Smart charging · Vehicle detection · RFID access</p></div></div>
    <div class="live"><span class="dot"></span><span id="connection">CONNECTING TO ESP32</span></div>
  </header>

  <section class="hero">
    <div><div class="eyebrow">⚡ Intelligent mobility, made simple</div><h2>Charge smarter.<br>Move greener.</h2><p>A live view of our Smart Iot parking station, access gate and RFID wallet activity.</p></div>
    <div class="hero-stat"><small>☀ Solar panel output</small><strong id="hero-solar">--.-- <span style="font-size:14px">V</span></strong><span>Live voltage telemetry</span></div>
  </section>

  <div class="section-head"><div><h3>Station overview</h3><p>Hardware status updates automatically</p></div><span class="pill good" id="updated">WAITING FOR DATA</span></div>
  <section class="grid">
    <article class="card solar">
      <div class="card-top"><div class="card-title"><span class="icon">☀</span>Solar telemetry</div><span class="pill good">LIVE</span></div>
      <div class="metric"><span id="solar-v">0.00</span> <em>V</em></div><div class="sub">Measured solar panel voltage</div>
      <div class="mini-track"><span id="solar-bar"></span></div><div class="info-line"><span>Source</span><strong>Solar panel / divider</strong></div>
    </article>
    <article class="card gate">
      <div class="card-top"><div class="card-title"><span class="icon">↗</span>Entry barrier</div><span id="gate-status" class="pill warn">CLOSED</span></div>
      <div class="metric" id="gate-title">Gate secured</div><div class="sub">RFID-controlled access with manual override</div>
      <div class="actions"><button class="btn btn-primary" onclick="sendCommand('gate_open')">↑ Open gate</button><button class="btn btn-danger" onclick="sendCommand('gate_close')">↓ Close gate</button></div>
    </article>
    <article class="card slot">
      <div class="card-top"><div class="card-title"><span class="icon">▤</span>Parking slot 01</div><span id="s1-occ" class="pill">CHECKING</span></div>
      <div class="slot-state"><div class="slot-symbol">🚘</div><div><strong id="s1-car">Waiting for vehicle</strong><small id="s1-detail">Sensor status loading</small></div></div>
      <div class="info-line"><span>Charging relay</span><strong id="s1-chg" class="pill">STANDBY</strong></div>
      <button class="btn btn-soft" style="width:100%;margin-top:7px" onclick="sendCommand('toggle_chg1')">Toggle charger 1</button>
    </article>
    <article class="card slot">
      <div class="card-top"><div class="card-title"><span class="icon">▤</span>Parking slot 02</div><span id="s2-occ" class="pill">CHECKING</span></div>
      <div class="slot-state"><div class="slot-symbol">🚙</div><div><strong id="s2-car">Waiting for vehicle</strong><small id="s2-detail">Sensor status loading</small></div></div>
      <div class="info-line"><span>Charging relay</span><strong id="s2-chg" class="pill">STANDBY</strong></div>
      <button class="btn btn-soft" style="width:100%;margin-top:7px" onclick="sendCommand('toggle_chg2')">Toggle charger 2</button>
    </article>
  </section>

  <div class="section-head"><div><h3>Driver wallets</h3><p>Individual balances &middot; ₹500 per accepted RFID scan in current firmware</p></div><span class="pill blue">RFID BILLING</span></div>
  <section class="wallet-grid">
    <article class="card wallet"><div class="wallet-head"><div class="avatar">M</div><div><div class="wallet-name">MALIK ABBAS</div><div class="wallet-label">Registered driver</div></div></div><div class="wallet-balance" id="malik-bal">₹25,000.00</div><div class="wallet-foot"><span>Wallet balance</span><span id="malik-state">Ready to scan</span></div></article>
    <article class="card wallet"><div class="wallet-head"><div class="avatar">I</div><div><div class="wallet-name">IRFAN ALI</div><div class="wallet-label">Registered driver</div></div></div><div class="wallet-balance" id="irfan-bal">₹20,000.00</div><div class="wallet-foot"><span>Wallet balance</span><span id="irfan-state">Ready to scan</span></div></article>
    <article class="card wallet"><div class="wallet-head"><div class="avatar">J</div><div><div class="wallet-name">JAFFER SADIQ</div><div class="wallet-label">Access restricted</div></div></div><div class="wallet-balance" id="jaffer-bal">₹15,000.00</div><div class="wallet-foot"><span>Wallet balance</span><span id="jaffer-state">No deduction</span></div></article>
  </section>

  <div class="section-head"><div><h3>Access & billing activity</h3><p>Most recent RFID scan reported by the ESP32</p></div></div>
  <section class="grid">
    <article class="card activity">
      <div class="card-top"><div class="card-title"><span class="icon">▣</span>Latest RFID event</div><span id="access-pill" class="pill">NO SCAN YET</span></div>
      <div class="scan-banner"><div class="scan-icon" id="scan-icon">⌁</div><div style="min-width:0;flex:1"><strong id="last-name">Waiting for card scan</strong><p id="scan-message">Tap a registered RFID card on the reader to show the latest access result.</p></div></div>
      <div class="info-line"><span>Card UID</span><strong class="uid" id="last-uid">None</strong></div>
      <div class="notice">The access result reflects the latest UID/name exposed by the current firmware. For a reliable granted/denied result, the ESP32 should also send an explicit access status field.</div>
    </article>
    <article class="card billing">
      <div class="card-top"><div class="card-title"><span class="icon">₹</span>Latest wallet transaction</div><span class="pill good">DEMO RATE</span></div>
      <div class="sub">Current firmware charge amount</div><div class="billing-amount">₹500.00</div>
      <div class="info-line"><span>Successful authorized scan</span><strong style="color:var(--green)">Deduct ₹500</strong></div>
      <div class="info-line"><span>Unknown / denied card</span><strong style="color:var(--red)">₹0 deducted</strong></div>
      <div class="info-line"><span>Billing mode</span><strong>RFID wallet</strong></div>
      <div class="notice">Balances update from the ESP32 every 2 seconds. The browser does not make or store wallet deductions.</div>
    </article>
  </section>
  <footer class="footer"><span>Smart EV Hub · Smart EV Charging & Parking</span><span>ESP32 edge controller <span id="last-refresh">· Connecting…</span></span></footer>
</div>
<div id="toast" role="status" aria-live="polite"></div>

<script>
const $ = id => document.getElementById(id);
let previousUid = '';
let lastNameSeen = '';
let toastTimer;
function money(v){return '₹'+Number(v||0).toLocaleString('en-IN',{minimumFractionDigits:2,maximumFractionDigits:2});}
function setPill(id,text,type){const el=$(id);if(!el)return;el.textContent=text;el.className='pill'+(type?' '+type:'');}
function toast(message){$('toast').textContent=message;$('toast').classList.add('show');clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('toast').classList.remove('show'),2400);}
async function sendCommand(cmd){
  const buttons=[...document.querySelectorAll('button')];buttons.forEach(b=>b.disabled=true);
  try{const r=await fetch('/control?action='+encodeURIComponent(cmd),{cache:'no-store'});if(!r.ok)throw new Error('Command failed');toast('Command sent to station');await refreshData();}
  catch(e){toast('Could not reach ESP32. Check Wi-Fi connection.');}
  finally{buttons.forEach(b=>b.disabled=false);}
}
function updateSlot(n,occupied,charging){
  const displayOccupied = !occupied;
  setPill('s'+n+'-occ',displayOccupied?'OCCUPIED':'AVAILABLE',displayOccupied?'warn':'good');
  $('s'+n+'-car').textContent=displayOccupied?'Vehicle detected':'Waiting for vehicle';
  $('s'+n+'-detail').textContent=displayOccupied?'Parking sensor detects a vehicle':'Parking space appears clear';
  setPill('s'+n+'-chg',charging?'ACTIVE':'STANDBY',charging?'good':'');
}
function updateScan(data){
  const uid=String(data.uid||'None').trim(),name=String(data.name||'None').trim();
  $('last-uid').textContent=uid||'None';$('last-name').textContent=(name&&name!=='None')?name:'Waiting for card scan';
  const newScan=uid&&uid!=='None'&&uid!==previousUid;
  if(newScan){
    previousUid=uid;lastNameSeen=name;
    const denied=/denied|unknown|jaffer/i.test(name);
    setPill('access-pill',denied?'ACCESS DENIED':'CARD DETECTED',denied?'bad':'good');
    $('scan-icon').textContent=denied?'×':'✓';
    $('scan-message').textContent=denied?'Access not authorized. No wallet deduction should occur.':'Registered card detected. Wallet values below reflect the ESP32-reported balances.';
    if(/Malik/i.test(name))$('malik-state').textContent='Recently scanned';
    if(/Irfan/i.test(name))$('irfan-state').textContent='Recently scanned';
    if(/Jaffer/i.test(name))$('jaffer-state').textContent='Access denied';
  }else if(uid==='None'){
    setPill('access-pill','NO SCAN YET','');
  }
}
async function refreshData(){
  try{
    const response=await fetch('/data',{cache:'no-store'});if(!response.ok)throw new Error('HTTP '+response.status);
    const data=await response.json();
    $('connection').textContent='ESP32 CONNECTED';
    $('solar-v').textContent=Number(data.solar||0).toFixed(2);
    $('hero-solar').innerHTML=Number(data.solar||0).toFixed(2)+' <span style="font-size:14px">V</span>';
    $('solar-bar').style.width=Math.max(0,Math.min(100,(Number(data.solar||0)/15)*100))+'%';
    const gate=!!data.gate;setPill('gate-status',gate?'OPEN':'CLOSED',gate?'good':'warn');
    $('gate-title').textContent=gate?'Gate is open':'Gate secured';
    updateSlot(1,Boolean(data.s1_occ),Boolean(data.s1_chg));
    updateSlot(2,Boolean(data.s2_occ),Boolean(data.s2_chg));
    $('malik-bal').textContent=money(data.malik_bal);
    $('irfan-bal').textContent=money(data.irfan_bal);
    $('jaffer-bal').textContent=money(data.jaffer_bal);
    updateScan(data);
    const now=new Date();$('last-refresh').textContent='· Updated '+now.toLocaleTimeString([], {hour:'2-digit',minute:'2-digit',second:'2-digit'});
    $('updated').textContent='UPDATED JUST NOW';
  }catch(e){
    $('connection').textContent='ESP32 OFFLINE';$('updated').textContent='CONNECTION LOST';
    setPill('updated','CONNECTION LOST','bad');
  }
}
refreshData();setInterval(refreshData,2000);
</script>

</body>
</html>
)rawliteral";


// ==================== WEB HANDLERS ====================
void handleRoot() {
  server.send(200, "text/html", DASHBOARD_HTML);
}

void handleData() {
  String json = "{";
  json += "\"solar\":" + String(solarVoltage, 2) + ",";
  json += "\"gate\":" + String(isGateOpen ? "true" : "false") + ",";
  json += "\"s1_occ\":" + String(isSlot1Occupied ? "true" : "false") + ",";
  json += "\"s1_chg\":" + String(!isChargingActive1 ? "true" : "false") + ","; // Added '!'
  json += "\"s2_occ\":" + String(isSlot2Occupied ? "true" : "false") + ",";
  json += "\"s2_chg\":" + String(!isChargingActive2 ? "true" : "false") + ","; // Added '!'
  json += "\"irfan_bal\":" + String(irfanBalance) + ",";
  json += "\"malik_bal\":" + String(malikBalance) + ",";
  json += "\"jaffer_bal\":" + String(jafferBalance) + ",";
  json += "\"name\":\"" + lastScannedName + "\",";
  json += "\"uid\":\"" + lastScannedUID + "\"";
  json += "}";
  server.send(200, "application/json", json);
}
void handleControl() {
  if (server.hasArg("action")) {
    String action = server.arg("action");
    if (action == "gate_open") {
      gateServo.write(0);     // Swings up into the air to 90 degrees
      isGateOpen = true;
      gateAutoCloseActive = false; 
      beepGateOpen();
    } else if (action == "gate_close") {
      gateServo.write(90);       // Closes back to 90 degrees
      isGateOpen = false;
      gateAutoCloseActive = false;
    } else if (action == "toggle_chg1") {
      isChargingActive1 = !isChargingActive1;
      digitalWrite(RELAY_PIN_1, isChargingActive1 ? HIGH : LOW);
      if(isChargingActive1) chargeStartTime1 = millis();
    } else if (action == "toggle_chg2") {
      isChargingActive2 = !isChargingActive2;
      digitalWrite(RELAY_PIN_2, isChargingActive2 ? HIGH : LOW);
      if(isChargingActive2) chargeStartTime2 = millis();
    }
  }
  server.send(200, "text/plain", "OK");
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);

  // 1. STABILIZE RELAYS, LEDS & BUZZER FIRST (Prevent boot glitching)
  pinMode(RELAY_PIN_1, OUTPUT);
  pinMode(RELAY_PIN_2, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);

  digitalWrite(RELAY_PIN_1, LOW);
  digitalWrite(RELAY_PIN_2, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, LOW);

  // 2. CONFIG SENSORS
  pinMode(IR_PIN_1, INPUT_PULLUP);
  pinMode(IR_PIN_2, INPUT_PULLUP);
  pinMode(SOLAR_PIN, INPUT);

  // 3. ATTACH SERVO (0 degrees = Closed position)
  ESP32PWM::allocateTimer(0);
  gateServo.setPeriodHertz(50);
  gateServo.attach(SERVO_PIN, 500, 2400); 
  gateServo.write(90);     // Start closed at 90 degrees

  // 4. INIT SPI & RFID
  SPI.begin();
  rfc.PCD_Init();
  delay(4); 

  // 5. WIFI CONNECTION
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Connected!");
  Serial.print("Access Dashboard at: http://");
  Serial.println(WiFi.localIP());

  beepSuccess();

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/control", handleControl);
  server.begin();
}

// ==================== LOOP ====================
void loop() {
  server.handleClient();

  // Read Solar Voltage (20k/10k divider ratio multiplier = 3.0)
  int rawADC = analogRead(SOLAR_PIN);
  float pinVolt = (rawADC / 4095.0) * 3.3;
  solarVoltage = pinVolt * ((r_top + r_bottom) / r_bottom);

  // --- IR SENSOR LOGIC (HIGH = Occupied, LOW = Available) ---
  isSlot1Occupied = (digitalRead(IR_PIN_1) == HIGH);
  isSlot2Occupied = (digitalRead(IR_PIN_2) == HIGH);

  // --- SLOT 1: 1s Delay before turning ON, Instant turn OFF ---
  if (isSlot1Occupied) {
    if (!isChargingActive1) {
      if (slot1StartTime == 0) {
        slot1StartTime = millis(); 
        Serial.println("Slot 1 occupied: Starting 1-second delay...");
      } else if (millis() - slot1StartTime >= IR_DELAY_TIME) {
        isChargingActive1 = true;
        chargeStartTime1 = millis();
        digitalWrite(RELAY_PIN_1, HIGH); 
        Serial.println("Slot 1: 1s passed. Relay turned ON!");
      }
    }
  } else {
    slot1StartTime = 0; 
    if (isChargingActive1) {
      isChargingActive1 = false;
      digitalWrite(RELAY_PIN_1, LOW); 
      Serial.println("Slot 1: Car left. Relay turned OFF immediately.");
    }
  }

  // --- SLOT 2: 1s Delay before turning ON, Instant turn OFF ---
  if (isSlot2Occupied) {
    if (!isChargingActive2) {
      if (slot2StartTime == 0) {
        slot2StartTime = millis(); 
        Serial.println("Slot 2 occupied: Starting 1-second delay...");
      } else if (millis() - slot2StartTime >= IR_DELAY_TIME) {
        isChargingActive2 = true;
        chargeStartTime2 = millis();
        digitalWrite(RELAY_PIN_2, HIGH); 
        Serial.println("Slot 2: 1s passed. Relay turned ON!");
      }
    }
  } else {
    slot2StartTime = 0; 
    if (isChargingActive2) {
      isChargingActive2 = false;
      digitalWrite(RELAY_PIN_2, LOW); 
      Serial.println("Slot 2: Car left. Relay turned OFF immediately.");
    }
  }

  // Automatic Gate Close Timer (3 seconds delay, returns to 0 degrees)
  if (gateAutoCloseActive && (millis() - gateOpenTimestamp >= GATE_OPEN_DURATION)) {
    gateServo.write(90);   // Close gate back to 90 degrees
    isGateOpen = false;
    gateAutoCloseActive = false;
    Serial.println("Gate closed automatically.");
  }

  // RFID Card Processing & User Wallet Deduction
  if (rfc.PICC_IsNewCardPresent() && rfc.PICC_ReadCardSerial()) {
    lastScannedUID = "";
    for (byte i = 0; i < rfc.uid.size; i++) {
      lastScannedUID += String(rfc.uid.uidByte[i] < 0x10 ? " 0" : " ");
      lastScannedUID += String(rfc.uid.uidByte[i], HEX);
    }
    lastScannedUID.toUpperCase();
    lastScannedUID.trim();
    Serial.println("Scanned UID: " + lastScannedUID);

    // Check User: M A L I K ("90 A7 8A 54") or I R F A N ("49 48 9A B7")
    if (lastScannedUID == "49 48 9A B7" || lastScannedUID == "E4 ED 32 07") {
      lastScannedName = (lastScannedUID == "49 48 9A B7") ? "Irfan" : "Malik";
      beepSuccess(); 
      
      digitalWrite(GREEN_LED_PIN, HIGH);

      bool walletSuccess = false;
      if (lastScannedName == "Irfan" && irfanBalance >= CHARGE_COST) {
        irfanBalance -= CHARGE_COST;
        walletSuccess = true;
      } else if (lastScannedName == "Malik" && malikBalance >= CHARGE_COST) {
        malikBalance -= CHARGE_COST;
        walletSuccess = true;
      }

      if (walletSuccess) {
        Serial.println("₹500 deducted from " + lastScannedName + "'s wallet.");
      } else {
        Serial.println(lastScannedName + " has insufficient wallet balance!");
      }

      gateServo.write(0);     // Swings up into the air to 180 degrees
      isGateOpen = true;
      gateOpenTimestamp = millis();
      gateAutoCloseActive = true;
      beepGateOpen();
      
    } else {
      lastScannedName = (lastScannedUID == "57 3C 5A 64") ? "Jaffer (Denied)" : "Unknown";
      beepDenied();
      Serial.println("Access Denied for: " + lastScannedName + " [UID: " + lastScannedUID + "]");
      
      digitalWrite(RED_LED_PIN, HIGH);
    }

    // Keep status LED active for 3 seconds, then turn both OFF
    delay(3000); 
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, LOW);

    rfc.PICC_HaltA();
    rfc.PCD_StopCrypto1();
  }

  delay(50);
}
