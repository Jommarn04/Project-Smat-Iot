#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <I2CKeyPad.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define LED_GREEN 18
#define LED_RED 19
#define I2C_ADDR_KEYPAD 0x20
#define I2C_ADDR_LCD 0x27

Preferences preferences;

const char *ssid = "Tamsuntai";
const char *password = "11111111";
const char *lineToken = "zCQMlbmEL06OnUCBYGO1AEQM/eI3jupY5WElbl9+rnNjMIc9PSU4r826fpbJ6BrzliAH4O2oB7ItijY/YEXoJ9Kp5k4Y/koAy75SYMV6Rl2BqL9zc3SNKGVpRAXSp6eqpRB918L/+e9jSsa4dxj5SwdB04t89/1O/w1cDnyilFU=";
const char *userId = "Uc0866ff06761067a0686a6edf34ff5ad";


#define API_KEY "AIzaSyDuWC2WdU2NmyXEWG66LCVBpGSYOICNQeQ"
#define DATABASE_URL "smartfarm-iot-d1ad0-default-rtdb.asia-southeast1.firebasedatabase.app" 

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool firebaseReady = false;

const int pumpPin = 2;      
const int moisturePin = 34; 
int moisturePercent = 0;
int limitThreshold = 30;

String inputString = "";
uint32_t lastKeyTime = 0;
unsigned long lastLineAlert = 0;
unsigned long lastSensorRead = 0;
unsigned long lastTime = 0;
unsigned long lastFirebaseRead = 0;

unsigned long timerDelay = 5000; 

bool lowMoistureAlertSent = false;
bool currentPumpOn = false;
bool lastPhysicalPumpState = false;
bool manualMode = false;
bool manualPumpState = false;
bool settingMode = false;
float energyUsed = 49.2; 
bool isOfflineMode = false;

LiquidCrystal_I2C lcd(I2C_ADDR_LCD, 16, 2);
I2CKeyPad keypad(I2C_ADDR_KEYPAD);

byte dropIcon[8] = {
  0b00100, 0b00100, 0b01010, 0b01010, 0b10001, 0b10001, 0b01110, 0b00000
};

char keys[16] = {
  '1', '2', '3', 'A',
  '4', '5', '6', 'B',
  '7', '8', '9', 'C',
  '*', '0', '#', 'D'
};

void sendLineAlert(String message) {
  if (!isOfflineMode && WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure secureClient;
    secureClient.setInsecure(); 
    HTTPClient http;
    http.begin(secureClient, "https://api.line.me/v2/bot/message/push");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + String(lineToken));

    String payload = "{\"to\":\"" + String(userId) + "\",\"messages\":[{\"type\":\"text\",\"text\":\"" + message + "\"}]}";
    int httpResponseCode = http.POST(payload);
    if (httpResponseCode > 0) {
      Serial.println("LINE Notify sent!");
    } else {
      Serial.printf("Error LINE: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    http.end();
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(pumpPin, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);

  digitalWrite(pumpPin, HIGH); 

  Wire.begin();
  lcd.init();
  lcd.backlight();
  keypad.begin();
  lcd.createChar(0, dropIcon);

  preferences.begin("plant-config", false);
  limitThreshold = preferences.getInt("limit", 30);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    isOfflineMode = false;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ONLINE MODE");
    lcd.setCursor(0, 1);
    lcd.print("WiFi Connected!");
    delay(2000);
    lcd.clear();

    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    config.signer.test_mode = true; 

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true); 
    
    Serial.println("Firebase Connected (Test Mode)!");
    firebaseReady = true;
  } else {
    isOfflineMode = true;
    Serial.println("\n[OFFLINE MODE] Cannot connect to WiFi. System running locally.");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("OFFLINE MODE");
    delay(2000);
    lcd.clear();
  }
}

void handleKeypad() {
  if (!keypad.isPressed()) return;
  if (millis() - lastKeyTime < 200) return;
  lastKeyTime = millis();

  uint8_t index = keypad.getKey();
  if (index >= 16) return;

  char key = keys[index];

  if (key == 'A') { 
    manualMode = true;
    manualPumpState = true;
    if(firebaseReady) {
      Firebase.RTDB.setBool(&fbdo, "smartfarm/control/manualMode", true);
      Firebase.RTDB.setBool(&fbdo, "smartfarm/control/manualPump", true);
    }
  } else if (key == 'B') { 
    manualMode = true;
    manualPumpState = false;
    if(firebaseReady) {
      Firebase.RTDB.setBool(&fbdo, "smartfarm/control/manualMode", true);
      Firebase.RTDB.setBool(&fbdo, "smartfarm/control/manualPump", false);
    }
  } else if (key == 'D') { 
    manualMode = false;
    if(firebaseReady) {
      Firebase.RTDB.setBool(&fbdo, "smartfarm/control/manualMode", false);
    }
  } else if (key == '*') {
    settingMode = true;
    inputString = "";
    lcd.clear(); 
  } else if (key >= '0' && key <= '9') {
    if (!settingMode) {
      settingMode = true;
      inputString = "";
      lcd.clear(); 
    }
    if (inputString.length() < 3) {
      inputString += key;
    }
  } else if (key == '#') {
    if (settingMode) {
      int newVal = inputString.toInt();
      if (newVal >= 0 && newVal <= 100) {
        limitThreshold = newVal;
        preferences.putInt("limit", limitThreshold);
        if(firebaseReady) {
          Firebase.RTDB.setInt(&fbdo, "smartfarm/control/threshold", limitThreshold);
        }
      }
      inputString = "";
      settingMode = false;
      lcd.clear();
    }
  }
}

void loop() {
  handleKeypad();
  if (firebaseReady && (millis() - lastFirebaseRead > 2000)) {
    lastFirebaseRead = millis();
    if (Firebase.RTDB.getBool(&fbdo, "smartfarm/control/manualMode")) {
      manualMode = fbdo.boolData();
    }
    if (Firebase.RTDB.getBool(&fbdo, "smartfarm/control/manualPump")) {
      manualPumpState = fbdo.boolData();
    }
    if (Firebase.RTDB.getInt(&fbdo, "smartfarm/control/threshold")) {
      int webThreshold = fbdo.intData();
      if (webThreshold != limitThreshold && webThreshold >= 0 && webThreshold <= 100) {
        limitThreshold = webThreshold;
        preferences.putInt("limit", limitThreshold); 
      }
    }
  }


  if (millis() - lastSensorRead > 500) {
    lastSensorRead = millis();

    int rawValue = analogRead(moisturePin);
    moisturePercent = map(rawValue, 4095, 2500, 0, 100);
    moisturePercent = constrain(moisturePercent, 0, 100);
    
    if (manualMode) {
      digitalWrite(pumpPin, manualPumpState ? LOW : HIGH);
    } else {
      if (moisturePercent < limitThreshold) {
        digitalWrite(pumpPin, LOW); 
      } else if (moisturePercent > limitThreshold + 5) {
        digitalWrite(pumpPin, HIGH); 
      }
    }
    
    currentPumpOn = (digitalRead(pumpPin) == LOW);
    if (currentPumpOn) energyUsed += 0.05;

    digitalWrite(LED_GREEN, currentPumpOn ? HIGH : LOW);
    digitalWrite(LED_RED, currentPumpOn ? LOW : HIGH);

    if (settingMode) {
      lcd.setCursor(0, 0);
      lcd.print("Set Moisture:   "); 
      lcd.setCursor(0, 1);
      lcd.print("Limit -> ");
      lcd.print(inputString);
      lcd.print("%       ");
    } else {
      lcd.setCursor(0, 0);
      lcd.write(0); 
      lcd.print(":");
      if (moisturePercent < 100) lcd.print(" ");
      if (moisturePercent < 10) lcd.print(" ");
      lcd.print(moisturePercent);
      lcd.print("% Set:");
      if (limitThreshold < 100) lcd.print(" ");
      if (limitThreshold < 10) lcd.print(" ");
      lcd.print(limitThreshold);
      lcd.print("% ");

      lcd.setCursor(0, 1);
      lcd.print(manualMode ? "[MANUAL] " : "[ AUTO ] ");
      lcd.print(currentPumpOn ? "PUMP:ON " : "PUMP:OFF");
    }
  }

  if (firebaseReady && (millis() - lastTime > timerDelay)) {
    lastTime = millis();
    Firebase.RTDB.setInt(&fbdo, "smartfarm/sensors/moisture", moisturePercent);
    Firebase.RTDB.setInt(&fbdo, "smartfarm/sensors/pumpState", currentPumpOn ? 1 : 0);
    Firebase.RTDB.setFloat(&fbdo, "smartfarm/sensors/energy", energyUsed);
  }

  if (moisturePercent < limitThreshold) {
    if (!lowMoistureAlertSent && (millis() - lastLineAlert > 60000 || lastLineAlert == 0)) {
      sendLineAlert("😊เเจ้งเตือนความชื้นตอนนี้คร้าบบบบ ความชื้น = " + String(moisturePercent) + "%");
      lowMoistureAlertSent = true;
      lastLineAlert = millis();
    }
  } else {
    lowMoistureAlertSent = false;
  }
}