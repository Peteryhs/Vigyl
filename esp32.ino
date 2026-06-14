#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --- Config Values ---
#define MAX_SERVERS 4
#define MAX_ALERTS 4
#define USE_ENTERPRISE true
#define EAP_IDENTITY ""
#define EAP_PASSWORD ""

const char* ssid = "";
const char* wpa2_password = "";
const char* api_url = "";
const char* api_key = "";
const unsigned long poll_interval = 10000;
const unsigned long lcd_cycle_interval = 5000;
const unsigned long DEBOUNCE_DELAY = 300;
const unsigned long scroll_interval = 400;
const int SCROLL_START_PAUSE = 4;
const int SCROLL_END_PAUSE = 4;

// --- Pin Allocations ---
const int BUTTON_PIN = 32;
const int ALERT_BUTTON_PIN = 34;
const int status_led = 2;

// --- Custom LCD Characters ---
// Begin AI Generated Code
// Google Gemini 3.1 Pro
byte upArrowChar[8] = {
  0b00100, 0b01110, 0b11111, 0b00100, 0b00100, 0b00100, 0b00000, 0b00000
};

byte downArrowChar[8] = {
  0b00100, 0b00100, 0b00100, 0b11111, 0b01110, 0b00100, 0b00000, 0b00000
};

byte backslashChar[8] = {
  0b00000, 0b10000, 0b01000, 0b00100, 0b00010, 0b00001, 0b00000, 0b00000
};

byte dinoChar[8] = {
  0b00111, 0b00101, 0b00111, 0b10110, 0b11111, 0b01110, 0b01010, 0b01010
};

byte cactusChar[8] = {
  0b00100, 0b00101, 0b10101, 0b10111, 0b11100, 0b00100, 0b00100, 0b00100
};
// End AI Generated Code

// --- Data Structures ---
struct ServerStats {
  String name;
  String status;
  float cpu;
  float ram;
  float disk;
  float net_in;
  float net_out;
};

struct ActiveAlert {
  String monitor_name;
  String server_name;
  String message;
};

enum DisplayMode {
  MODE_SERVER_CYCLE,
  MODE_NEW_ALERT,
  MODE_ALERT_VIEW,
  MODE_DINO_GAME
};

// --- Global Objects & State Variables ---
LiquidCrystal_I2C lcd(0x27, 16, 2);
SemaphoreHandle_t dataMutex = NULL;

DisplayMode displayMode = MODE_SERVER_CYCLE;
ServerStats serverStatsList[MAX_SERVERS];
ActiveAlert alertList[MAX_ALERTS];
int serverCount = 0;
int alertCount = 0;

unsigned long last_lcd_cycle_time = 0;
int current_screen_index = 0;
int current_alert_index = 0;

volatile bool button_isr_flag = false;
volatile unsigned long last_isr_trigger = 0;
volatile bool alert_button_isr_flag = false;
volatile unsigned long alert_last_isr_trigger = 0;

String seenAlertNames[MAX_ALERTS];
int seenAlertCount = 0;
int newAlertQueue[MAX_ALERTS];
int newAlertQueueCount = 0;
int newAlertQueuePos = 0;

bool alert_scrolling_active = false;
String alert_scroll_text = "";
int alert_scroll_offset = 0;
int alert_scroll_ticks = 0;
unsigned long last_scroll_time = 0;
unsigned long last_poll_time = 0;

// Begin AI Generated Code
// Google Gemini 3.1 Pro
// --- Dino Game State --- 
bool dino_game_over = false;
int dino_y = 1;
unsigned long dino_jump_start = 0;
const unsigned long DINO_JUMP_DURATION = 1000;
int dino_obstacle_x = 15;
int dino_obstacle_width = 1;
unsigned long dino_last_update_time = 0;
unsigned long dino_update_interval = 250;
int dino_score = 0;
bool dino_needs_redraw = true;
unsigned long dino_mode_transition_time = 0;

// End AI Generated Code

// --- Sleep Mode State ---
volatile bool sleep_mode = false;
unsigned long sleep_transition_time = 0;
const unsigned long SLEEP_DEBOUNCE = 1000;

// --- Function Prototypes ---
// Begin AI Generated Code
// Google Gemini 3.1 Pro
void connectToWiFi();
void checkServerStatus();
void parseStatusJson(String json_str);
void printCompactBar(char label, float value);
void printNetRate(float kbps);
void showAlertScreen(int alertIdx);
void cycleLcdScreen();
void updateAlertScroll();
void finishAlertScroll();
void httpTask(void* pvParameters);
void initDinoGame();
void runDinoGame();
void enterSleepMode();
void wakeFromSleep();
// End AI Generated Code

// --- ISR Handlers ---
void IRAM_ATTR buttonISR() {
  unsigned long now = millis();
  if (now - last_isr_trigger >= DEBOUNCE_DELAY) {
    last_isr_trigger = now;
    button_isr_flag = true;
  }
}

void IRAM_ATTR alertButtonISR() {
  unsigned long now = millis();
  if (now - alert_last_isr_trigger >= DEBOUNCE_DELAY) {
    alert_last_isr_trigger = now;
    alert_button_isr_flag = true;
  }
}

// --- Setup & Main Loop ---
void setup() {
  Serial.begin(115200);
  pinMode(status_led, OUTPUT);
  digitalWrite(status_led, LOW);
  pinMode(BUTTON_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, RISING);
  pinMode(ALERT_BUTTON_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(ALERT_BUTTON_PIN), alertButtonISR, RISING);

  Wire.begin(18, 21);
  lcd.init();
  lcd.createChar(0, upArrowChar);
  lcd.createChar(1, downArrowChar);
  lcd.createChar(2, backslashChar);
  lcd.createChar(3, dinoChar);
  lcd.createChar(4, cactusChar);
  lcd.home();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Vigyl b0.35");
  lcd.setCursor(0, 1);
  lcd.print("Booting...");
  delay(1500);

  connectToWiFi();

  dataMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(
    httpTask,
    "HTTPTask",
    8192,
    NULL,
    1,
    NULL,
    0
  );
}

void loop() {
  unsigned long current_time = millis();

  // --- Sleep Mode: wake on any button press (with debounce) ---
  if (sleep_mode) {
    if (current_time - sleep_transition_time < SLEEP_DEBOUNCE) {
      // Ignore button presses during cooldown after entering sleep
      button_isr_flag = false;
      alert_button_isr_flag = false;
      return;
    }
    if (button_isr_flag || alert_button_isr_flag) {
      button_isr_flag = false;
      alert_button_isr_flag = false;
      wakeFromSleep();
    }
    return;
  }

  // Begin AI Generated Code
  // Google Gemini 3.1 Pro

  bool both_held = (digitalRead(BUTTON_PIN) == HIGH) && (digitalRead(ALERT_BUTTON_PIN) == HIGH);
  static unsigned long both_held_start = 0;
  if (both_held) {
    if (both_held_start == 0) {
      both_held_start = current_time;
    } else if (current_time - both_held_start >= 5000) {
      if (displayMode == MODE_DINO_GAME) {
        Serial.println("Dino Game: Exiting easter egg mode!");
        displayMode = MODE_SERVER_CYCLE;
        current_screen_index = 0;
        last_lcd_cycle_time = current_time;
        if (dataMutex != NULL && xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
          cycleLcdScreen();
          xSemaphoreGive(dataMutex);
        }
      } else {
        Serial.println("Dino Game: Entering easter egg mode!");
        displayMode = MODE_DINO_GAME;
        initDinoGame();
      }
      both_held_start = 0;
      button_isr_flag = false;
      alert_button_isr_flag = false;
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("<< LOADING... >>");
      delay(1000);
      
      button_isr_flag = false;
      alert_button_isr_flag = false;
      current_time = millis();
    }
  } else {
    both_held_start = 0;
  }

  // --- D34-only hold for 5 seconds: enter sleep mode ---
  static unsigned long d34_solo_held_start = 0;
  bool d34_solo_held = (digitalRead(ALERT_BUTTON_PIN) == HIGH) && (digitalRead(BUTTON_PIN) == LOW);
  if (d34_solo_held) {
    if (d34_solo_held_start == 0) {
      d34_solo_held_start = current_time;
    } else if (current_time - d34_solo_held_start >= 5000) {
      d34_solo_held_start = 0;
      button_isr_flag = false;
      alert_button_isr_flag = false;
      enterSleepMode();
      return;
    }
  } else {
    d34_solo_held_start = 0;
  }

  if (displayMode == MODE_DINO_GAME) {
    runDinoGame();
    return;
  }
// End AI Generated Code

  if (button_isr_flag) {
    button_isr_flag = false;
    Serial.println("\nD32 BUTTON \u2014 server cycle / back");

    if (dataMutex != NULL && xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
      alert_scrolling_active = false;

      if (displayMode == MODE_ALERT_VIEW) {
        Serial.println("  Exiting alert view \u2192 server cycle");
        displayMode = MODE_SERVER_CYCLE;
        current_screen_index = 0;
        cycleLcdScreen();
      } else if (displayMode == MODE_NEW_ALERT) {
        Serial.println("  Skipping new-alert display \u2192 server cycle");
        displayMode = MODE_SERVER_CYCLE;
        newAlertQueuePos = newAlertQueueCount;
        cycleLcdScreen();
      } else {
        Serial.printf("  Advancing server screen (was %d)\n", current_screen_index);
        cycleLcdScreen();
      }

      last_lcd_cycle_time = current_time;
      xSemaphoreGive(dataMutex);
    }
  }

  if (alert_button_isr_flag) {
    alert_button_isr_flag = false;
    Serial.println("\nD34 BUTTON \u2014 enter alert view");

    if (dataMutex != NULL && xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
      alert_scrolling_active = false;

      if (alertCount > 0) {
        displayMode = MODE_ALERT_VIEW;
        current_alert_index = 0;
        showAlertScreen(0);
        Serial.printf("  Showing alert 0 of %d\n", alertCount);
      } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("No Active");
        lcd.setCursor(0, 1);
        lcd.print("Alerts");
        Serial.println("  No alerts to display");
      }

      last_lcd_cycle_time = current_time;
      xSemaphoreGive(dataMutex);
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected! Reconnecting...");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Disconn!");
    lcd.setCursor(0, 1);
    lcd.print("Reconnecting...");
    connectToWiFi();
  }


// Begin AI Generated Code
// Google Gemini 3.1 Pro

  if (WiFi.status() == WL_CONNECTED && (current_time - last_lcd_cycle_time >= lcd_cycle_interval)) {
    if (dataMutex != NULL && xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
      last_lcd_cycle_time = current_time;
      cycleLcdScreen();
      xSemaphoreGive(dataMutex);
    }
  }

  if (alert_scrolling_active && (current_time - last_scroll_time >= scroll_interval)) {
    last_scroll_time = current_time;
    updateAlertScroll();
  }
}
// End AI Generated Code

// Begin AI Generated Code
// Google Gemini 3.1 Pro

// --- Wi-Fi & Network Utilities --- 
void connectToWiFi() {
  Serial.println("\n==========================================");
  Serial.print("Connecting to SSID: ");
  Serial.println(ssid);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Vigyl b0.35");
  lcd.setCursor(0, 1);
  lcd.print("connecting ");

  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);

  if (USE_ENTERPRISE) {
    Serial.println("Using WPA2-Enterprise authentication...");
    WiFi.begin(ssid, WPA2_AUTH_PEAP, EAP_IDENTITY, EAP_IDENTITY, EAP_PASSWORD);
  } else {
    Serial.println("Using WPA2-Personal authentication...");
    WiFi.begin(ssid, wpa2_password);
  }

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) {
    delay(500);
    Serial.print(".");

    lcd.setCursor(11, 1);
    const char spinner[] = {'|', '/', '-'};
    if (attempts % 4 == 3) {
      lcd.write((byte)2);
    } else {
      lcd.print(spinner[attempts % 4]);
    }
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi Connected successfully!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.println("==========================================\n");

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP().toString());
    
    digitalWrite(status_led, HIGH); delay(100);
    digitalWrite(status_led, LOW);  delay(100);
    digitalWrite(status_led, HIGH); delay(100);
    digitalWrite(status_led, LOW);

    delay(2000);
  } else {
    Serial.println("\nWi-Fi Connection failed. Will retry...");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Conn Failed!");
    lcd.setCursor(0, 1);
    lcd.print("Will retry...");
    delay(2000);
  }
}

// End AI Generated Code

void checkServerStatus() {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println("Fetching dashboard status...");
  HTTPClient http;

  http.begin(api_url);
  http.setTimeout(10000);
  
  http.addHeader("X-API-Key", api_key);
  http.addHeader("Content-Type", "application/json");

  int http_code = http.GET();

  if (http_code > 0) {
    Serial.print("HTTP Status Code: ");
    Serial.println(http_code);

    if (http_code == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println("Data received successfully.");
      parseStatusJson(payload);
    } else {
      Serial.print("Server returned error code: ");
      Serial.println(http_code);
    }
  } else {
    Serial.print("HTTP GET Request failed. Error: ");
    Serial.println(http.errorToString(http_code).c_str());
  }

  http.end();
}

void parseStatusJson(String json_str) {
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, json_str);

  if (error) {
    Serial.print("JSON Deserialization failed: ");
    Serial.println(error.f_str());
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  bool any_container_down = false;

  ServerStats tempServerStatsList[MAX_SERVERS];
  int tempServerCount = 0;
  ActiveAlert tempAlertList[MAX_ALERTS];
  int tempAlertCount = 0;

// Begin AI Generated Code
// Google Gemini 3.1 Pro

  Serial.println("\n--- SYSTEM MONITOR REPORT ---");

  for (JsonVariant val : arr) {
    JsonObject obj = val.as<JsonObject>();

    if (obj.containsKey("hardware")) {
      JsonArray hw_arr = obj["hardware"];
      Serial.println("Hardware Status:");
      for (JsonVariant hw_val : hw_arr) {
        JsonObject hw = hw_val.as<JsonObject>();
        const char* server_name = hw["Server_Name"];
        const char* status = hw["hw_status"]["status"];
        float cpu = hw["hw_status"]["cpu_load"];
        float ram = hw["hw_status"]["ram_pct"];
        float disk = hw["hw_status"]["disk_pct"];
        float net_in = hw["hw_status"]["net_in_kbps"];
        float net_out = hw["hw_status"]["net_out_kbps"];
        
        Serial.printf("  \u2022 [%s]: %s (CPU: %.2f%%, RAM: %.1f%%, Disk: %.1f%%, Net: %.1fKB/s in / %.1fKB/s out)\n", server_name, status, cpu, ram, disk, net_in, net_out);

        if (tempServerCount < MAX_SERVERS) {
          tempServerStatsList[tempServerCount].name = String(server_name);
          tempServerStatsList[tempServerCount].status = String(status);
          tempServerStatsList[tempServerCount].cpu = cpu;
          tempServerStatsList[tempServerCount].ram = ram;
          tempServerStatsList[tempServerCount].disk = disk;
          tempServerStatsList[tempServerCount].net_in = net_in;
          tempServerStatsList[tempServerCount].net_out = net_out;
          tempServerCount++;
        }
      }
    } 
    else if (obj.containsKey("Monitor_Name")) {
      const char* monitor_name = obj["Monitor_Name"];
      int status_code = obj["Status"];
      const char* server = obj["Server_Name"];
      const char* msg = obj["Message"];
      const char* ai_summary = obj["AI_Summary"];

      Serial.printf("ALERT: Container [%s] on [%s] is DOWN!\n", monitor_name, server);
      Serial.printf("   Message: %s\n", msg);
      if (ai_summary) {
        Serial.printf("   AI Summary: %s\n", ai_summary);
      }
      
      if (status_code == 0) {
        any_container_down = true;
        if (tempAlertCount < MAX_ALERTS) {
          tempAlertList[tempAlertCount].monitor_name = String(monitor_name);
          tempAlertList[tempAlertCount].server_name = String(server);
          tempAlertList[tempAlertCount].message = ai_summary ? String(ai_summary) : String(msg);
          tempAlertCount++;
        }
      }
    }
  }
  Serial.println("--------------------------------\n");



  if (dataMutex != NULL && xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
    serverCount = tempServerCount;
    for (int i = 0; i < tempServerCount; i++) {
      serverStatsList[i] = tempServerStatsList[i];
    }

    alertCount = tempAlertCount;
    for (int i = 0; i < tempAlertCount; i++) {
      alertList[i] = tempAlertList[i];
    }

    if (any_container_down) {
      digitalWrite(status_led, HIGH);
    } else {
      digitalWrite(status_led, LOW);
    }

    newAlertQueueCount = 0;
    newAlertQueuePos = 0;
    for (int i = 0; i < tempAlertCount; i++) {
      bool alreadySeen = false;
      for (int j = 0; j < seenAlertCount; j++) {
        if (seenAlertNames[j] == tempAlertList[i].monitor_name) {
          alreadySeen = true;
          break;
        }
      }
      if (!alreadySeen) {
        newAlertQueue[newAlertQueueCount++] = i;
        Serial.printf("New alert queued for one-time display: %s\n", tempAlertList[i].monitor_name.c_str());
      }
    }

    seenAlertCount = tempAlertCount;
    for (int i = 0; i < tempAlertCount; i++) {
      seenAlertNames[i] = tempAlertList[i].monitor_name;
    }

    int server_screens = serverCount * 2;
    if (server_screens > 0 && current_screen_index >= server_screens) {
      current_screen_index = server_screens - 1;
    }

    last_lcd_cycle_time = millis();
    
    xSemaphoreGive(dataMutex);
  }
}

// End AI Generated Code

void httpTask(void* pvParameters) {
  Serial.println("HTTP background task started.");
  vTaskDelay(pdMS_TO_TICKS(2000));

  for (;;) {
    if (WiFi.status() == WL_CONNECTED && !sleep_mode) {
      Serial.println("Starting background HTTP poll...");
      checkServerStatus();
      Serial.println("Background HTTP poll complete.");
    }
    vTaskDelay(pdMS_TO_TICKS(poll_interval));
  }
}

// Begin AI Generated Code
// Google Gemini 3.1 Pro
// --- LCD Display Helpers ---

void printCompactBar(char label, float value) {
  int pct = constrain((int)round(value), 0, 99);
  int bars = (pct * 3 + 50) / 100;
  if (pct > 0 && bars == 0) bars = 1;
  
  lcd.print(label);
  lcd.print(':');
  for (int i = 0; i < 3; i++) {
    if (i < bars) lcd.write(0xFF);
    else lcd.print(' ');
  }
  char buf[4];
  snprintf(buf, sizeof(buf), "%2d%%", pct);
  lcd.print(buf);
}

// End AI Generated Code


void printNetRate(float kbps) {
  if (kbps < 100.0f) {
    int val = constrain((int)round(kbps), 0, 99);
    char buf[4];
    snprintf(buf, sizeof(buf), "%2dK", val);
    lcd.print(buf);
  } else {
    int val = constrain((int)round(kbps / 1024.0f), 0, 99);
    if (val == 0) val = 1;
    char buf[4];
    snprintf(buf, sizeof(buf), "%2dM", val);
    lcd.print(buf);
  }
}

void showAlertScreen(int alertIdx) {
  if (alertIdx < 0 || alertIdx >= alertCount) return;

  ActiveAlert a = alertList[alertIdx];
  lcd.clear();

  char row0[17];
  snprintf(row0, sizeof(row0), "DOWN: %s", a.monitor_name.c_str());
  lcd.setCursor(0, 0);
  lcd.print(row0);

  alert_scroll_text = a.message;
  alert_scroll_offset = 0;
  alert_scroll_ticks = 0;
  alert_scrolling_active = true;
  last_scroll_time = millis();

  lcd.setCursor(0, 1);
  int text_len = alert_scroll_text.length();
  for (int i = 0; i < 16; i++) {
    if (i < text_len) lcd.print(alert_scroll_text[i]);
    else lcd.print(' ');
  }
}

void cycleLcdScreen() {
  if (alert_scrolling_active) return;

  if (displayMode == MODE_ALERT_VIEW) {
    if (alertCount == 0) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("No Active");
      lcd.setCursor(0, 1);
      lcd.print("Alerts");
      return;
    }
    if (current_alert_index >= alertCount) current_alert_index = 0;
    showAlertScreen(current_alert_index);
    return;
  }

  if (displayMode == MODE_NEW_ALERT) {
    if (newAlertQueuePos < newAlertQueueCount) {
      int idx = newAlertQueue[newAlertQueuePos];
      if (idx < alertCount) {
        showAlertScreen(idx);
        return;
      }
    }
    displayMode = MODE_SERVER_CYCLE;
  }

  if (newAlertQueuePos < newAlertQueueCount) {
    displayMode = MODE_NEW_ALERT;
    int idx = newAlertQueue[newAlertQueuePos];
    if (idx < alertCount) {
      showAlertScreen(idx);
      return;
    }
  }

  int server_screens = serverCount * 2;

  if (server_screens == 0) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Waiting for");
    lcd.setCursor(0, 1);
    lcd.print("server data...");
    return;
  }

  // Advance index FIRST, so duplicate rapid calls re-display the same screen
  current_screen_index = (current_screen_index + 1) % server_screens;

  lcd.clear();

  int srv_idx     = current_screen_index / 2;
  int screen_type = current_screen_index % 2;
  ServerStats s = serverStatsList[srv_idx];

  lcd.setCursor(0, 0);
  char row0[17];
  snprintf(row0, sizeof(row0), "%s|%s", s.name.c_str(), s.status.c_str());
  lcd.print(row0);

  if (s.status == "offline") {
    lcd.setCursor(0, 1);
    lcd.print("OFFLINE");
    current_screen_index = (srv_idx + 1) * 2 - 1;
    if (current_screen_index >= server_screens) current_screen_index = server_screens - 1;
    return;
  }

// Begin AI Generated Code
// Google Gemini 3.1 Pro

  lcd.setCursor(0, 1);

  if (screen_type == 0) {
    printCompactBar('C', min(100.0f, s.cpu * 100.0f));
    printCompactBar('R', s.ram);
  } else {
    int disk_pct = constrain((int)round(s.disk), 0, 99);
    char disk_buf[6];
    snprintf(disk_buf, sizeof(disk_buf), "D:%2d%%", disk_pct);
    lcd.print(disk_buf);
    lcd.print(' ');
    lcd.print("N:");
    lcd.write((byte)0);
    printNetRate(s.net_out);
    lcd.write((byte)1);
    printNetRate(s.net_in);
  }
}


void updateAlertScroll() {
  int text_len = alert_scroll_text.length();
  int max_offset = text_len - 16;
  
  if (max_offset <= 0) {
    alert_scroll_ticks++;
    if (alert_scroll_ticks >= 7) {
      finishAlertScroll();
    }
    return;
  }
  
  alert_scroll_ticks++;
  
  if (alert_scroll_offset == 0 && alert_scroll_ticks <= SCROLL_START_PAUSE) {
    return;
  }
  
  if (alert_scroll_offset >= max_offset) {
    alert_scroll_ticks++;
    if (alert_scroll_ticks >= SCROLL_END_PAUSE) {
      finishAlertScroll();
    }
    return;
  }
  
  alert_scroll_offset++;
  
  lcd.setCursor(0, 1);
  for (int i = 0; i < 16; i++) {
    int idx = alert_scroll_offset + i;
    if (idx < text_len) lcd.print(alert_scroll_text[idx]);
    else lcd.print(' ');
  }
  
  if (alert_scroll_offset >= max_offset) {
    alert_scroll_ticks = 0;
  }
}
// End AI Generated Code

void finishAlertScroll() {
  alert_scrolling_active = false;
  if (dataMutex != NULL && xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {

    if (displayMode == MODE_ALERT_VIEW) {
      current_alert_index = (current_alert_index + 1) % max(alertCount, 1);
    } else if (displayMode == MODE_NEW_ALERT) {
      newAlertQueuePos++;
      if (newAlertQueuePos >= newAlertQueueCount) {
        displayMode = MODE_SERVER_CYCLE;
        Serial.println("All new alerts displayed \u2014 returning to server cycle");
      }
    }

    last_lcd_cycle_time = millis();
    xSemaphoreGive(dataMutex);
  }
}

// Begin AI Generated Code
// Google Gemini 3.1 Pro

// --- Easter Egg: Dino Game ---
void initDinoGame() {
  dino_game_over = false;
  dino_y = 1;
  dino_jump_start = 0;
  dino_obstacle_x = 15;
  dino_obstacle_width = 1 + (random(3)); // 1, 2, or 3 blocks
  dino_score = 0;
  dino_update_interval = 250;
  dino_last_update_time = millis();
  dino_needs_redraw = true;
  lcd.clear();
}

void runDinoGame() {
  unsigned long current_time = millis();

  if (dino_y == 0) {
    if (current_time - dino_jump_start >= DINO_JUMP_DURATION) {
      dino_y = 1;
      dino_needs_redraw = true;
    }
  }

  if (button_isr_flag) {
    button_isr_flag = false;
    if (dino_game_over) {
      initDinoGame();
    } else if (dino_y == 1) {
      dino_y = 0;
      dino_jump_start = current_time;
      dino_needs_redraw = true;
      Serial.println("Dino JUMP!");
    }
  }

  if (alert_button_isr_flag) {
    alert_button_isr_flag = false;
  }

  if (!dino_game_over && (current_time - dino_last_update_time >= dino_update_interval)) {
    dino_last_update_time = current_time;

    dino_obstacle_x--;
    if (dino_obstacle_x + dino_obstacle_width <= 0) {
      dino_obstacle_x = 15;
      dino_obstacle_width = 1 + (random(3)); // 1, 2, or 3 blocks
      dino_score++;
      if (dino_update_interval > 100) {
        dino_update_interval -= 5;
      }
    }

    // Check collision: dino is at column 2, cactus spans [obstacle_x .. obstacle_x + width - 1]
    if (dino_y == 1 && dino_obstacle_x <= 2 && (dino_obstacle_x + dino_obstacle_width - 1) >= 2) {
      dino_game_over = true;
      Serial.printf("CRASH! Game Over. Score: %d\n", dino_score);
    }

    dino_needs_redraw = true;
  }

  if (dino_needs_redraw) {
    dino_needs_redraw = false;

    if (dino_game_over) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("GAME OVER!");
      lcd.setCursor(0, 1);
      char score_buf[17];
      snprintf(score_buf, sizeof(score_buf), "Score:%d D32:Play", dino_score);
      lcd.print(score_buf);
    } else {
      char row0[17] = "                ";
      char row1[17] = "                ";

      char score_str[7];
      snprintf(score_str, sizeof(score_str), "S:%03d", dino_score);
      memcpy(row0 + 10, score_str, 5);

      if (dino_y == 0) {
        row0[2] = (char)3;
      } else {
        row1[2] = (char)3;
      }

      for (int w = 0; w < dino_obstacle_width; w++) {
        int cx = dino_obstacle_x + w;
        if (cx >= 0 && cx < 16) {
          row1[cx] = (char)4;
        }
      }

      lcd.setCursor(0, 0);
      for (int i = 0; i < 16; i++) {
        if (row0[i] == (char)3) {
          lcd.write((byte)3);
        } else {
          lcd.print(row0[i]);
        }
      }

      lcd.setCursor(0, 1);
      for (int i = 0; i < 16; i++) {
        if (row1[i] == (char)3) {
          lcd.write((byte)3);
        } else if (row1[i] == (char)4) {
          lcd.write((byte)4);
        } else {
          lcd.print(row1[i]);
        }
      }
    }
  }
}
// End AI Generated Code


// --- Sleep Mode Functions ---
void enterSleepMode() {
  Serial.println("Entering sleep mode - screen off, polling paused.");
  sleep_mode = true;
  sleep_transition_time = millis();
  lcd.clear();
  lcd.noBacklight();
  digitalWrite(status_led, LOW);
}

void wakeFromSleep() {
  Serial.println("Waking up from sleep mode!");
  sleep_mode = false;
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Waking up...");
  delay(1000);

  // Reset to server cycling mode
  displayMode = MODE_SERVER_CYCLE;
  current_screen_index = 0;
  last_lcd_cycle_time = millis();
  sleep_transition_time = millis();

  // Clear any ISR flags triggered during the delay
  button_isr_flag = false;
  alert_button_isr_flag = false;

  if (dataMutex != NULL && xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
    cycleLcdScreen();
    xSemaphoreGive(dataMutex);
  }
}
