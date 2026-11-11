// svitlo_hb — датчик наличия света для приложения «Світло».
// Публичная версия: без чужих данных сети и без чужого топика.
//
// Транспорт: MQTT, публичный брокер HiveMQ (без аккаунта и без квоты).
//
// Почему не ntfy: у публичного ntfy лимит 250 сообщений в сутки на IP,
// а heartbeat раз в 30 секунд — это 2880. Лишние отбиваются кодом 429,
// и приложение видит тишину — то есть врёт, что света нет. MQTT таких
// квот не имеет, а главное — умеет last will.
//
// Схема:
//   * при подключении публикуем retained-сообщение "online";
//   * там же регистрируем last will — "offline" (retained);
//   * питание пропало -> плата умерла -> TCP умер -> брокер сам публикует
//     "offline". Приложению достаточно быть подписанным, ничего не опрашиваем.

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// ─── что нужно заполнить ─────────────────────────────────────────────────────
// 1. Сеть строго 2.4 ГГц (ESP8266 не умеет 5 ГГц).
#define WIFI_SSID  "НАЗВА_МЕРЕЖІ"
#define WIFI_PASS  "ПАРОЛЬ_МЕРЕЖІ"

// 2. Свой топик. Имя топика — фактически пароль: кто его знает, тот видит
//    состояние дома и может его подделать. Никому не публикуй.
//    Придумать: svitlo/<місто>-<24 випадкові hex>/state
#define TOPIC_STATE "svitlo/my-city-xxxxxxxxxxxxxxxxxxxxxxxx/state"

// ─── настройки ───────────────────────────────────────────────────────────────
#define MQTT_HOST  "broker.hivemq.com"
#define MQTT_PORT  1883

#define KEEPALIVE_S     20                   // брокер заметит обрыв за ~30 с
#define REPUBLISH_MS    (5UL * 60 * 1000UL)  // обновляем retained раз в 5 минут
#define DEAD_MS         (10UL * 60 * 1000UL) // нет связи 10 минут -> перезагрузка

const int LED = LED_BUILTIN;                 // GPIO2, активный уровень LOW

WiFiClient net;
PubSubClient mqtt(net);

unsigned long lastPublish = 0;
unsigned long lastOk = 0;

void led(bool on) { digitalWrite(LED, on ? LOW : HIGH); }

void connectMqtt() {
  if (mqtt.connected()) return;
  String clientId = "svitlo-" + String(ESP.getChipId(), HEX);
  // last will: retain=true, qos=1, payload="offline"
  bool ok = mqtt.connect(clientId.c_str(), nullptr, nullptr,
                         TOPIC_STATE, 1, true, "offline");
  if (ok) {
    mqtt.publish(TOPIC_STATE, "online", true);
    lastPublish = millis();
    lastOk = millis();
    Serial.println(F("mqtt: online (retained)"));
  } else {
    Serial.printf("mqtt: connect failed rc=%d\n", mqtt.state());
  }
}

void setup() {
  pinMode(LED, OUTPUT);
  led(false);

  Serial.begin(115200);
  Serial.println();
  Serial.printf("\n=== svitlo_hb mqtt boot | chip %08X | free %u ===\n",
                ESP.getChipId(), ESP.getFreeHeap());

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.hostname("svitlo-hb");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000UL) {
    delay(200);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("wifi ok, ip=%s rssi=%d\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
  }

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setKeepAlive(KEEPALIVE_S);
  mqtt.setSocketTimeout(5);
  lastOk = millis();
  connectMqtt();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    led(false);
    if (millis() - lastOk > DEAD_MS) ESP.restart();
    delay(500);
    return;
  }

  if (!mqtt.connected()) {
    led(false);
    connectMqtt();
    if (!mqtt.connected() && millis() - lastOk > DEAD_MS) ESP.restart();
    delay(1000);
    return;
  }

  mqtt.loop();

  if (mqtt.connected()) {
    led(true);
    lastOk = millis();
    if (millis() - lastPublish > REPUBLISH_MS) {
      mqtt.publish(TOPIC_STATE, "online", true);
      lastPublish = millis();
      Serial.printf("republish online, free=%u\n", ESP.getFreeHeap());
    }
  }

  delay(50);
}
