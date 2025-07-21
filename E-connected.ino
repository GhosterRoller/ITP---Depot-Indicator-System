#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

/* ——— 1) PER-BOARD IDENTIFIER ——— */
const char* TRAIN_ID    = "Train A";    // ← change per-board
/* ───────────────────────────────── */

const char* SSID        = "TP-Link_5541";
const char* PASSWORD    = "81221189";
const char* MQTT_BROKER = "192.168.0.101";
const uint16_t MQTT_PORT= 1883;

// Topics (filled in setup)
char statusTopic[32], overrideTopic[32];

// LED pins
const int LED1                   = 4;
const int LED2                   = 5;
const int LED3                   = 6;
const int LED_MOVEMENT_ALLOWED   = 7;
const int LED_DO_NOT_POWER_ON    = 15;
const int LED_SHED_PLUG_FRONT    = 16;
const int LED_SHED_PLUG_REAR     = 17;
const int LED_WHEEL_CHOCK        = 21;
const int LED_COMPONENT_REMOVED  = 1;
const int LED_UNDERCARRIAGE_WORK = 2;
const int LED_ROOF_WORK          = 18;
const int LED_EMERGENCY          = 8;

// Buzzer pin (wired between 3.3 V and GPIO 13 via ~47 Ω)
const int BUZZER_PIN             = 13;

const int ALL_LEDS[] = {
  LED1, LED2, LED3,
  LED_MOVEMENT_ALLOWED,
  LED_DO_NOT_POWER_ON,
  LED_SHED_PLUG_FRONT,
  LED_SHED_PLUG_REAR,
  LED_WHEEL_CHOCK,
  LED_COMPONENT_REMOVED,
  LED_UNDERCARRIAGE_WORK,
  LED_ROOF_WORK
};

WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

// override flag
bool ignoreCommands    = false;

// store last valid status
struct TrainStatus {
  int  partiesInside;
  bool movementAllowed;
  bool doNotPowerOn;
  bool shedPlugFront;
  bool shedPlugRear;
  bool wheelChock;
  bool componentRemoved;
  bool undercarriageWork;
  bool roofWork;
} savedStatus;
bool savedStatusValid = false;

// buzzer timing
unsigned long lastBeepTime = 0;
bool          buzzerOn     = false;
const unsigned long beepPeriod     = 2000;  // ms between beep starts
const unsigned long beepOnDuration =  200;  // ms length of each beep

//─────────────────────────────────────────────────────────────────────────────
// Apply a saved TrainStatus to the LEDs
void applyStatus(const TrainStatus &st) {
  digitalWrite(LED1, st.partiesInside >= 1 ? HIGH : LOW);
  digitalWrite(LED2, st.partiesInside >= 2 ? HIGH : LOW);
  digitalWrite(LED3, st.partiesInside >= 3 ? HIGH : LOW);

  digitalWrite(LED_MOVEMENT_ALLOWED,   st.movementAllowed   ? HIGH : LOW);
  digitalWrite(LED_DO_NOT_POWER_ON,    st.doNotPowerOn      ? HIGH : LOW);
  digitalWrite(LED_SHED_PLUG_FRONT,    st.shedPlugFront     ? HIGH : LOW);
  digitalWrite(LED_SHED_PLUG_REAR,     st.shedPlugRear      ? HIGH : LOW);
  digitalWrite(LED_WHEEL_CHOCK,        st.wheelChock        ? HIGH : LOW);
  digitalWrite(LED_COMPONENT_REMOVED,  st.componentRemoved  ? HIGH : LOW);
  digitalWrite(LED_UNDERCARRIAGE_WORK, st.undercarriageWork ? HIGH : LOW);
  digitalWrite(LED_ROOF_WORK,          st.roofWork          ? HIGH : LOW);
}

//─────────────────────────────────────────────────────────────────────────────
// Wi-Fi connect
void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  unsigned long start = millis();
  Serial.printf("🔌 Connecting to Wi-Fi %s …", SSID);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(500);
    if (millis() - start > 20000) {
      Serial.println("\n❌ Wi-Fi timeout; restarting…");
      ESP.restart();
    }
  }
  Serial.printf("\n✅ Wi-Fi connected; IP = %s\n\n", WiFi.localIP().toString().c_str());
}

//─────────────────────────────────────────────────────────────────────────────
// MQTT connect & subscribe
void connectToMQTT() {
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  String clientId = String("esp32-s3-") + TRAIN_ID + "-" + WiFi.macAddress();
  Serial.printf("☁️  Connecting to MQTT %s:%u …\n", MQTT_BROKER, MQTT_PORT);
  while (!mqttClient.connected()) {
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("✅ MQTT connected");
      mqttClient.subscribe(statusTopic);
      mqttClient.subscribe(overrideTopic);
    } else {
      Serial.printf("❌ MQTT failed (rc=%d); retrying in 2s…\n", mqttClient.state());
      delay(2000);
    }
  }
}

//─────────────────────────────────────────────────────────────────────────────
// MQTT message handler
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  // 1) Override topic?
  if (strcmp(topic, overrideTopic) == 0) {
    static char bufO[128];
    unsigned int mO = min(len, sizeof(bufO)-1);
    memcpy(bufO, payload, mO); bufO[mO] = '\0';

    StaticJsonDocument<128> docO;
    if (!deserializeJson(docO, bufO)) {
      ignoreCommands = docO["ignoreCommands"].as<bool>();
      Serial.printf("🚦 ignoreCommands = %s\n", ignoreCommands?"true":"false");

      if (ignoreCommands) {
        // Turn off all non-emergency LEDs
        for (int p : ALL_LEDS) digitalWrite(p, LOW);
        digitalWrite(LED_EMERGENCY, HIGH);
        // Reset buzzer cycle
        buzzerOn     = false;
        lastBeepTime = millis();
      } else {
        // Exit override
        digitalWrite(LED_EMERGENCY, LOW);
        // Silence buzzer
        digitalWrite(BUZZER_PIN, HIGH);
        buzzerOn = false;
        // Restore previous status
        if (savedStatusValid) applyStatus(savedStatus);
      }
    }
    return;
  }

  // 2) Status topic—ignore if overridden
  if (ignoreCommands) return;

  // 3) Parse status JSON
  static char buf[512];
  unsigned int m = min(len, sizeof(buf)-1);
  memcpy(buf, payload, m); buf[m] = '\0';

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, buf)) return;

  // Save & apply
  savedStatus.partiesInside     = constrain(doc["partiesInside"]|0, 0, 3);
  savedStatus.movementAllowed   = doc["movementAllowed"].as<bool>();
  savedStatus.doNotPowerOn      = doc["doNotPowerOn"].as<bool>();
  savedStatus.shedPlugFront     = doc["shedPlugFront"].as<bool>();
  savedStatus.shedPlugRear      = doc["shedPlugRear"].as<bool>();
  savedStatus.wheelChock        = doc["wheelChock"].as<bool>();
  savedStatus.componentRemoved  = doc["componentRemoved"].as<bool>();
  savedStatus.undercarriageWork = doc["undercarriageWork"].as<bool>();
  savedStatus.roofWork          = doc["roofWork"].as<bool>();
  savedStatusValid              = true;

  applyStatus(savedStatus);
}

//─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(1);
  Serial.printf("\n🔌 %s main board starting…\n\n", TRAIN_ID);

  // Initialize LEDs
  for (int p : ALL_LEDS) {
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  pinMode(LED_EMERGENCY, OUTPUT);
  digitalWrite(LED_EMERGENCY, LOW);

  // Initialize buzzer pin—HIGH = silent
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH);

  // Build MQTT topic strings
  snprintf(statusTopic,  sizeof(statusTopic),
           "train/%s/status",         TRAIN_ID);
  snprintf(overrideTopic, sizeof(overrideTopic),
           "train/%s/control/status", TRAIN_ID);

  // Wire up MQTT callback & connections
  mqttClient.setCallback(mqttCallback);
  connectToWiFi();
  connectToMQTT();

  Serial.printf("✅ Subscribed to:\n • %s\n • %s\n\n", statusTopic, overrideTopic);
}

//─────────────────────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // Handle buzzer beeps if in override mode
  if (ignoreCommands) {
    if (!buzzerOn && (now - lastBeepTime) >= beepPeriod) {
      digitalWrite(BUZZER_PIN, LOW);   // turn buzzer ON
      buzzerOn     = true;
      lastBeepTime = now;
    }
    else if (buzzerOn && (now - lastBeepTime) >= beepOnDuration) {
      digitalWrite(BUZZER_PIN, HIGH);  // turn buzzer OFF
      buzzerOn     = false;
      lastBeepTime = now;
    }
  }

  // Maintain connections
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ Wi-Fi lost; reconnecting…");
    connectToWiFi();
  }
  if (!mqttClient.connected()) {
    Serial.println("⚠️ MQTT lost; reconnecting…");
    connectToMQTT();
  }

  mqttClient.loop();
}
