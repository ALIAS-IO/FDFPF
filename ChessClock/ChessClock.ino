#include <Arduino.h>
#define USE_SERIAL_1602_LCD
#include "LCDBigNumbers.hpp"
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <ESPmDNS.h>

// ================= WIFI AP CONFIG =================
const char* AP_SSID = "ChessTimer";
const char* AP_PASS = "chess123";

WebServer server(80);

// ================= DISPLAY =================
LiquidCrystal_I2C lcd1(0x26, LCD_COLUMNS, LCD_ROWS);
LiquidCrystal_I2C lcd2(0x27, LCD_COLUMNS, LCD_ROWS);

LCDBigNumbers big1(&lcd1, BIG_NUMBERS_FONT_1_COLUMN_2_ROWS_VARIANT_1);
LCDBigNumbers big2(&lcd2, BIG_NUMBERS_FONT_1_COLUMN_2_ROWS_VARIANT_1);

// ================= BUTTONS =================
// Port 3  (BTN_P1)      — Controls LCD1 (end p1 turn / select white for LCD1)
// Port 10 (BTN_P2)      — Controls LCD2 (end p2 turn / select white for LCD2)
// Port 5  (BTN_POWER)   — Hold 2.5s: on/off | Short press: select/confirm preset (menu) | start clock (startup)
// Port 4  (BTN_MENU)    — Hold 2.5s: open preset menu | Short press inside menu: cycle left
// Port 8  (BTN_FORWARD) — Hold 2.5s: show web UI reminder + unlock web timing
#define BTN_P1      3
#define BTN_POWER   5
#define BTN_MENU    4
#define BTN_FORWARD 8
#define BTN_P2      10

// ================= STATE =================
bool clockOn = false;
bool running = false;
bool player1Turn = true;
bool inMenu = false;
bool p1IsWhite = true;
bool paused = false;

unsigned long lastTick = 0;

// ================= TIME =================
unsigned long p1Time = 0;
unsigned long p2Time = 0;
int incrementSeconds = 0;

// ================= PRESETS =================
struct Preset { int hours; int minutes; int seconds; int increment; };
Preset presets[] = {
  {0,  1, 0,  0},  // 1 min
  {0, 10, 0,  0},  // 10 min
  {0, 30, 0,  0},  // 30 min
  {0,  3, 2,  2},  // 3|2
  {0,  5, 3,  3},  // 5|3
  {0, 15, 10, 10},  // 15|10
  {1, 30, 30, 30},  // 90|30
};
const int NUM_PRESETS = sizeof(presets)/sizeof(Preset);
int presetIndex = 4;

// ================= BUTTON STATES =================
bool prevP1      = HIGH;
bool prevP2      = HIGH;
bool prevPower   = HIGH;
bool prevMenu    = HIGH;
bool prevForward = HIGH;

// ================= HOLD TIMERS =================
unsigned long powerHoldStart   = 0;
unsigned long menuHoldStart    = 0;
unsigned long forwardHoldStart = 0;

// ================= FLAGS =================
bool presetConfirmed  = false;
bool showingReminder  = false;
bool webUnlocked      = false;
#define ONE_COLUMN_SPACE_CHARACTER ' '

// ================= REMINDER SCREEN STATE =================
unsigned long lastReminderSwitch = 0;
bool reminderShowingPass         = false;

// ================= BIG W AND L (2 wide x 2 tall) =================
byte W_TL[8] = { 0b11001, 0b11001, 0b11001, 0b11001, 0b11001, 0b11001, 0b11001, 0b11001 };
byte W_BL[8] = { 0b11001, 0b11001, 0b11001, 0b11001, 0b11001, 0b11001, 0b11111, 0b11111 };
byte W_TR[8] = { 0b10011, 0b10011, 0b10011, 0b10011, 0b10011, 0b10011, 0b10011, 0b10011 };
byte W_BR[8] = { 0b10011, 0b10011, 0b10011, 0b10011, 0b10011, 0b10011, 0b11111, 0b11111 };
byte L_TL[8] = { 0b11000, 0b11000, 0b11000, 0b11000, 0b11000, 0b11000, 0b11000, 0b11000 };
byte L_BL[8] = { 0b11000, 0b11000, 0b11000, 0b11000, 0b11000, 0b11000, 0b11111, 0b11111 };
byte L_TR[8] = { 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000 };
byte L_BR[8] = { 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b11111, 0b11111 };

#define CHAR_W_TL 0
#define CHAR_W_BL 1
#define CHAR_W_TR 2
#define CHAR_W_BR 3
#define CHAR_L_TL 4
#define CHAR_L_BL 5
#define CHAR_L_TR 6
#define CHAR_L_BR 7

// ================= FORWARD DECLARATIONS =================
void drawStartupScreen();
void drawPresetScreen();
void drawTimes();
void applyPreset();
void drawTime(LCDBigNumbers &big, LiquidCrystal_I2C &lcd, unsigned long totalSeconds, int startColumn);
void drawWinnerScreen(bool p1Wins);
void resetToPreset();
void loadWinnerChars(LiquidCrystal_I2C &lcd);
void printBigW(LiquidCrystal_I2C &lcd, int col);
void printBigL(LiquidCrystal_I2C &lcd, int col);
void handlePower();
void handleMenu();
void updateClock();
void handleTurns();
void handleStartupPlayerSelection();
void checkPresetEntry();
void checkStartClock();
void checkWebUIReminder();
void drawReminderScreen(bool showPass);
void updateReminderScreen();

// ================= READ BUTTON EDGES =================
bool buttonPressed(bool &prevState, int pin) {
  bool current = digitalRead(pin);
  if (prevState == HIGH && current == LOW) {
    prevState = LOW;
    return true;
  }
  if (current == HIGH) prevState = HIGH;
  return false;
}

// ================= WEB SERVER HTML =================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Chess Clock Config</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: 'Segoe UI', sans-serif;
      background: #1a1a2e;
      color: #eee;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      min-height: 100vh;
      padding: 20px;
    }
    h1 { font-size: 1.8rem; margin-bottom: 8px; color: #e2c97e; letter-spacing: 2px; }
    p.subtitle { color: #888; margin-bottom: 32px; font-size: 0.9rem; }
    .card {
      background: #16213e;
      border: 1px solid #0f3460;
      border-radius: 16px;
      padding: 28px;
      width: 100%;
      max-width: 480px;
      margin-bottom: 16px;
    }
    .card h2 { font-size: 1rem; color: #e2c97e; margin-bottom: 20px; text-transform: uppercase; letter-spacing: 1px; }
    .fields { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 12px; }
    .fields-4 { display: grid; grid-template-columns: 1fr 1fr 1fr 1fr; gap: 12px; }
    .field label { display: block; font-size: 0.75rem; color: #888; margin-bottom: 6px; text-transform: uppercase; letter-spacing: 1px; }
    .field input {
      width: 100%;
      background: #0f3460;
      border: 1px solid #1a4a8a;
      border-radius: 8px;
      color: #fff;
      font-size: 1.4rem;
      padding: 10px;
      text-align: center;
      outline: none;
      transition: border-color 0.2s;
    }
    .field input:focus { border-color: #e2c97e; }
    .field input:disabled { opacity: 0.4; }
    .field input.error { border-color: #f44336; }
    .same-time { display: flex; align-items: center; gap: 10px; margin-top: 16px; font-size: 0.85rem; color: #888; cursor: pointer; }
    .same-time input[type=checkbox] { width: 16px; height: 16px; accent-color: #e2c97e; cursor: pointer; }
    button {
      width: 100%;
      padding: 14px;
      border-radius: 10px;
      border: none;
      font-size: 1rem;
      font-weight: 600;
      cursor: pointer;
      letter-spacing: 1px;
      background: #e2c97e;
      color: #1a1a2e;
      transition: opacity 0.2s, transform 0.1s;
    }
    button:active { transform: scale(0.97); }
    button:disabled { opacity: 0.4; cursor: not-allowed; }
    #status { margin-top: 16px; font-size: 0.85rem; text-align: center; min-height: 20px; color: #888; }
    #status.ok  { color: #4caf50; }
    #status.err { color: #f44336; }
    .presets { display: grid; grid-template-columns: repeat(4, 1fr); gap: 8px; margin-bottom: 4px; }
    .preset-btn {
      padding: 10px 4px;
      background: #0f3460;
      color: #e2c97e;
      border: 1px solid #1a4a8a;
      border-radius: 8px;
      font-size: 0.78rem;
      cursor: pointer;
      text-align: center;
      transition: background 0.2s;
      line-height: 1.4;
    }
    .preset-btn:hover { background: #1a4a8a; }
    .preset-btn .inc { font-size: 0.65rem; color: #aaa; display: block; }
    .inc-note { font-size: 0.75rem; color: #888; margin-top: 8px; }
    #lockMsg { text-align: center; color: #e2c97e; font-size: 0.85rem; margin-bottom: 12px; display: none; }
  </style>
</head>
<body>
  <h1>&#9822; Chess Timer</h1>
  <p class="subtitle">Join <strong>ChessTimer</strong> WiFi then visit <strong>chesstimer.local</strong></p>

  <div class="card">
    <h2>Quick Presets</h2>
    <div class="presets">
      <div class="preset-btn" onclick="setPreset(0,1,0,0,0,1,0,0)">1 min<span class="inc">no inc</span></div>
      <div class="preset-btn" onclick="setPreset(0,10,0,0,0,10,0,0)">10 min<span class="inc">no inc</span></div>
      <div class="preset-btn" onclick="setPreset(0,30,0,0,0,30,0,0)">30 min<span class="inc">no inc</span></div>
      <div class="preset-btn" onclick="setPreset(0,3,0,2,0,3,0,2)">3 | 2<span class="inc">+2s inc</span></div>
      <div class="preset-btn" onclick="setPreset(0,5,0,3,0,5,0,3)">5 | 3<span class="inc">+3s inc</span></div>
      <div class="preset-btn" onclick="setPreset(0,15,0,10,0,15,0,10)">15 | 10<span class="inc">+10s inc</span></div>
      <div class="preset-btn" onclick="setPreset(1,30,0,30,1,30,0,30)">90 | 30<span class="inc">+30s inc</span></div>
    </div>
    <p class="inc-note">Format: <strong>time | increment</strong> &mdash; e.g. 3 | 2 = 3 min + 2s per move</p>
  </div>

  <div class="card">
    <h2>&#11014; Player 1 (White)</h2>
    <div class="fields-4">
      <div class="field"><label>Hours</label><input type="number" id="p1h" min="0" max="23" value="0"></div>
      <div class="field"><label>Minutes</label><input type="number" id="p1m" min="0" max="59" value="5"></div>
      <div class="field"><label>Seconds</label><input type="number" id="p1s" min="0" max="59" value="0"></div>
      <div class="field"><label>+Inc (s)</label><input type="number" id="inc1" min="0" max="300" value="0"></div>
    </div>
  </div>

  <div class="card">
    <h2>&#11015; Player 2 (Black)</h2>
    <div class="fields-4">
      <div class="field"><label>Hours</label><input type="number" id="p2h" min="0" max="23" value="0"></div>
      <div class="field"><label>Minutes</label><input type="number" id="p2m" min="0" max="59" value="5"></div>
      <div class="field"><label>Seconds</label><input type="number" id="p2s" min="0" max="59" value="0"></div>
      <div class="field"><label>+Inc (s)</label><input type="number" id="inc2" min="0" max="300" value="0"></div>
    </div>
    <label class="same-time">
      <input type="checkbox" id="sameTime" checked onchange="syncTimes()">
      Same time as Player 1
    </label>
  </div>

  <div class="card">
    <div id="lockMsg">&#128274; Hold the web button on the clock first</div>
    <button id="btnSend" onclick="sendTiming()">Send to Clock</button>
    <div id="status">Checking clock status...</div>
  </div>

  <script>
    let unlocked = false;
    let statusInterval = null;

    async function checkStatus() {
      try {
        const res = await fetch('/status');
        const text = await res.text();
        unlocked = text === 'unlocked';
        updateLockUI();
      } catch(e) {
        setStatus('Cannot reach clock.', 'err');
      }
    }

    function updateLockUI() {
      const btn = document.getElementById('btnSend');
      const msg = document.getElementById('lockMsg');
      if (unlocked) {
        btn.disabled = false;
        msg.style.display = 'none';
        setStatus('Ready', '');
      } else {
        btn.disabled = true;
        msg.style.display = 'block';
        setStatus('', '');
      }
    }

    ['p1h','p1m','p1s','inc1'].forEach(id => {
      document.getElementById(id).addEventListener('input', () => {
        if (document.getElementById('sameTime').checked) syncTimes();
      });
    });

    function syncTimes() {
      const checked = document.getElementById('sameTime').checked;
      if (checked) {
        document.getElementById('p2h').value  = document.getElementById('p1h').value;
        document.getElementById('p2m').value  = document.getElementById('p1m').value;
        document.getElementById('p2s').value  = document.getElementById('p1s').value;
        document.getElementById('inc2').value = document.getElementById('inc1').value;
      }
      document.getElementById('p2h').disabled  = checked;
      document.getElementById('p2m').disabled  = checked;
      document.getElementById('p2s').disabled  = checked;
      document.getElementById('inc2').disabled = checked;
    }

    function setPreset(p1h, p1m, p1s, i1, p2h, p2m, p2s, i2) {
      document.getElementById('p1h').value  = p1h;
      document.getElementById('p1m').value  = p1m;
      document.getElementById('p1s').value  = p1s;
      document.getElementById('inc1').value = i1;
      document.getElementById('p2h').value  = p2h;
      document.getElementById('p2m').value  = p2m;
      document.getElementById('p2s').value  = p2s;
      document.getElementById('inc2').value = i2;
      syncTimes();
    }

    function clamp(v, mn, mx) { return Math.min(Math.max(parseInt(v) || 0, mn), mx); }

    function validate() {
      let ok = true;
      const limits = { p1h:[0,23], p1m:[0,59], p1s:[0,59], inc1:[0,300],
                       p2h:[0,23], p2m:[0,59], p2s:[0,59], inc2:[0,300] };
      Object.entries(limits).forEach(([id, [mn, mx]]) => {
        const el = document.getElementById(id);
        const v = parseInt(el.value) || 0;
        if (v < mn || v > mx) { el.classList.add('error'); ok = false; }
        else el.classList.remove('error');
      });
      const p1total = clamp(document.getElementById('p1h').value,0,23)*3600
                    + clamp(document.getElementById('p1m').value,0,59)*60
                    + clamp(document.getElementById('p1s').value,0,59);
      const p2total = clamp(document.getElementById('p2h').value,0,23)*3600
                    + clamp(document.getElementById('p2m').value,0,59)*60
                    + clamp(document.getElementById('p2s').value,0,59);
      if (p1total === 0) { setStatus('Player 1 time cannot be zero.', 'err'); ok = false; }
      else if (p2total === 0) { setStatus('Player 2 time cannot be zero.', 'err'); ok = false; }
      return ok;
    }

    async function sendTiming() {
      if (!unlocked) { setStatus('Hold the web button on the clock first.', 'err'); return; }
      if (!validate()) return;
      const p1h  = clamp(document.getElementById('p1h').value,  0, 23);
      const p1m  = clamp(document.getElementById('p1m').value,  0, 59);
      const p1s  = clamp(document.getElementById('p1s').value,  0, 59);
      const inc1 = clamp(document.getElementById('inc1').value, 0, 300);
      const p2h  = clamp(document.getElementById('p2h').value,  0, 23);
      const p2m  = clamp(document.getElementById('p2m').value,  0, 59);
      const p2s  = clamp(document.getElementById('p2s').value,  0, 59);
      const inc2 = clamp(document.getElementById('inc2').value, 0, 300);
      try {
        const res = await fetch('/set', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: `p1h=${p1h}&p1m=${p1m}&p1s=${p1s}&inc1=${inc1}&p2h=${p2h}&p2m=${p2m}&p2s=${p2s}&inc2=${inc2}`
        });
        const text = await res.text();
        if (text === 'OK') {
          clearInterval(statusInterval);
          statusInterval = null;
          document.getElementById('lockMsg').style.display = 'none';
          document.getElementById('btnSend').disabled = false;
          setStatus('&#10003; Timing sent! Clock is ready to start.', 'ok');
        } else {
          setStatus('Error: ' + text, 'err');
        }
      } catch(e) {
        setStatus('Failed to reach clock: ' + e.message, 'err');
      }
    }

    function setStatus(msg, cls) {
      const el = document.getElementById('status');
      el.innerHTML = msg;
      el.className = cls;
    }

    syncTimes();
    checkStatus();
    statusInterval = setInterval(checkStatus, 2000);
  </script>
</body>
</html>
)rawliteral";

// ================= WEB SERVER HANDLERS =================
void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  server.send(200, "text/plain", webUnlocked ? "unlocked" : "locked");
}

void handleSet() {
  if (!webUnlocked) { server.send(403, "text/plain", "Hold BTN8 on the clock first"); return; }
  if (!server.hasArg("p1h")) { server.send(400, "text/plain", "Bad request"); return; }

  int p1h = constrain(server.arg("p1h").toInt(), 0, 23);
  int p1m = constrain(server.arg("p1m").toInt(), 0, 59);
  int p1s = constrain(server.arg("p1s").toInt(), 0, 59);
  int p2h = constrain(server.arg("p2h").toInt(), 0, 23);
  int p2m = constrain(server.arg("p2m").toInt(), 0, 59);
  int p2s = constrain(server.arg("p2s").toInt(), 0, 59);
  int inc = constrain(server.arg("inc").toInt(), 0, 300);

  unsigned long t1 = p1h * 3600UL + p1m * 60UL + p1s;
  unsigned long t2 = p2h * 3600UL + p2m * 60UL + p2s;

  if (t1 == 0) { server.send(400, "text/plain", "Player 1 time cannot be zero"); return; }
  if (t2 == 0) { server.send(400, "text/plain", "Player 2 time cannot be zero"); return; }

  p1Time = t1;
  p2Time = t2;
  incrementSeconds = inc;
  presetConfirmed = true;
  showingReminder  = false;
  webUnlocked      = false;
  running = false;
  prevP1 = HIGH; prevP2 = HIGH;
  big1.begin(); big2.begin();
  drawStartupScreen();

  server.send(200, "text/plain", "OK");
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  pinMode(BTN_P1,      INPUT_PULLUP);
  pinMode(BTN_P2,      INPUT_PULLUP);
  pinMode(BTN_POWER,   INPUT_PULLUP);
  pinMode(BTN_MENU,    INPUT_PULLUP);
  pinMode(BTN_FORWARD, INPUT_PULLUP);

  Wire.begin(6, 7);
  Wire.setTimeout(250);

  lcd1.init(); lcd1.clear(); lcd1.noBacklight(); big1.begin();
  lcd2.init(); lcd2.clear(); lcd2.noBacklight(); big2.begin();

  WiFi.softAP(AP_SSID, AP_PASS);
  delay(100);
  MDNS.begin("chesstimer"); // http://chesstimer.local

  server.on("/",       HTTP_GET,  handleRoot);
  server.on("/status", HTTP_GET,  handleStatus);
  server.on("/set",    HTTP_POST, handleSet);
  server.begin();

  Serial.println("AP: " + String(AP_SSID));
  Serial.println("IP: " + WiFi.softAPIP().toString());
  Serial.println("URL: http://chesstimer.local");
}

// ================= LOOP =================
void loop() {
  server.handleClient();

  handlePower();

  if (!clockOn) return;

  if (showingReminder) {
    updateReminderScreen();
    return;
  }

  if (inMenu) {
    handleMenu();
  } else if (running) {
    updateClock();
    handleTurns();
  } else {
    handleStartupPlayerSelection();
    checkPresetEntry();
    checkWebUIReminder();
    checkStartClock();
  }
}

// ================= POWER HANDLING =================
void handlePower() {
  if (digitalRead(BTN_POWER) == LOW) {
    if (powerHoldStart == 0) powerHoldStart = millis();
    if (millis() - powerHoldStart > 2500) {
      powerHoldStart = 0;
      prevPower = HIGH;
      if (!clockOn) {
        lcd1.init(); lcd1.backlight(); big1.begin();
        lcd2.init(); lcd2.backlight(); big2.begin();
        p1Time = 0; p2Time = 0;
        running = false;
        player1Turn = true;
        inMenu = false;
        presetConfirmed = false;
        showingReminder = false;
        webUnlocked     = false;
        clockOn = true;
        drawStartupScreen();
      } else {
        running = false; inMenu = false; clockOn = false;
        showingReminder = false;
        webUnlocked     = false;
        lcd1.clear(); lcd1.noBacklight();
        lcd2.clear(); lcd2.noBacklight();
      }
    }
  } else {
    powerHoldStart = 0;
  }
}

// ================= STARTUP PLAYER SELECTION =================
void handleStartupPlayerSelection() {
  if (!running && !inMenu) {
    if (buttonPressed(prevP1, BTN_P1)) { p1IsWhite = true;  drawStartupScreen(); }
    if (buttonPressed(prevP2, BTN_P2)) { p1IsWhite = false; drawStartupScreen(); }
  }
}

// ================= CHECK PRESET ENTRY =================
void checkPresetEntry() {
  unsigned long now = millis();
  if (digitalRead(BTN_MENU) == LOW) {
    if (menuHoldStart == 0) menuHoldStart = now;
    if (now - menuHoldStart > 2500) {
      menuHoldStart = 0;
      prevMenu = HIGH;
      inMenu = true;
      presetConfirmed = false;
      drawPresetScreen();
    }
  } else {
    menuHoldStart = 0;
  }
}

// ================= WEB UI REMINDER =================
void checkWebUIReminder() {
  unsigned long now = millis();
  if (digitalRead(BTN_FORWARD) == LOW) {
    if (forwardHoldStart == 0) forwardHoldStart = now;
    if (now - forwardHoldStart > 2500) {
      forwardHoldStart = 0;
      prevForward = HIGH;
      showingReminder      = true;
      webUnlocked          = true;
      reminderShowingPass  = false;
      lastReminderSwitch   = millis();
      drawReminderScreen(false);
    }
  } else {
    forwardHoldStart = 0;
  }
}

// ================= DRAW REMINDER SCREEN =================
void drawReminderScreen(bool showPass) {
  lcd1.clear(); lcd2.clear();
  lcd1.setCursor(0, 0); lcd1.print("WiFi:");
  lcd1.setCursor(0, 1); lcd1.print(AP_SSID);
  if (!showPass) {
    lcd2.setCursor(0, 0); lcd2.print("Password:");
    lcd2.setCursor(0, 1); lcd2.print(AP_PASS);
  } else {
    lcd2.setCursor(0, 0); lcd2.print("Then visit:");
    lcd2.setCursor(0, 1); lcd2.print("chesstimer.local");
  }
}

// ================= UPDATE REMINDER SCREEN =================
void updateReminderScreen() {
  unsigned long now = millis();
  if (now - lastReminderSwitch > 3000) {
    lastReminderSwitch   = now;
    reminderShowingPass  = !reminderShowingPass;
    drawReminderScreen(reminderShowingPass);
  }
}

// ================= MENU HANDLING =================
void handleMenu() {
  if (buttonPressed(prevForward, BTN_FORWARD)) {
    presetIndex = (presetIndex + 1) % NUM_PRESETS;
    presetConfirmed = false;
    drawPresetScreen();
  }
  if (buttonPressed(prevMenu, BTN_MENU)) {
    presetIndex = (presetIndex - 1 + NUM_PRESETS) % NUM_PRESETS;
    presetConfirmed = false;
    drawPresetScreen();
  }
  if (buttonPressed(prevPower, BTN_POWER)) {
    if (!presetConfirmed) {
      presetConfirmed = true;
      applyPreset();
      drawPresetScreen();
    } else {
      inMenu = false;
      running = false;
      drawStartupScreen();
    }
  }
}

// ================= DRAW PRESET SCREEN =================
void drawPresetScreen() {
  lcd1.clear();
  lcd2.clear();

  Preset p = presets[presetIndex];

  big1.setBigNumberCursor(4);
  big1.print(p.hours); big1.print(ONE_COLUMN_SPACE_CHARACTER);
  if (p.minutes < 10) big1.print('0'); big1.print(p.minutes); big1.print(ONE_COLUMN_SPACE_CHARACTER);
  if (p.seconds < 10) big1.print('0'); big1.print(p.seconds);

  if (p.increment > 0) {
    lcd1.setCursor(0, 0); lcd1.print("+"); lcd1.print(p.increment); lcd1.print("s");
  }

  big2.setBigNumberCursor(5);
  if (presetConfirmed) {
    big2.print(p.hours); big2.print(ONE_COLUMN_SPACE_CHARACTER);
    if (p.minutes < 10) big2.print('0'); big2.print(p.minutes); big2.print(ONE_COLUMN_SPACE_CHARACTER);
    if (p.seconds < 10) big2.print('0'); big2.print(p.seconds);
    if (p.increment > 0) {
      lcd2.setCursor(0, 0); lcd2.print("+"); lcd2.print(p.increment); lcd2.print("s");
    }
  } else {
    unsigned long h = p1Time / 3600;
    int m = (p1Time % 3600) / 60;
    int s = p1Time % 60;
    big2.print(h); big2.print(ONE_COLUMN_SPACE_CHARACTER);
    if (m < 10) big2.print('0'); big2.print(m); big2.print(ONE_COLUMN_SPACE_CHARACTER);
    if (s < 10) big2.print('0'); big2.print(s);
  }
}

// ================= APPLY PRESET =================
void applyPreset() {
  Preset p = presets[presetIndex];
  p1Time = p2Time = p.hours * 3600UL + p.minutes * 60UL + p.seconds;
  incrementSeconds = p.increment;
}

// ================= STARTUP SCREEN =================
void drawStartupScreen() {
  lcd1.clear(); lcd2.clear();
  drawTimes();
  if (p1IsWhite) {
    lcd1.setCursor(15, 1); lcd1.print("W");
    lcd2.setCursor(0,  1); lcd2.print("B");
  } else {
    lcd1.setCursor(15, 1); lcd1.print("B");
    lcd2.setCursor(0,  1); lcd2.print("W");
  }
}

// ================= START CLOCK =================
void checkStartClock() {
  if (!presetConfirmed || running) return;
  if (buttonPressed(prevPower, BTN_POWER)) {
    running = true;
    player1Turn = p1IsWhite;
    lastTick = millis();
    prevP1 = HIGH;
    prevP2 = HIGH;
    drawTimes();
  }
}

// ================= CLOCK =================
void updateClock() {
  unsigned long now = millis();
  if (now - lastTick >= 1000 && !paused) {
    lastTick = now;
    if (player1Turn  && p1Time > 0) p1Time--;
    if (!player1Turn && p2Time > 0) p2Time--;
    if (p1Time == 0 && player1Turn)  { drawWinnerScreen(false); delay(5000); resetToPreset(); return; }
    if (p2Time == 0 && !player1Turn) { drawWinnerScreen(true);  delay(5000); resetToPreset(); return; }
    drawTimes();
  }
}

// ================= HANDLE TURNS =================
void handleTurns() {
  if (buttonPressed(prevPower, BTN_POWER)) {
      paused = !paused;
      if (!paused) {
        lastTick = millis();
        drawTimes();
      }
    }
  bool p1Pressed = buttonPressed(prevP1, BTN_P1);
  bool p2Pressed = buttonPressed(prevP2, BTN_P2);
  if (p1Pressed && player1Turn)  { player1Turn = false; if (incrementSeconds > 0) p1Time += incrementSeconds; drawTimes(); }
  if (p2Pressed && !player1Turn) { player1Turn = true;  if (incrementSeconds > 0) p2Time += incrementSeconds; drawTimes(); }
}

// ================= WINNER SCREEN =================
void loadWinnerChars(LiquidCrystal_I2C &lcd) {
  lcd.createChar(CHAR_W_TL, W_TL); lcd.createChar(CHAR_W_BL, W_BL);
  lcd.createChar(CHAR_W_TR, W_TR); lcd.createChar(CHAR_W_BR, W_BR);
  lcd.createChar(CHAR_L_TL, L_TL); lcd.createChar(CHAR_L_BL, L_BL);
  lcd.createChar(CHAR_L_TR, L_TR); lcd.createChar(CHAR_L_BR, L_BR);
}

void printBigW(LiquidCrystal_I2C &lcd, int col) {
  lcd.setCursor(col, 0); lcd.write(byte(CHAR_W_TL)); lcd.write(byte(CHAR_W_TR));
  lcd.setCursor(col, 1); lcd.write(byte(CHAR_W_BL)); lcd.write(byte(CHAR_W_BR));
}

void printBigL(LiquidCrystal_I2C &lcd, int col) {
  lcd.setCursor(col, 0); lcd.write(byte(CHAR_L_TL)); lcd.write(byte(CHAR_L_TR));
  lcd.setCursor(col, 1); lcd.write(byte(CHAR_L_BL)); lcd.write(byte(CHAR_L_BR));
}

void drawWinnerScreen(bool p1Wins) {
  lcd1.clear(); lcd2.clear();
  loadWinnerChars(lcd1); loadWinnerChars(lcd2);
  if (p1Wins) { printBigW(lcd1, 7); printBigL(lcd2, 7); }
  else        { printBigL(lcd1, 7); printBigW(lcd2, 7); }
}

// ================= RESET TO PRESET =================
void resetToPreset() {
  applyPreset();
  running = false;
  player1Turn = p1IsWhite;
  prevP1 = HIGH; prevP2 = HIGH;
  big1.begin(); big2.begin();
  drawStartupScreen();
}

// ================= DRAW TIME =================
void drawTime(LCDBigNumbers &big, LiquidCrystal_I2C &lcd, unsigned long totalSeconds, int startColumn) {
  unsigned long hours   = totalSeconds / 3600;
  unsigned long minutes = (totalSeconds % 3600) / 60;
  unsigned long seconds = totalSeconds % 60;
  big.setBigNumberCursor(startColumn);
  big.print(hours); big.print(ONE_COLUMN_SPACE_CHARACTER);
  if (minutes < 10) big.print('0'); big.print(minutes); big.print(ONE_COLUMN_SPACE_CHARACTER);
  if (seconds < 10) big.print('0'); big.print(seconds);
}

// ================= DRAW TIMES =================
void drawTimes() {
  drawTime(big1, lcd1, p1Time, 4);
  drawTime(big2, lcd2, p2Time, 5);
}