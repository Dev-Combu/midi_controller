#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <MIDI.h>
#include <ArduinoJson.h>

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

// [설정] 버튼 번호를 화면에 유지할 시간 (500ms = 0.5초)
const unsigned long DISPLAY_HOLD_TIME = 500; 

const byte segPins[7] = {SEG_A, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F, SEG_G};

// 애노드 공통 (LOW 일 때 ON / HIGH 일 때 OFF)
const byte digits[3][7] = {
  {HIGH, LOW,  LOW,  HIGH, HIGH, HIGH, HIGH}, // 숫자 1 표시
  {LOW,  LOW,  HIGH, LOW,  LOW,  HIGH, LOW }, // 숫자 2 표시
  {LOW,  LOW,  LOW,  LOW,  HIGH, HIGH, LOW }  // 숫자 3 표시
};

SoftwareSerial midiSerial(255, 10); 
MIDI_CREATE_INSTANCE(SoftwareSerial, midiSerial, MIDI);

struct ButtonConfig { byte type; byte number; };
struct ModeConfig { ButtonConfig btn[3]; };

ModeConfig modes[3];
bool toggleState[3];
byte currentMode = 0;

// 화면 복귀를 위한 타이머 변수들
unsigned long displayTimer = 0;
bool isShowingButtonNum = false;

// 화면에 숫자를 띄워주는 함수 (editing 변수에 따라 DP 제어)
void showMode(byte mode, bool editing){
  for(int i = 0; i < 7; i++){
    digitalWrite(segPins[i], digits[mode][i]);
  }
  // 평소 모드(editing == false)일 때만 DP를 켬(LOW)
  // 버튼 번호 표시(editing == true)일 때는 DP를 끔(HIGH)
  digitalWrite(SEG_DP, editing ? HIGH : LOW);
}

void saveConfig(){ EEPROM.put(0, modes); }
void loadConfig(){
  EEPROM.get(0, modes);
  if(modes[0].btn[0].number > 127){
    modes[0].btn[0]={0,0};   modes[0].btn[1]={0,1};   modes[0].btn[2]={0,2};
    modes[1].btn[0]={1,20};  modes[1].btn[1]={1,21};  modes[1].btn[2]={1,22};
    modes[2].btn[0]={1,30};  modes[2].btn[1]={1,31};  modes[2].btn[2]={0,10};
    saveConfig();
  }
}

void setup() {
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);

  for(int i = 0; i < 7; i++) pinMode(segPins[i], OUTPUT);
  pinMode(SEG_DP, OUTPUT);

  Serial.begin(115200);
  midiSerial.begin(31250);
  MIDI.begin(MIDI_CHANNEL_OMNI);

  loadConfig();
  showMode(currentMode, false); // 처음에는 모드 표시 + DP 켜짐
}

void sendAction(byte idx){
  ButtonConfig cfg = modes[currentMode].btn[idx];
  if(cfg.type == 0){
    MIDI.sendProgramChange(cfg.number, 1);
  } else {
    toggleState[idx] = !toggleState[idx];
    MIDI.sendControlChange(cfg.number, toggleState[idx] ? 127 : 0, 1);
  }
}

void receiveWebConfig(){
  static String buffer;
  while(Serial.available()){
    char c = Serial.read();
    if(c == '\n'){ parseConfig(buffer); buffer = ""; }
    else { buffer += c; }
  }
}

void parseConfig(String json){
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, json);
  if(error) return;
  for(JsonObject item : doc.as<JsonArray>()){
    int mode = item["mode"];
    int button = item["button"];
    modes[mode].btn[button].type = item["type"];
    modes[mode].btn[button].number = item["number"];
  }
  saveConfig();
}

void loop() {
  static bool last1 = 1;
  static bool last2 = 1;
  static bool last3 = 1;

  bool b1 = digitalRead(BTN1);
  bool b2 = digitalRead(BTN2);
  bool b3 = digitalRead(BTN3);

  // --- 1. 모드 변경 처리 (1번과 3번을 동시에 누르고 있을 때) ---
  if (!b1 && !b3) {
    unsigned long pressTime = millis();
    while (!digitalRead(BTN1) && !digitalRead(BTN3)) {
      if (millis() - pressTime > 1000) { // 1초 이상 누르면 모드 전환
        currentMode++;
        if (currentMode > 2) currentMode = 0;
        showMode(currentMode, false);
        isShowingButtonNum = false;
        
        // 사용자가 두 버튼 모두에서 손을 뗄 때까지 대기하여 오작동 방지
        while (!digitalRead(BTN1) || !digitalRead(BTN3)) { delay(10); }
        
        // 모드 변경 후 기존 버튼 기억을 지워 신호 발송 차단
        last1 = 1; last2 = 1; last3 = 1;
        return;
      }
      delay(10);
    }
  }

  // --- 2. 단일 버튼 처리 (버튼을 눌렀다가 "뗄 때" 미디 신호 및 화면 표시 발동) ---

  // 1번 버튼에서 손을 뗄 때 (눌려있다가(last1==0) 떼어짐(b1==1))
  if (last1 == 0 && b1 == 1) { 
    delay(20); // 디바운싱
    showMode(0, true);       // 화면에 '1' 표시 + DP 끔
    sendAction(0);           // 미디 신호 송신
    displayTimer = millis();  // 타이머 작동시작
    isShowingButtonNum = true;
  }

  // 2번 버튼에서 손을 뗄 때
  if (last2 == 0 && b2 == 1) { 
    delay(20); 
    showMode(1, true);       // 화면에 '2' 표시 + DP 끔
    sendAction(1);           
    displayTimer = millis(); 
    isShowingButtonNum = true;
  }

  // 3번 버튼에서 손을 뗄 때
  if (last3 == 0 && b3 == 1) { 
    delay(20); 
    showMode(2, true);       // 화면에 '3' 표시 + DP 끔
    sendAction(2);           
    displayTimer = millis(); 
    isShowingButtonNum = true;
  }

  // --- 3. 타이머 체크: 지정된 시간(0.5초)이 지나면 원래 모드로 복원 ---
  if (isShowingButtonNum && (millis() - displayTimer >= DISPLAY_HOLD_TIME)) {
    showMode(currentMode, false); // 원래 현재 모드 복원 + DP 켬
    isShowingButtonNum = false;
  }

  last1 = b1;
  last2 = b2;
  last3 = b3;

  receiveWebConfig();
}