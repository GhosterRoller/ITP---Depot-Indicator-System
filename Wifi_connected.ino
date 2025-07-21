#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

/* ─── 0) PER-BOARD IDENTIFIER ─── */
const char* TRAIN_ID = "Train A";   // ← set this to "Train B", "Train C", "Train H", etc.
/* ──────────────────────────────── */

/* ─── 1) YOUR WI-FI & STATIC IP ─── */
const char* SSID     = "TP-Link_5541";
const char* PASSWORD = "81221189";

IPAddress local_IP(192, 168, 0, 103);
IPAddress gateway(  192, 168, 0,   1);
IPAddress subnet(   255, 255, 255,  0);
IPAddress primaryDNS( 8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);
/* ───────────────────────────────── */

/* ─── 2) MQTT BROKER ─── */
const char* MQTT_BROKER  = "192.168.0.101";
const uint16_t MQTT_PORT = 1883;
/* ───────────────────── */

/* ─── 3) BUTTON PIN ─── */
const int BUTTON_PIN = 4;  // wire one leg → 3V3, other → GPIO4
/* ───────────────────── */

WiFiClient    wifiClient;
PubSubClient  mqttClient(wifiClient);

// will hold "train/<TRAIN_ID>/control/status"
char pubTopic[32];

void connectToWiFi() {
  Serial.printf("🔌 Applying static IP %s…\n", local_IP.toString().c_str());
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("⚠️ Static IP configuration failed");
  }
  Serial.printf("🔌 Connecting to Wi-Fi %s …\n", SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
    if (millis() - start > 20000) {
      Serial.println("\n❌ Wi-Fi timeout; restarting…");
      ESP.restart();
    }
  }
  Serial.printf("\n✅ Wi-Fi connected; IP = %s\n\n",
                WiFi.localIP().toString().c_str());
}

void connectToMQTT() {
  Serial.printf("☁️  Connecting to MQTT %s:%u …\n", MQTT_BROKER, MQTT_PORT);
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  // include TRAIN_ID in clientId for clarity
  String clientId = String("btn-") + TRAIN_ID + "-" + WiFi.macAddress();
  while (!mqttClient.connected()) {
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("✅ MQTT connected");
    } else {
      Serial.printf("🔄 Retry in 2 s… (rc=%d)\n", mqttClient.state());
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(1); }
  Serial.printf("\n🔧 Button board for %s starting…\n\n", TRAIN_ID);

  // build the topic string once
  snprintf(pubTopic, sizeof(pubTopic),
           "train/%s/control/status",
           TRAIN_ID);
  Serial.printf("➡️  Will publish override messages to: %s\n\n", pubTopic);

  pinMode(BUTTON_PIN, INPUT_PULLDOWN);

  connectToWiFi();
  connectToMQTT();

  Serial.println("🚦 Ready; press/release the button to send override messages.\n");
}

void loop() {
  if (!mqttClient.connected()) {
    Serial.println("⚠️  MQTT lost; reconnecting…");
    connectToMQTT();
  }
  mqttClient.loop();

  static int lastState = LOW;
  int currentState = digitalRead(BUTTON_PIN);

  if (currentState != lastState) {
    delay(50);  // debounce
    currentState = digitalRead(BUTTON_PIN);
    if (currentState != lastState) {
      lastState = currentState;
      bool ignoreOn = (currentState == HIGH);

      // clear press/release printout
      if (ignoreOn) {
        Serial.println("🔔 Button PRESSED detected!");
      } else {
        Serial.println("🔔 Button RELEASED detected!");
      }

      // existing status line
      Serial.printf("🔔 Button %s → ignoreCommands=%s\n",
                    ignoreOn ? "PRESSED" : "RELEASED",
                    ignoreOn ? "true"    : "false");

      // publish JSON { "ignoreCommands": true/false }
      StaticJsonDocument<32> doc;
      doc["ignoreCommands"] = ignoreOn;
      char buf[32];
      size_t len = serializeJson(doc, buf);

      bool ok = mqttClient.publish(
        pubTopic,
        reinterpret_cast<const uint8_t*>(buf),
        len,
        /*retain=*/true
      );
      Serial.printf("%s Published to %s\n\n",
                    ok ? "✅" : "❌",
                    pubTopic);
    }
  }
}
