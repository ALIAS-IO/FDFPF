#include <Arduino.h>
#define USE_SERIAL_1602_LCD
#include "LCDBigNumbers.hpp"

// ================= DISPLAY =================
LiquidCrystal_I2C lcd1(0x26, LCD_COLUMNS, LCD_ROWS);
LiquidCrystal_I2C lcd2(0x27, LCD_COLUMNS, LCD_ROWS);

LCDBigNumbers big1(&lcd1, BIG_NUMBERS_FONT_1_COLUMN_2_ROWS_VARIANT_1);
LCDBigNumbers big2(&lcd2, BIG_NUMBERS_FONT_1_COLUMN_2_ROWS_VARIANT_1);

// ================= BUTTONS =================
// Port 3  (BTN_P1)      — Controls LCD1 (end p1 turn / select white for LCD1)
// Port 10 (BTN_P2)      — Controls LCD2 (end p2 turn / select white for LCD2)
// Port 4  (BTN_POWER)   — Hold 2.5s: on/off | Short press: select/confirm preset (menu) | start clock (startup)
// Port 5  (BTN_MENU)    — Hold 2.5s: open preset menu | Short press inside menu: cycle left
// Port 8  (BTN_FORWARD) — Short press inside menu: cycle right
#define BTN_P1      3
#define BTN_POWER   4
#define BTN_MENU    5
#define BTN_FORWARD 8
#define BTN_P2      10

// ================= STATE =================
bool clockOn = false;
bool running = false;
bool player1Turn = true;
bool inMenu = false;
bool p1IsWhite = true;

unsigned long lastTick = 0;

// ================= TIME =================
unsigned long p1Time = 0;
unsigned long p2Time = 0;
int incrementSeconds = 0;

// ================= PRESETS =================
struct Preset { int hours; int minutes; int seconds; int increment; };
Preset presets[] = {
  {0,1,0,0}, {0,3,0,0}, {0,5,0,0}, {0,10,0,0}, {0,15,10,0}, {0,30,0,0}, {1,30,0,0}
};
const int NUM_PRESETS = sizeof(presets)/sizeof(Preset);
int presetIndex = 4;

// ================= BUTTON DEBOUNCE =================
const unsigned long debounceTime = 150;
unsigned long lastMenuPress    = 0;
unsigned long lastForwardPress = 0;
unsigned long lastP1Press      = 0;
unsigned long lastP2Press      = 0;
unsigned long lastPowerPress   = 0;

// ================= PREVIOUS BUTTON STATES =================
bool prevP1      = HIGH;
bool prevP2      = HIGH;
bool prevPower   = HIGH;
bool prevMenu    = HIGH;
bool prevForward = HIGH;

// ================= HOLD TIMERS =================
unsigned long powerHoldStart = 0;
unsigned long menuHoldStart  = 0;

// ================= FLAGS =================
bool presetConfirmed = false;
#define ONE_COLUMN_SPACE_CHARACTER ' '

// ================= BIG W AND L (1 wide x 2 tall) =================
// W - left half
byte W_TL[8] = { 0b11001, 0b11001, 0b11001, 0b11001, 0b11001, 0b11001, 0b11001, 0b11001 };
byte W_BL[8] = { 0b11001, 0b11001, 0b11001, 0b11001, 0b11001, 0b11001, 0b11111, 0b11111 };

// W - right half
byte W_TR[8] = { 0b10011, 0b10011, 0b10011, 0b10011, 0b10011, 0b10011, 0b10011, 0b10011 };
byte W_BR[8] = { 0b10011, 0b10011, 0b10011, 0b10011, 0b10011, 0b10011, 0b11111, 0b11111 };

// L - left half
byte L_TL[8] = { 0b11000, 0b11000, 0b11000, 0b11000, 0b11000, 0b11000, 0b11000, 0b11000 };
byte L_BL[8] = { 0b11000, 0b11000, 0b11000, 0b11000, 0b11000, 0b11000, 0b11111, 0b11111 };

// L - right half (blank top, solid bottom)
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

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  pinMode(BTN_P1,      INPUT_PULLUP);
  pinMode(BTN_P2,      INPUT_PULLUP);
  pinMode(BTN_POWER,   INPUT_PULLUP);
  pinMode(BTN_MENU,    INPUT_PULLUP);
  pinMode(BTN_FORWARD, INPUT_PULLUP);

  Wire.begin();
  Wire.setTimeout(250);

  lcd1.init(); lcd1.clear(); lcd1.noBacklight(); big1.begin();
  lcd2.init(); lcd2.clear(); lcd2.noBacklight(); big2.begin();
}

// ================= READ BUTTON EDGES =================
bool buttonPressed(bool &prevState, int pin, unsigned long &lastPress) {
  bool current = digitalRead(pin);
  unsigned long now = millis();
  if (prevState == HIGH && current == LOW && now - lastPress > debounceTime) {
    prevState = LOW;
    lastPress = now;
    return true;
  }
  if (current == HIGH) prevState = HIGH;
  return false;
}

// ================= LOOP =================
void loop() {
  handlePower();

  if (!clockOn) return;

  if (inMenu) {
    handleMenu();
  } else if (running) {
    updateClock();
    handleTurns();
  } else {
    handleStartupPlayerSelection();
    drawTimes();
    checkPresetEntry();
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
        clockOn = true;

        drawStartupScreen();
      } else {
        running = false; inMenu = false; clockOn = false;
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
    if (buttonPressed(prevP1, BTN_P1, lastP1Press)) {
      p1IsWhite = true;
      drawStartupScreen();
    }
    if (buttonPressed(prevP2, BTN_P2, lastP2Press)) {
      p1IsWhite = false;
      drawStartupScreen();
    }
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

// ================= MENU HANDLING =================
void handleMenu() {
  if (buttonPressed(prevForward, BTN_FORWARD, lastForwardPress)) {
    presetIndex = (presetIndex + 1) % NUM_PRESETS;
    presetConfirmed = false;
    drawPresetScreen();
  }

  if (buttonPressed(prevMenu, BTN_MENU, lastMenuPress)) {
    presetIndex = (presetIndex - 1 + NUM_PRESETS) % NUM_PRESETS;
    presetConfirmed = false;
    drawPresetScreen();
  }

  if (buttonPressed(prevPower, BTN_POWER, lastPowerPress)) {
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

  big2.setBigNumberCursor(5);
  if (presetConfirmed) {
    big2.print(p.hours); big2.print(ONE_COLUMN_SPACE_CHARACTER);
    if (p.minutes < 10) big2.print('0'); big2.print(p.minutes); big2.print(ONE_COLUMN_SPACE_CHARACTER);
    if (p.seconds < 10) big2.print('0'); big2.print(p.seconds);
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

  if (buttonPressed(prevPower, BTN_POWER, lastPowerPress)) {
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
  if (now - lastTick >= 1000) {
    lastTick = now;
    if (player1Turn  && p1Time > 0) p1Time--;
    if (!player1Turn && p2Time > 0) p2Time--;

    if (p1Time == 0 && player1Turn) {
      drawWinnerScreen(false);
      delay(5000);
      resetToPreset();
      return;
    }
    if (p2Time == 0 && !player1Turn) {
      drawWinnerScreen(true);
      delay(5000);
      resetToPreset();
      return;
    }

    drawTimes();
  }
}

// ================= WINNER SCREEN =================
void loadWinnerChars(LiquidCrystal_I2C &lcd) {
  lcd.createChar(CHAR_W_TL, W_TL);
  lcd.createChar(CHAR_W_BL, W_BL);
  lcd.createChar(CHAR_W_TR, W_TR);
  lcd.createChar(CHAR_W_BR, W_BR);
  lcd.createChar(CHAR_L_TL, L_TL);
  lcd.createChar(CHAR_L_BL, L_BL);
  lcd.createChar(CHAR_L_TR, L_TR);
  lcd.createChar(CHAR_L_BR, L_BR);
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
  lcd1.clear();
  lcd2.clear();
  loadWinnerChars(lcd1);
  loadWinnerChars(lcd2);

  if (p1Wins) {
    printBigW(lcd1, 7); // centred on 16 col display
    printBigL(lcd2, 7);
  } else {
    printBigL(lcd1, 7);
    printBigW(lcd2, 7);
  }
}

// ================= RESET TO PRESET =================
void resetToPreset() {
  applyPreset();
  running = false;
  player1Turn = p1IsWhite;
  prevP1 = HIGH;
  prevP2 = HIGH;
  big1.begin();
  big2.begin();
  drawStartupScreen();
}

// ================= HANDLE TURNS =================
void handleTurns() {
  bool p1Pressed = buttonPressed(prevP1, BTN_P1, lastP1Press);
  bool p2Pressed = buttonPressed(prevP2, BTN_P2, lastP2Press);

  if (p1Pressed && player1Turn) {
    player1Turn = false;
    if (incrementSeconds > 0) p1Time += incrementSeconds;
    drawTimes();
  }

  if (p2Pressed && !player1Turn) {
    player1Turn = true;
    if (incrementSeconds > 0) p2Time += incrementSeconds;
    drawTimes();
  }
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