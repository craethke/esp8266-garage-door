#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>

#include "secrets.h"

const int REED_SWITCH_PIN = D5;
const int OPEN_PIN_VALUE = HIGH;
const int CLOSED_PIN_VALUE = LOW;
const char* GARAGE_DOOR_STATE_TOPIC = "garagedoor/state";
const char* OPEN_STATE = "open";
const char* CLOSED_STATE = "closed";
const char* CLOSING_STATE = "closing";
const char* GARAGE_DOOR_STATE_REFRESH_TOPIC = "garagedoor/state/refresh";

// GPIO 4(D2) and 5(D1) are the most safe to use to operate relays
// According to
// https://acrobotic.com/blogs/learning/wemos-d1-mini-esp8266-relay-shield D1 is
// used by the relay GPIO 12(D6), 13(D7), and 14 (D5) are safe also
const int RELAY_PIN = D1;
const char* GARAGE_DOOR_ACTION_TOPIC = "garagedoor/set";
const int PRESS_BUTTON_TIME = 1000;

const char* ssid = SECRET_WIFI_SSID;
const char* password = SECRET_WIFI_PASSWORD;
const char* mqttServer = SECRET_MQTT_SERVER;
const int mqttPort = 1883;
const char* mqttUser = SECRET_MQTT_USER;
const char* mqttPassword = SECRET_MQTT_PASSWORD;

int previousValue = OPEN_PIN_VALUE;
boolean closing = false;

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  Serial.begin(9600);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.hostname("ESP-Garage");

  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  Serial.println("Connected to the WiFi network");

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() { Serial.println("\nEnd"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  pinMode(REED_SWITCH_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
}

void loop() {
  ArduinoOTA.handle();

  if (!client.connected()) {
    reconnect();
  }

  int currentValue = digitalRead(REED_SWITCH_PIN);
  if (currentValue != previousValue) {
    Serial.print("door is now ");
    Serial.println(currentValue == OPEN_PIN_VALUE ? OPEN_STATE : CLOSED_STATE);
    client.publish(GARAGE_DOOR_STATE_TOPIC,
                   currentValue == OPEN_PIN_VALUE ? OPEN_STATE : CLOSED_STATE);
    previousValue = currentValue;
    closing = false;
  }

  client.loop();
}

void reconnect() {
  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");
    if (client.connect("GarageDoorController", mqttUser, mqttPassword)) {
      Serial.println("connected");
      client.subscribe(GARAGE_DOOR_ACTION_TOPIC);
      client.subscribe(GARAGE_DOOR_STATE_REFRESH_TOPIC);
    } else {
      Serial.print("failed with state ");
      Serial.print(client.state());
      delay(2000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  char message[length];
  memcpy(message, payload, length + 1);
  message[length] = '\0';

  Serial.print("Message arrived: ");
  Serial.println(message);
  Serial.println("-----------------------");

  if (strcmp(topic, GARAGE_DOOR_ACTION_TOPIC) == 0) {
    if (strcmp(message, "OPEN") == 0) {
      if (previousValue == CLOSED_PIN_VALUE || closing) {
        if (closing) {
          client.publish(GARAGE_DOOR_STATE_TOPIC, OPEN_STATE);
        }
        closing = false;
        Serial.println("opening door");
        pressButton();
      }
    } else if (strcmp(message, "CLOSE") == 0) {
      if (previousValue == OPEN_PIN_VALUE && !closing) {
        closing = true;
        client.publish(GARAGE_DOOR_STATE_TOPIC, CLOSING_STATE);
        Serial.println("closing door");
        pressButton();
      }
    }
  } else if (strcmp(topic, GARAGE_DOOR_STATE_REFRESH_TOPIC) == 0) {
    int currentValue = digitalRead(REED_SWITCH_PIN);
    client.publish(GARAGE_DOOR_STATE_TOPIC,
                   currentValue == OPEN_PIN_VALUE ? OPEN_STATE : CLOSED_STATE);
  }
}

void pressButton() {
  digitalWrite(RELAY_PIN, HIGH);
  delay(PRESS_BUTTON_TIME);
  digitalWrite(RELAY_PIN, LOW);
}
