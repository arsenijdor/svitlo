/* ═══════════════════════════════════════════════════════════════════════════
   СВІТЛО — прошивка датчика. УНІВЕРСАЛЬНА: ESP8266 та ESP32.
   ═══════════════════════════════════════════════════════════════════════════

   ЩО ЦЕ РОБИТЬ
   Плата живиться від тієї ж розетки, що й світло. Поки живлення є — вона раз на
   кілька хвилин підтверджує брокеру «я жива». Зникло живлення — плата померла
   разом зі світлом, і брокер сам публікує «offline». Застосунок це бачить і
   пише «СВІТЛА НЕМАЄ». Нічого опитувати не треба.

   ЯКІ ПЛАТИ ПІДХОДЯТЬ
   • ESP8266: Wemos D1 mini, NodeMCU V3, ESP-01 з перехідником.
   • ESP32: DevKit V1/WROOM-32, ESP32-C3, ESP32-S3 (у кого є LED_BUILTIN —
     лампочка блиматиме, у кого немає — просто працює без неї).
   Будь-яка з них із USB-роз'ємом живлення. Живлення — від звичайної зарядки 5 В
   (телефонної): шнур у роз'єм плати, зарядка в розетку.

   ⚠ МЕРЕЖА СТРОГО 2.4 ГГц. ESP8266 взагалі не вміє 5 ГГц, ESP32 зазвичай теж.
     Якщо роутер роздає одну назву і на 2.4, і на 5 ГГц — зроби окрему назву для
     2.4 ГГц, інакше плата може не підключитися.

   ───────────────────────────────────────────────────────────────────────────
   КРОК 1. ЗАПОВНИ ЦЕЙ БЛОК. УСЕ, ЩО ПОТРІБНО ВВЕСТИ, — ТУТ.
   ─────────────────────────────────────────────────────────────────────────── */

// ── ДАНІ ВАШОЇ МЕРЕЖІ WI-FI (2.4 ГГц) ───────────────────────────────────────
#define WIFI_SSID   "НАЗВА_МЕРЕЖІ"        // ← назва мережі, напр. "MyHomeWiFi"
#define WIFI_PASS   "ПАРОЛЬ_МЕРЕЖІ"       // ← пароль від Wi-Fi

// ── ТОПІК ПЛАТИ (це ваш особистий «пароль» каналу) ──────────────────────────
// Формат: svitlo/<місто>-<24 випадкові hex-символи>/state
// Приклад: svitlo/kyiv-1a2b3c4d5e6f7a8b9c0d1e2f/state  (вигаданий, не бери)
// Придумай свій — не копіюй приклад! Хто знає топік, той бачить твій стан
// і може його підробити (надіслати неправдиве «світло є»).
// Цей самий рядок треба вставити в застосунку: Налаштування → Топік плати.
#define TOPIC_STATE "svitlo/my-city-xxxxxxxxxxxxxxxxxxxxxxxx/state"

// ── БРОКЕР (залиш як є, якщо не знаєш, що це) ───────────────────────────────
#define MQTT_HOST   "broker.hivemq.com"   // публічний брокер: безкоштовно, без реєстрації
#define MQTT_PORT   1883

// ── ТОНКЕ НАЛАШТУВАННЯ (можна не чіпати) ────────────────────────────────────
#define KEEPALIVE_S     20                    // брокер помітить обрив за ~30 с
#define REPUBLISH_MS    (5UL * 60 * 1000UL)   // підтвердження «жива» раз на 5 хвилин
#define DEAD_MS         (10UL * 60 * 1000UL)  // зв'язку немає 10 хвилин → перезавантаження

/* ───────────────────────────────────────────────────────────────────────────
   КРОК 2. ПРОШИВКА
   Arduino IDE → Інструменти:
     • плата: «Wemos D1 mini» (ESP8266) або «ESP32 Dev Module»;
     • порт: той, що з'явився після підключення плати.
   Бібліотека: PubSubClient (Менеджер бібліотек → «PubSubClient» від Nick O'Leary).
   Кнопка «Завантажити». Швидкість у моніторі порту — 115200.

   КРОК 3. ПЕРЕВІРКА
   Відкрий монітор порту. Має бути:
     wifi ok, ip=192.168.0.x rssi=-55
     mqtt: online (retained)
   У застосунку той самий топік → картка стане «СВІТЛО Є».

   ЯКЩО НЕ ПРАЦЮЄ
   • «connect failed rc=-2» — плата не дісталася до мережі: перевір назву,
     пароль і що мережа 2.4 ГГц.
   • rc=-4 — не пускає брокер: перевір MQTT_HOST і що топік не порожній.
   • Висить на крапках у моніторі — слабкий сигнал або неправильний пароль.
   • Застосунок пише «НЕМАЄ ДАНИХ» — топік у застосунку не збігається з
     TOPIC_STATE (порівняй посимвольно, без пробілів).
   ─────────────────────────────────────────────────────────────────────────── */

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ESP32)
  #include <WiFi.h>
#else
  #error "Ця прошивка для ESP8266 та ESP32. Інший чіп не підійде."
#endif

#include <PubSubClient.h>

// ── світлодіод плати: в ESP8266 він активний низьким рівнем, в ESP32 — високим.
//    Якщо на платі його немає (ESP32-C3/S3 часто без нього), усе працює без нього.
//    Рівні визначаємо завжди: навіть якщо світлодіода немає, компілятор має їх
//    бачити — інакше led() не збереться.
#if defined(ESP8266)
  #define LED_ON  LOW
  #define LED_OFF HIGH
#else
  #define LED_ON  HIGH
  #define LED_OFF LOW
#endif

#ifdef LED_BUILTIN
  #define STATUS_LED LED_BUILTIN
#else
  #define STATUS_LED -1
#endif

WiFiClient  net;
PubSubClient mqtt(net);

unsigned long lastPublish = 0;
unsigned long lastOk      = 0;

void led(bool on) {
  if (STATUS_LED < 0) return;
  digitalWrite(STATUS_LED, on ? LED_ON : LED_OFF);
}

// в ESP8266 та ESP32 ідентифікатор чіпа дістають по-різному
uint32_t chipId() {
#if defined(ESP8266)
  return ESP.getChipId();
#else
  return (uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFFULL);
#endif
}

void connectMqtt() {
  if (mqtt.connected()) return;

  String clientId = "svitlo-" + String(chipId(), HEX);

  // останній аргумент — last will: брокер опублікує "offline" сам,
  // щойно помітить, що плата зникла (retain=true, qos=1)
  bool ok = mqtt.connect(clientId.c_str(), nullptr, nullptr,
                         TOPIC_STATE, 1, true, "offline");
  if (ok) {
    mqtt.publish(TOPIC_STATE, "online", true);   // retain: стан «на зараз»
    lastPublish = millis();
    lastOk      = millis();
    Serial.println(F("mqtt: online (retained)"));
  } else {
    Serial.print(F("mqtt: connect failed rc="));
    Serial.println(mqtt.state());
  }
}

// Перше підключення після Wi-Fi майже завжди потребує повтору: мережа ще не
// готова, і брокер відповідає rc=-2 (не додзвонилися). П'ять спроб із паузою
// прибирають мерехтіння з логу й зайві секунди без зв'язку.
void connectMqttWithRetries(uint8_t tries) {
  for (uint8_t i = 0; i < tries && !mqtt.connected(); i++) {
    connectMqtt();
    if (!mqtt.connected() && i + 1 < tries) delay(1500);
  }
}

void setup() {
  if (STATUS_LED >= 0) {
    pinMode(STATUS_LED, OUTPUT);
    led(false);
  }

  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.printf("\n=== svitlo: старт | чіп %08X | вільно %u байт ===\n",
                (unsigned)chipId(), (unsigned)ESP.getFreeHeap());

  if (strcmp(WIFI_SSID, "НАЗВА_МЕРЕЖІ") == 0) {
    Serial.println(F("!!! Не заповнений блок налаштувань угорі: WIFI_SSID / WIFI_PASS / TOPIC_STATE"));
  }

#if defined(ESP8266)
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
#endif
  WiFi.mode(WIFI_STA);
  WiFi.hostname("svitlo-hb");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print(F("підключаюсь до "));
  Serial.print(WIFI_SSID);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000UL) {
    delay(200);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("wifi ok, ip=%s rssi=%d\n",
                  WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
#if defined(ESP8266)
    WiFi.setSleepMode(WIFI_NONE_SLEEP);   // тримаємо з'єднання живим
#else
    WiFi.setSleep(false);
#endif
    delay(1000);   // даємо стеку підняти мережу: без цього перший MQTT дає rc=-2
  } else {
    Serial.println(F("wifi: не підключилось — перевір назву, пароль і діапазон 2.4 ГГц"));
  }

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setKeepAlive(KEEPALIVE_S);
  mqtt.setSocketTimeout(5);

  lastOk = millis();
  connectMqttWithRetries(5);
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
    led(true);                 // горить — отже зв'язок є, світло є
    lastOk = millis();
    if (millis() - lastPublish > REPUBLISH_MS) {
      mqtt.publish(TOPIC_STATE, "online", true);
      lastPublish = millis();
      Serial.printf("підтвердив online, вільно %u байт\n",
                    (unsigned)ESP.getFreeHeap());
    }
  }

  delay(50);
}
