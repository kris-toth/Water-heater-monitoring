// for ESP32

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <NTPClient.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>


// Defines
#define MEASNUM 10
#define DTIME 50
#define REDLED 33

// WiFi
#define WIFI_SSID     "your_ssid" // Add wifi name
#define WIFI_PASSWORD "your_pw" // Add wifi passoword

#define ID "water-heater" // Add unique name for this sensor

#define INFLUX_HOST "host-address" // Influx host (e.g. eu-central-1-1.aws.cloud2.influxdata.com)
#define INFLUX_ORG_ID "org-id" // Org id
#define INFLUX_TOKEN "Token *token*" // Influx token
#define INFLUX_BUCKET "bucket-id" // Influx bucket that we set up for this host


// Variables
const long interval = DTIME * 1000;           // interval at which to blink (milliseconds)
unsigned long previousMillis = 0;        // will store last time LED was updated
static int fail = 0;

// NTP Client
WiFiUDP ntpUDP;
NTPClient ntpClient(ntpUDP);


// Loki & Infllux Clients
HTTPClient httpInflux;
HTTPClient httpLoki;
HTTPClient httpGraphite;


// Adafruit BMP280
Adafruit_BMP280 bmp1; //fűtés
Adafruit_BMP280 bmp2; //visszatérő
//TH06 - indirekt tároló



// Function to set up the connection to the WiFi AP
void setupWiFi() {
  Serial.print("Connecting to '");
  Serial.print(WIFI_SSID);
  Serial.print("' ...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  //    while (WiFi.status() != WL_CONNECTED) {
  //        delay(500);
  //        Serial.print(".");
  //    }

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  Serial.println("connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

/*
  // Function to submit metrics to Influx
  void submitToInflux(unsigned long ts, float temp_1, float temp_2, float temp_3)
  {
    String influxClient = String("https://") + INFLUX_HOST + "/api/v2/write?org=" + INFLUX_ORG_ID + "&bucket=" + INFLUX_BUCKET + "&precision=s";
    String body = String("temperature_1 value=") + temp_1 + " " + ts + "\n"
                        + "temperature_2 value=" + temp_2 + " " + ts + "\n"
                        + "temperature_3 value=" + temp_3 + " " + ts;

    // Submit POST request via HTTP
    httpInflux.begin(influxClient);
    httpInflux.addHeader("Authorization", INFLUX_TOKEN);
    int httpCode = httpInflux.POST(body);
    Serial.printf("Influx [HTTPS] POST...  Code: %d\n", httpCode);
    httpInflux.end();
  }
*/

// Function to submit logs to Loki
void submitToLoki(unsigned long ts, float temp_1, float temp_2, float temp_3)
{
  String lokiClient = String("https://") + LOKI_USER + ":" + LOKI_API_KEY + "@logs-prod-eu-west-0.grafana.net/loki/api/v1/push";
  String body = String("{\"streams\": [{ \"stream\": { \"plant_id\": \"2020_12_17\", \"monitoring_type\": \"avocado\"}, \"values\": [ [ \"") + ts + "000000000\", \""
                + "temperature_1=" + temp_1
                + " temperature_2=" + temp_2
                + " temperature_3=" + temp_3
                + "\" ] ] }]}";

  // Submit POST request via HTTP
  httpLoki.begin(lokiClient);
  httpLoki.addHeader("Content-Type", "application/json");
  int httpCode = httpLoki.POST(body);
  Serial.printf("Loki [HTTPS] POST...  Code: %\n", httpCode);
  httpLoki.end();
}


// Function to submit logs to Graphite
void submitToGraphite(unsigned long ts, float temp_1, float temp_2, float temp_3) {
  // build hosted metrics json payload
  String body = String("[") +
                "{\"name\":\"temperature_1\",\"interval\":" + DTIME + ",\"value\":" + temp_1 + ",\"mtype\":\"gauge\",\"time\":" + ts + "}," +
                "{\"name\":\"temperature_2\",\"interval\":" + DTIME + ",\"value\":" + temp_2 + ",\"mtype\":\"gauge\",\"time\":" + ts + "}," +
                "{\"name\":\"temperature_3\",\"interval\":" + DTIME + ",\"value\":" + temp_3 + ",\"mtype\":\"gauge\",\"time\":" + ts + "}]";

  // submit POST request via HTTP
  httpGraphite.begin("https://graphite-prod-01-eu-west-0.grafana.net/graphite/metrics");
  httpGraphite.setAuthorization(GRAPHITE_USER, GRAPHITE_API_KEY);
  httpGraphite.addHeader("Content-Type", "application/json");

  int httpCode = httpGraphite.POST(body);
  Serial.printf("Graphite [HTTPS] POST...  Code: %\n", httpCode);
  httpGraphite.end();
}


// Function to check if any reading failed
bool checkIfReadingFailed(float temp_1, float temp_2, float temp_3) {
  if (isnan(temp_1) || isnan(temp_2) || isnan(temp_3) || temp_1 < 0 || temp_1 > 100 || temp_2 < 0 || temp_2 > 100) {
    Serial.println(F("Failed to read from some sensor!"));
    return true;
  }
  return false;
}


// Read the sensor
float funcfuncmeasure(float (*measure)()) {
  int i = 0;
  float measurement;
  float minValue, maxValue, sum = 0, avg = 0;

  measurement = minValue = maxValue = (*measure)();

  for (i = 1; i < MEASNUM; i++) {
    measurement = (*measure)();
    if (measurement < minValue) minValue = measurement;
    if (measurement > maxValue) maxValue = measurement;
    sum += measurement;
    Serial.print(".");

    yield();

    delay(500);
  }
  Serial.println();

  sum = sum - minValue - maxValue;
  avg = sum / (MEASNUM - 2);
  return avg;
}


// Function for temperature measurement
float temperature_3_TH06() {
  float t = 0;
  int r;
  Wire.beginTransmission(0x40);
  Wire.write(0xE3);
  Wire.endTransmission();
  Wire.requestFrom(0x40, 2);
  if (Wire.available() > 0) {
    r = Wire.read();
    r = r << 8;
    r = r | Wire.read();
  }
  return t = (175.72 * r) / 65536.0 - 46.85;
}


// Function for temperature measurement
float temperature_1_BMP280() {
  return bmp1.readTemperature();
}


float temperature_2_BMP280() {
  return bmp2.readTemperature();
}

void OTA_Setup() {
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Port defaults to 3232
  // ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  // ArduinoOTA.setHostname("myesp32");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA
  .onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  })
  .onEnd([]() {
    Serial.println("\nEnd");
  })
  .onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  })
  .onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

}


// ========== MAIN FUNCTIONS: SETUP & LOOP ==========
// SETUP: Function called at boot to initialize the system
void setup() {
  pinMode(REDLED, OUTPUT);
  digitalWrite(33, LOW);

  // Start the serial output at 115,200 baud
  Serial.begin(115200);
  OTA_Setup();

  // initialize I2C
  Wire.begin();

  // initialize BMP280 sensor
  if (!bmp1.begin(0x76)) {
    Serial.println("Could not find first valid BMP280 sensor, check wiring!");
    while (1);
  }
  if (!bmp2.begin(0x77)) {
    Serial.println("Could not find second valid BMP280 sensor, check wiring!");
    while (1);
  }

  // Connect to WiFi
  //setupWiFi();

  // Initialize a NTPClient to get time
  ntpClient.begin();

  digitalWrite(33, HIGH);
  delay(400);
}




// LOOP: Function called in a loop to read from sensors and send them do databases
void loop() {
  unsigned long currentMillis = millis();
  ArduinoOTA.handle();

  if (currentMillis - previousMillis >= interval) {
    digitalWrite(33, LOW);

    // Reconnect to WiFi if required
    while (WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect();
      yield();
      setupWiFi();
    }

    // Update time via NTP if required
    while (!ntpClient.update()) {
      yield();
      ntpClient.forceUpdate();
    }

    // Get current timestamp
    unsigned long ts = ntpClient.getEpochTime();

    // Data
    float temp_1 = funcfuncmeasure(temperature_1_BMP280);
    float temp_2 = funcfuncmeasure(temperature_2_BMP280);
    float temp_3 = funcfuncmeasure(temperature_3_TH06);
    if (checkIfReadingFailed(temp_1, temp_2, temp_3)) {
      fail++;
      if (fail > 3) {
        ESP.restart();
      }
      return;
    }
    //String message = "hi";


    // Submit data
    //submitToInflux(ts, temp_1, temp_2, temp_3);
    submitToGraphite(ts, temp_1, temp_2, temp_3);
    submitToLoki(ts, temp_1, temp_2, temp_3);

    digitalWrite(33, HIGH);
    previousMillis = currentMillis;
  }

}
