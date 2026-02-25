#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ======================= LCDs =======================
LiquidCrystal_I2C lcdL(0x26, 16, 2);
LiquidCrystal_I2C lcdR(0x27, 16, 2);

// ======================= Buttons =======================
#define BTN_P1      3
#define BTN_POWER   4
#define BTN_MENU    5
#define BTN_FORWARD 8
#define BTN_P2      9

// ======================= Timing =======================
unsigned long p1Time = 0;
unsigned long p2Time = 0;
unsigned long lastTick = 0;
int incrementSeconds = 0;

// ======================= States =======================
bool clockOn = false;
bool inMenu = false;
bool running = false;
bool player1Turn = true;
bool p1IsWhite = true;
unsigned long holdStart[40];

// ======================= Presets =======================
struct Preset {
  int hours;
  int minutes;
  int increment;
};

Preset presets[] = {
  {0,1,0},
  {0,3,2},
  {0,5,3},
  {0,10,5},
  {0,15,10},
  {0,30,30},
  {1,30,30}
};

int presetIndex = 0;

// ======================= Display Mode =======================
enum DisplayMode { MODE_OFF, MODE_START, MODE_RUNNING, MODE_MENU };
DisplayMode currentMode = MODE_OFF;
DisplayMode lastMode = MODE_OFF;

// ======================= 1x2 Big Numbers =======================
// Each digit uses 2 custom chars vertically: top row, bottom row
// Define your 8 custom patterns for top row (0-7) and bottom row (0-7)
const uint8_t bigDigitChars[8][8] = {
  { B11110, B10010, B10010, B10010, B10010, B10010, B10010, B11110 }, // 0
  { B00100, B01100, B00100, B00100, B00100, B00100, B00100, B01110 }, // 1
  { B11110, B00010, B00010, B11110, B10000, B10000, B10000, B11110 }, // 2
  { B11110, B00010, B00010, B11110, B00010, B00010, B00010, B11110 }, // 3
  { B10010, B10010, B10010, B11110, B00010, B00010, B00010, B00010 }, // 4
  { B11110, B10000, B10000, B11110, B00010, B00010, B00010, B11110 }, // 5
  { B11110, B10000, B10000, B11110, B10010, B10010, B10010, B11110 }, // 6
  { B11110, B00010, B00010, B00010, B00010, B00010, B00010, B00010 }, // 7
};

// ======================= LCDBigNumbers Class =======================
class LCDBigNumbers {
public:
    LiquidCrystal_I2C *LCD;

    LCDBigNumbers(LiquidCrystal_I2C *aLCD) : LCD(aLCD) {}

    void init() {
        LCD->init();
        LCD->noBacklight();
        for (uint8_t i = 0; i < 8; i++) LCD->createChar(i, bigDigitChars[i]);
    }

    void printBigDigit(uint8_t digit, uint8_t col) {
        if(digit > 7) digit = 0; // fallback
        LCD->setCursor(col, 0);
        LCD->write(byte(digit));
        LCD->setCursor(col, 1);
        LCD->write(byte(digit));
    }

    void printBigTime(unsigned long t) {
        unsigned long h = t / 3600;
        unsigned long m = (t % 3600) / 60;
        unsigned long s = t % 60;

        uint8_t startCol = (16 - 7) / 2; // 0 00 00 layout

        printBigDigit(h, startCol);        // H
        printBigDigit(m/10, startCol+2);   // M tens
        printBigDigit(m%10, startCol+3);   // M ones
        printBigDigit(s/10, startCol+5);   // S tens
        printBigDigit(s%10, startCol+6);   // S ones
    }

    void showWhite() { LCD->setCursor(0,1); LCD->print("W"); }
    void showBlack() { LCD->setCursor(15,1); LCD->print("B"); }
};

// ======================= Objects =======================
LCDBigNumbers bigL(&lcdL);
LCDBigNumbers bigR(&lcdR);

// ======================= Setup =======================
void setup() {
    Serial.begin(115200);

    pinMode(BTN_P1, INPUT_PULLUP);
    pinMode(BTN_POWER, INPUT_PULLUP);
    pinMode(BTN_MENU, INPUT_PULLUP);
    pinMode(BTN_FORWARD, INPUT_PULLUP);
    pinMode(BTN_P2, INPUT_PULLUP);

    bigL.init();
    bigR.init();
}

// ======================= Loop =======================
void loop() {
    handlePower();

    if(!clockOn) return;

    if(inMenu){
        handleMenu();
        return;
    }

    if(!running){
        handlePreStartSelection();
    } else{
        handleGame();
    }

    updateDisplays();
}

// ======================= Power =======================
void handlePower() {
    if(digitalRead(BTN_POWER) == LOW){
        if(holdStart[BTN_POWER] == 0) holdStart[BTN_POWER] = millis();
        if(millis() - holdStart[BTN_POWER] > 2500){
            clockOn = !clockOn;
            running = false;
            inMenu = false;
            holdStart[BTN_POWER] = 0;

            if(clockOn){
                lcdL.backlight();
                lcdR.backlight();
                bigL.printBigTime(0);
                bigR.printBigTime(0);
                bigL.showWhite();
                bigR.showBlack();
            } else{
                lcdL.noBacklight();
                lcdR.noBacklight();
                lcdL.clear();
                lcdR.clear();
            }
        }
    } else holdStart[BTN_POWER] = 0;
}

// ======================= Pre-Start =======================
void handlePreStartSelection(){
    if(digitalRead(BTN_P1) == LOW) p1IsWhite = true;
    if(digitalRead(BTN_P2) == LOW) p1IsWhite = false;

    if(digitalRead(BTN_MENU) == LOW){
        if(holdStart[BTN_MENU]==0) holdStart[BTN_MENU]=millis();
        if(millis()-holdStart[BTN_MENU]>2500){
            inMenu=true;
            holdStart[BTN_MENU]=0;
        }
    } else holdStart[BTN_MENU]=0;

    if(digitalRead(BTN_POWER) == LOW){
        delay(200);
        running = true;
        player1Turn = p1IsWhite;
        lastTick = millis();
    }
}

// ======================= Game =======================
void handleGame(){
    if(millis()-lastTick >= 1000){
        lastTick = millis();
        if(player1Turn && p1Time>0) p1Time--;
        else if(!player1Turn && p2Time>0) p2Time--;
    }

    if(digitalRead(BTN_P1) == LOW && player1Turn){
        delay(200);
        p1Time+=incrementSeconds;
        player1Turn=false;
    }

    if(digitalRead(BTN_P2) == LOW && !player1Turn){
        delay(200);
        p2Time+=incrementSeconds;
        player1Turn=true;
    }
}

// ======================= Update Displays =======================
void updateDisplays(){
    if(running){
        bigL.printBigTime(p1Time);
        bigR.printBigTime(p2Time);
        bigL.showWhite();
        bigR.showBlack();
    }
}

// ======================= Menu (optional) =======================
void handleMenu(){
    lcdL.setCursor(0,0);
    lcdL.print("Preset:");
    lcdL.setCursor(0,1);
    lcdL.print(formatPreset(presets[presetIndex]));

    if(digitalRead(BTN_FORWARD) == LOW){
        delay(200);
        presetIndex = (presetIndex + 1) % 7;
    }

    if(digitalRead(BTN_MENU) == LOW){
        delay(200);
        presetIndex--;
        if(presetIndex < 0) presetIndex = 6;
    }

    if(digitalRead(BTN_P1) == LOW){
        if(holdStart[BTN_P1] == 0) holdStart[BTN_P1] = millis();
        if(millis() - holdStart[BTN_P1] > 2500){
            p1Time = p2Time = (presets[presetIndex].hours*3600UL) + (presets[presetIndex].minutes*60UL);
            incrementSeconds = presets[presetIndex].increment;
            inMenu = false;
            holdStart[BTN_P1] = 0;
        }
    } else holdStart[BTN_P1] = 0;
}

// ======================= Format Preset =======================
String formatPreset(Preset p){
    char buf[12];
    sprintf(buf, "%01d:%02d +%d", p.hours,p.minutes,p.increment);
    return String(buf);
}