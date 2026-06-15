#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <MIDI.h>
#include <stdlib.h>
#include <string.h>

#define SEG_A 5
#define SEG_B 6
#define SEG_C 7
#define SEG_D 8
#define SEG_E 9
#define SEG_F 11  
#define SEG_G 12
#define SEG_DP 13

#define BTN1 2
#define BTN2 3
#define BTN3 4

const unsigned long DISPLAY_HOLD_TIME = 500; 
const byte segPins[7] = {SEG_A, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F, SEG_G};

const byte digits[3][7] = {
  {HIGH, LOW,  LOW,  HIGH, HIGH, HIGH, HIGH}, // 1
  {LOW,  LOW,  HIGH, LOW,  LOW,  HIGH, LOW }, // 2
  {LOW,  LOW,  LOW,  LOW,  HIGH, HIGH, LOW }  // 3
};

SoftwareSerial midiSerial(255, 10); 
MIDI_CREATE_INSTANCE(SoftwareSerial, midiSerial, MIDI);

struct ButtonConfig { 
  byte type;   
  byte number; 
};

struct ModeConfig { 
  ButtonConfig btn[3]; 
};

ModeConfig modes[3];
bool toggleState[3];
byte currentMode = 0;

unsigned long displayTimer = 0;
bool isShowingButtonNum = false;

#define EEPROM_MAGIC 0x55 // EEPROM initialization signature

void showMode(byte mode, bool editing){
  for(int i = 0; i < 7; i++) {
    digitalWrite(segPins[i], digits[mode][i]);
  }
  digitalWrite(SEG_DP, editing ? HIGH : LOW);
}

void saveConfig(){ 
  EEPROM.write(0, EEPROM_MAGIC);
  EEPROM.put(1, modes); 
}

void loadConfig(){
  byte magic = EEPROM.read(0);
  if (magic == EEPROM_MAGIC) {
    EEPROM.get(1, modes);
  } else {
    // Default configuration
    modes[0].btn[0]={1,26};  modes[0].btn[1]={1,60};  modes[0].btn[2]={1,27}; 
    modes[1].btn[0]={1,22};  modes[1].btn[1]={1,76};  modes[1].btn[2]={1,23}; 
    modes[2].btn[0]={0,0};   modes[2].btn[1]={0,1};   modes[2].btn[2]={0,2};  
    saveConfig();
  }
}

void setup() {
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);

  for(int i = 0; i < 7; i++) pinMode(segPins[i], OUTPUT);
  pinMode(SEG_DP, OUTPUT);

  Serial.begin(9600); // Web page communication baud rate
  midiSerial.begin(31250);
  MIDI.begin(MIDI_CHANNEL_OMNI);

  loadConfig();
  showMode(currentMode, false);
}

void sendAction(byte idx){
  ButtonConfig cfg = modes[currentMode].btn[idx];
  
  if(cfg.type == 0){
    MIDI.sendProgramChange(cfg.number, 1);
  } else {
    toggleState[idx] = !toggleState[idx];
    byte ccVal = toggleState[idx] ? 127 : 0;
    
    // --- Ampero II Stomp MIDI CC Mapping based on Firmware V2.0.0 ---
    
    // 1. Momentary / Trigger CCs: Always send 127 on press
    // - CC 22: Bank -
    // - CC 23: Bank +
    // - CC 24: Pre-Select Menu
    // - CC 26: Patch -
    // - CC 27: Patch +
    // - CC 63: Looper Rec/Overdub
    // - CC 67: Looper Undo/Redo
    // - CC 68: Looper Clear
    // - CC 76: Tap Tempo
    // - CC 79: FS 1 Effect Slot On/Off
    // - CC 80: FS 2 Effect Slot On/Off
    // - CC 81: FS 3 Effect Slot On/Off
    if(cfg.number == 22 || cfg.number == 23 || cfg.number == 24 || 
       cfg.number == 26 || cfg.number == 27 || 
       cfg.number == 63 || cfg.number == 67 || cfg.number == 68 || 
       cfg.number == 76 || cfg.number == 79 || cfg.number == 80 || cfg.number == 81) {
      ccVal = 127;
    }
    
    // 2. Choose Scene (CC 25): Value 1-3. 
    // Automatically sets Scene based on button index (BTN1 -> Scene 1, BTN2 -> Scene 2, BTN3 -> Scene 3)
    else if (cfg.number == 25) {
      ccVal = idx + 1; // BTN1 (idx 0) -> Value 1 (Scene 1), BTN2 -> Value 2 (Scene 2), BTN3 -> Value 3 (Scene 3)
    }
    
    // 3. Unit Engage/Bypass (CC 78): Toggles between 0 (Analog Bypass) and 2 (Engage)
    else if (cfg.number == 78) {
      ccVal = toggleState[idx] ? 2 : 0;
    }
    
    // 4. Default Toggle CCs (e.g. Tuner CC 60, Looper Play/Stop CC 64, Unit Mode CC 28):
    // Toggles between 0 (Off/Stop/Patch Mode) and 127 (On/Play/Stomp Mode)
    
    MIDI.sendControlChange(cfg.number, ccVal, 1);
  }
}

// Memory-safe C-string parser
void parseAndSave(char* data) {
  char* token = strtok(data, ",");
  bool updated = false;
  
  while (token != NULL) {
    if (token[0] == 'M') {
      char* bPtr = strchr(token, 'B');
      char* tPtr = strchr(token, 'T');
      char* nPtr = strchr(token, 'N');
      
      if (bPtr && tPtr && nPtr) {
        int m = atoi(token + 1);
        int b = atoi(bPtr + 1);
        int t = atoi(tPtr + 1);
        int n = atoi(nPtr + 1);
        
        if (m >= 0 && m < 3 && b >= 0 && b < 3) {
          modes[m].btn[b].type = t;
          modes[m].btn[b].number = n;
          updated = true;
        }
      }
    }
    token = strtok(NULL, ",");
  }
  
  if (updated) {
    saveConfig();
    showMode(currentMode, false);
  }
}

// Memory-safe Serial reader
void receiveWebConfig(){
  static char buffer[128];
  static size_t bufIndex = 0;
  
  while(Serial.available() > 0){
    char c = Serial.read();
    if(c == '\n' || c == '\r'){
      if(bufIndex > 0) {
        buffer[bufIndex] = '\0';
        parseAndSave(buffer);
        bufIndex = 0;
      }
    } else {
      if(bufIndex < sizeof(buffer) - 1) {
        buffer[bufIndex++] = c;
      }
    }
  }
}

void loop() {
  static bool last1 = 1;
  static bool last2 = 1;
  static bool last3 = 1;

  bool b1 = digitalRead(BTN1);
  bool b2 = digitalRead(BTN2);
  bool b3 = digitalRead(BTN3);

  // Dual-press detection (BTN1 + BTN3 for 1 second) to switch Modes
  if (!b1 && !b3) {
    unsigned long pressTime = millis();
    while (!digitalRead(BTN1) && !digitalRead(BTN3)) {
      if (millis() - pressTime > 1000) { 
        currentMode++;
        if (currentMode > 2) currentMode = 0;
        showMode(currentMode, false);
        isShowingButtonNum = false;
        while (!digitalRead(BTN1) || !digitalRead(BTN3)) { delay(10); }
        last1 = 1; last2 = 1; last3 = 1;
        return;
      }
      delay(10);
    }
  }

  // Trigger single button actions on release (rising edge)
  if (last1 == 0 && b1 == 1) { 
    delay(20); 
    showMode(0, true);       
    sendAction(0);           
    displayTimer = millis();  
    isShowingButtonNum = true;
  }
  if (last2 == 0 && b2 == 1) { 
    delay(20); 
    showMode(1, true);       
    sendAction(1);           
    displayTimer = millis(); 
    isShowingButtonNum = true;
  }
  if (last3 == 0 && b3 == 1) { 
    delay(20); 
    showMode(2, true);       
    sendAction(2);           
    displayTimer = millis(); 
    isShowingButtonNum = true;
  }

  if (isShowingButtonNum && (millis() - displayTimer >= DISPLAY_HOLD_TIME)) {
    showMode(currentMode, false); 
    isShowingButtonNum = false;
  }

  last1 = b1;
  last2 = b2;
  last3 = b3;

  receiveWebConfig();
}