#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ZMPT101B.h>
#include <ACS712.h>

// ----------------------------------------------------------------
//  WiFi Credentials
// ----------------------------------------------------------------
const char* WIFI_SSID     = "Samfred";
const char* WIFI_PASSWORD = "samfred224";

// ----------------------------------------------------------------
//  Supabase Credentials
// ----------------------------------------------------------------
const char* SUPABASE_URL  = "nulsiefrwxsrxwtwrkva.supabase.co";  // host only, no https
const char* SUPABASE_PATH = "/rest/v1/device_telemetry";
const char* SUPABASE_KEY  = "sb_publishable_amwx5_Ce4-oQCgQNwfxObA_pmUBTxTE";
const char* DEVICE_ID     = "PERWER-001";

// ----------------------------------------------------------------
//  A9 GPRS Settings
// ----------------------------------------------------------------
#define A9_RX_PIN       19        // ESP32 GPIO 19 → receives from A9 TX
#define A9_TX_PIN       20        // ESP32 GPIO 20 → sends to A9 RX
#define A9_BAUD         115200
#define GSM_APN         "internet" // MTN Nigeria APN

HardwareSerial A9Serial(1);       // Use UART1 for A9

// ----------------------------------------------------------------
//  Sensor Pins
// ----------------------------------------------------------------
#define ZMPT101B_PIN      1
#define ACS712_PIN        2
#define ACS758_PV_PIN     3
#define ACS758_BATT_PIN   4
#define VOLT_PV_PIN       5
#define VOLT_BATT_PIN     6
#define LDR_PIN           7
#define LCD_SDA           8
#define LCD_SCL           9
#define DS18B20_PIN       10
#define DHT_PIN           11
#define GREEN_LED_PIN     16
#define YELLOW_LED_PIN    17
#define BUZZER_PIN        18


// Forward declaration
void lcdPrint(String line1, String line2);
// ----------------------------------------------------------------
//  Sensor Objects
// ----------------------------------------------------------------
ZMPT101B          voltageSensor(ZMPT101B_PIN, 50);
ACS712            acCurrentSensor(ACS712_PIN,       3.3, 30, 66);
ACS712            pvCurrentSensor(ACS758_PV_PIN,    3.3, 50, 40);
ACS712            batCurrentSensor(ACS758_BATT_PIN, 3.3, 50, 40);
OneWire           oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
DHT               dht(DHT_PIN, DHT22);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ----------------------------------------------------------------
//  Calibration
// ----------------------------------------------------------------
#define ZMPT101B_SENSITIVITY  500.0f
#define VOLT_PV_CORRECTION    0.80
#define VOLT_BATT_CORRECTION  0.80
#define AC_VOLT_NOISE_FLOOR   10.0
#define AC_CURR_NOISE_FLOOR   0.15
#define DC_CURR_NOISE_FLOOR   0.20

// ----------------------------------------------------------------
//  Alert Thresholds
// ----------------------------------------------------------------
#define PV_TEMP_WARN          60.0
#define PV_TEMP_CRITICAL      75.0
#define BATT_TEMP_WARN        45.0
#define BATT_TEMP_CRITICAL    50.0
#define BATT_VOLT_LOW         11.5
#define BATT_VOLT_CRITICAL    11.0
#define AC_VOLT_MIN           200.0
#define AC_VOLT_MAX           240.0

// ----------------------------------------------------------------
//  Timing
// ----------------------------------------------------------------
#define SEND_INTERVAL_MS    30000
#define LCD_SCROLL_MS       3000
#define SAMPLE_INTERVAL_MS  100

unsigned long lastSendTime   = 0;
unsigned long lastScrollTime = 0;
int           lcdPage        = 0;

// ----------------------------------------------------------------
//  Global Sensor Values
// ----------------------------------------------------------------
float pvVoltage   = 0, pvCurrent  = 0, pvPower   = 0;
float pvTemp      = 0, irradiance = 0;
float acVoltage   = 0, acCurrent  = 0, acPower   = 0;
float frequency   = 0, energyKwh  = 0;
float battVoltage = 0, battCurrent= 0, battPower = 0;
float battTemp    = 0, ambTemp    = 0, humidity  = 0;
String battState    = "idle";
bool  alertActive   = false;
bool  criticalAlert = false;

// ================================================================
//  A9 GPRS HELPERS
// ================================================================

// Send AT command and wait for expected response
bool a9SendAT(const char* cmd, const char* expected, unsigned long timeout = 5000) {
  A9Serial.println(cmd);
  Serial.printf("[A9 >>] %s\n", cmd);

  unsigned long start = millis();
  String response = "";

  while (millis() - start < timeout) {
    while (A9Serial.available()) {
      char c = A9Serial.read();
      response += c;
    }
    if (response.indexOf(expected) != -1) {
      Serial.printf("[A9 <<] %s\n", response.c_str());
      return true;
    }
  }
  Serial.printf("[A9 TIMEOUT] Expected: %s Got: %s\n", expected, response.c_str());
  return false;
}

// Read full response from A9
String a9ReadResponse(unsigned long timeout = 3000) {
  unsigned long start = millis();
  String response = "";
  while (millis() - start < timeout) {
    while (A9Serial.available()) {
      response += (char)A9Serial.read();
    }
  }
  return response;
}

// Initialize A9 and connect to GPRS
bool a9InitGPRS() {
  Serial.println("[A9] Initializing GPRS...");
  lcdPrint("A9 GPRS", "Initializing...");

  // Test AT
  if (!a9SendAT("AT", "OK", 3000)) {
    Serial.println("[A9] No response to AT");
    return false;
  }

  // Disable echo
  a9SendAT("ATE0", "OK", 2000);

  // Check SIM
  if (!a9SendAT("AT+CIMI", "OK", 3000)) {
    Serial.println("[A9] No SIM detected");
    lcdPrint("A9 Error", "No SIM card!");
    return false;
  }

  // Wait for network registration (up to 30 seconds)
  Serial.println("[A9] Waiting for network...");
  lcdPrint("A9 GPRS", "Finding network..");
  bool registered = false;
  for (int i = 0; i < 10; i++) {
    A9Serial.println("AT+CREG?");
    delay(1000);
    String resp = a9ReadResponse(2000);
    // +CREG: 0,1 = registered home, +CREG: 0,5 = roaming
    if (resp.indexOf(",1") != -1 || resp.indexOf(",5") != -1) {
      registered = true;
      Serial.println("[A9] Network registered!");
      break;
    }
    Serial.printf("[A9] Not registered yet... attempt %d/10\n", i + 1);
    delay(2000);
  }

  if (!registered) {
    Serial.println("[A9] Network registration failed");
    lcdPrint("A9 Error", "No network!");
    return false;
  }

  // Set APN and start GPRS
  a9SendAT("AT+CGATT=1", "OK", 5000);                              // Attach GPRS
  a9SendAT("AT+CGDCONT=1,\"IP\",\"" GSM_APN "\"", "OK", 5000);   // Set APN
  a9SendAT("AT+CGACT=1,1", "OK", 10000);                           // Activate PDP context

  // Get IP — confirms GPRS is connected
  A9Serial.println("AT+CGPADDR=1");
  String ipResp = a9ReadResponse(5000);
  Serial.printf("[A9] IP Response: %s\n", ipResp.c_str());

  if (ipResp.indexOf("0.0.0.0") != -1 || ipResp.indexOf("ERROR") != -1) {
    Serial.println("[A9] GPRS connection failed");
    lcdPrint("A9 Error", "GPRS failed!");
    return false;
  }

  Serial.println("[A9] GPRS connected!");
  lcdPrint("A9 GPRS", "Connected! MTN");
  delay(1000);
  return true;
}

// Send HTTP POST via A9 GPRS using raw TCP socket
bool a9SendHTTP(const String& payload) {
  Serial.println("[A9] Sending via GPRS...");
  lcdPrint("Sending GPRS", "Please wait...");

  int payloadLen = payload.length();

  // Build HTTP request manually
  String httpRequest =
    "POST " + String(SUPABASE_PATH) + " HTTP/1.1\r\n"
    "Host: " + String(SUPABASE_URL) + "\r\n"
    "Content-Type: application/json\r\n"
    "apikey: " + String(SUPABASE_KEY) + "\r\n"
    "Authorization: Bearer " + String(SUPABASE_KEY) + "\r\n"
    "Prefer: return=minimal\r\n"
    "Content-Length: " + String(payloadLen) + "\r\n"
    "Connection: close\r\n"
    "\r\n" +
    payload;

  // Open TCP connection to Supabase
  String connectCmd = "AT+CIPSTART=\"TCP\",\"" + String(SUPABASE_URL) + "\",80";
  if (!a9SendAT(connectCmd.c_str(), "CONNECT", 15000)) {
    Serial.println("[A9] TCP connect failed");
    a9SendAT("AT+CIPCLOSE", "OK", 3000);
    return false;
  }

  delay(500);

  // Send data length
  String sendCmd = "AT+CIPSEND=" + String(httpRequest.length());
  A9Serial.println(sendCmd);
  delay(2000);

  // Wait for > prompt then send data
  String prompt = a9ReadResponse(3000);
  if (prompt.indexOf(">") != -1 || true) {  // send anyway
    A9Serial.print(httpRequest);
    A9Serial.write(26);  // Ctrl+Z to end sending
  }

  // Read response
  String response = a9ReadResponse(10000);
  Serial.printf("[A9 HTTP Response] %s\n", response.c_str());

  // Close connection
  a9SendAT("AT+CIPCLOSE", "OK", 3000);

  // Check for HTTP 201 Created
  if (response.indexOf("201") != -1) {
    Serial.println("[A9] Data sent via GPRS!");
    lcdPrint("GPRS Send", "Success!");
    return true;
  } else {
    Serial.println("[A9] GPRS send failed");
    lcdPrint("GPRS Send", "Failed!");
    return false;
  }
}

// ================================================================
//  HELPER: LCD Print Two Lines
// ================================================================
void lcdPrint(String line1, String line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  while (line1.length() < 16) line1 += " ";
  lcd.print(line1.substring(0, 16));
  lcd.setCursor(0, 1);
  while (line2.length() < 16) line2 += " ";
  lcd.print(line2.substring(0, 16));
}

// ================================================================
//  HELPER: Read DS18B20 safely
// ================================================================
float readDS18B20(int index) {
  float temp = ds18b20.getTempCByIndex(index);
  return (temp == DEVICE_DISCONNECTED_C) ? -99.0 : temp;
}

// ================================================================
//  HELPER: Read DC Voltage
// ================================================================
float readDCVoltage(int pin, float correction) {
  long sum = 0;
  for (int i = 0; i < 50; i++) {
    sum += analogReadMilliVolts(pin);
    delayMicroseconds(200);
  }
  float voltage = (sum / 50.0 / 3300.0) * 25.0 * correction;
  return (voltage < 0.1) ? 0.0 : voltage;
}

// ================================================================
//  HELPER: AC Frequency
// ================================================================
float measureFrequency() {
  unsigned long startTime = millis();
  int  crossings = 0;
  bool lastAbove = analogReadMilliVolts(ZMPT101B_PIN) > 1650;
  while (millis() - startTime < 500) {
    bool nowAbove = analogReadMilliVolts(ZMPT101B_PIN) > 1650;
    if (nowAbove != lastAbove) { crossings++; lastAbove = nowAbove; }
  }
  return (crossings / 2.0) / 0.5;
}

// ================================================================
//  HELPER: Build JSON Payload
// ================================================================
String buildPayload() {
  StaticJsonDocument<1024> doc;
  doc["device_id"]        = DEVICE_ID;
  doc["pv_voltage"]       = round(pvVoltage   * 100) / 100.0;
  doc["pv_current"]       = round(pvCurrent   * 100) / 100.0;
  doc["pv_power"]         = round(pvPower     * 100) / 100.0;
  doc["pv_temperature"]   = round(pvTemp      * 100) / 100.0;
  doc["irradiance"]       = round(irradiance  * 10)  / 10.0;
  doc["ac_voltage"]       = round(acVoltage   * 100) / 100.0;
  doc["ac_current"]       = round(acCurrent   * 100) / 100.0;
  doc["ac_power"]         = round(acPower     * 100) / 100.0;
  doc["frequency"]        = round(frequency   * 10)  / 10.0;
  doc["power_factor"]     = nullptr;
  doc["energy_kwh"]       = round(energyKwh   * 100) / 100.0;
  doc["batt_voltage"]     = round(battVoltage * 100) / 100.0;
  doc["batt_current"]     = round(battCurrent * 100) / 100.0;
  doc["batt_power"]       = round(battPower   * 100) / 100.0;
  doc["batt_temperature"] = round(battTemp    * 100) / 100.0;
  doc["batt_state"]       = battState;
  doc["amb_temperature"]  = round(ambTemp     * 100) / 100.0;
  doc["humidity"]         = round(humidity    * 100) / 100.0;
  String payload;
  serializeJson(doc, payload);
  return payload;
}

// ================================================================
//  SEND DATA — WiFi first, fallback to GPRS
// ================================================================
void sendData() {
  String payload = buildPayload();

  // ── Try WiFi first ────────────────────────────────────────────
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[NET] Sending via WiFi...");
    HTTPClient http;
    http.begin("https://" + String(SUPABASE_URL) + String(SUPABASE_PATH));
    http.addHeader("Content-Type",  "application/json");
    http.addHeader("apikey",        SUPABASE_KEY);
    http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
    http.addHeader("Prefer",        "return=minimal");
    int code = http.POST(payload);
    http.end();

    if (code == 201) {
      Serial.println("[OK] Sent via WiFi!");
      lcdPrint("WiFi Send", "Success!");
      digitalWrite(GREEN_LED_PIN, LOW); delay(100);
      digitalWrite(GREEN_LED_PIN, HIGH);
      return;  // Done — no need for GPRS
    } else {
      Serial.printf("[WARN] WiFi send failed HTTP %d — trying GPRS\n", code);
    }
  } else {
    Serial.println("[NET] WiFi offline — trying GPRS");
  }

  // ── Fallback to A9 GPRS ───────────────────────────────────────
  if (a9InitGPRS()) {
    a9SendHTTP(payload);
  } else {
    Serial.println("[ERROR] Both WiFi and GPRS failed!");
    lcdPrint("Send Failed!", "No connection");
  }
}

// ================================================================
//  HELPER: Alerts
// ================================================================
void checkAlerts() {
  alertActive   = false;
  criticalAlert = false;
  String alertMsg = "";

  if (pvTemp      >= PV_TEMP_CRITICAL)                      { criticalAlert = true; alertMsg = "PV TEMP CRIT!"; }
  if (battTemp    >= BATT_TEMP_CRITICAL)                    { criticalAlert = true; alertMsg = "BATT TEMP CRIT"; }
  if (battVoltage <= BATT_VOLT_CRITICAL && battVoltage > 0) { criticalAlert = true; alertMsg = "BATT VOLT LOW!"; }

  if (!criticalAlert) {
    if (pvTemp      >= PV_TEMP_WARN)                        { alertActive = true; alertMsg = "PV Temp High"; }
    if (battTemp    >= BATT_TEMP_WARN)                      { alertActive = true; alertMsg = "Batt Temp High"; }
    if (battVoltage <= BATT_VOLT_LOW  && battVoltage > 0)   { alertActive = true; alertMsg = "Battery Low"; }
    if (acVoltage   <  AC_VOLT_MIN    && acVoltage   > 0)   { alertActive = true; alertMsg = "AC Volt Low"; }
    if (acVoltage   >  AC_VOLT_MAX)                         { alertActive = true; alertMsg = "AC Volt High"; }
  }

  if (criticalAlert) {
    digitalWrite(GREEN_LED_PIN,  LOW);
    digitalWrite(YELLOW_LED_PIN, HIGH);
    for (int i = 0; i < 3; i++) {
      digitalWrite(BUZZER_PIN, HIGH); delay(100);
      digitalWrite(BUZZER_PIN, LOW);  delay(100);
    }
    Serial.printf("[CRITICAL] %s\n", alertMsg.c_str());
  } else if (alertActive) {
    digitalWrite(GREEN_LED_PIN,  LOW);
    digitalWrite(YELLOW_LED_PIN, HIGH);
    digitalWrite(BUZZER_PIN, HIGH); delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    Serial.printf("[WARNING] %s\n", alertMsg.c_str());
  } else {
    digitalWrite(GREEN_LED_PIN,  HIGH);
    digitalWrite(YELLOW_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN,     LOW);
  }
}

// ================================================================
//  HELPER: LCD Scroll Pages
// ================================================================
void updateLCD() {
  lcd.clear();
  switch (lcdPage) {
    case 0:
      lcd.setCursor(0, 0); lcd.print("PV  V:" + String(pvVoltage, 1) + "V");
      lcd.setCursor(0, 1); lcd.print("PV  I:" + String(pvCurrent, 2) + "A");
      break;
    case 1:
      lcd.setCursor(0, 0); lcd.print("PV  P:" + String(pvPower, 1) + "W");
      lcd.setCursor(0, 1); lcd.print("IRR:" + String(irradiance, 0) + "W/m2");
      break;
    case 2:
      lcd.setCursor(0, 0); lcd.print("AC  V:" + String(acVoltage, 1) + "V");
      lcd.setCursor(0, 1); lcd.print("AC  I:" + String(acCurrent, 2) + "A");
      break;
    case 3:
      lcd.setCursor(0, 0); lcd.print("AC  P:" + String(acPower, 1) + "W");
      lcd.setCursor(0, 1); lcd.print("FREQ:" + String(frequency, 1) + "Hz");
      break;
    case 4:
      lcd.setCursor(0, 0); lcd.print("BAT V:" + String(battVoltage, 1) + "V");
      lcd.setCursor(0, 1); lcd.print("BAT I:" + String(battCurrent, 2) + "A");
      break;
    case 5:
      lcd.setCursor(0, 0); lcd.print("BAT:" + battState);
      lcd.setCursor(0, 1); lcd.print("BAT T:" + String(battTemp, 1) + (char)223 + "C");
      break;
    case 6:
      lcd.setCursor(0, 0); lcd.print("PV  T:" + String(pvTemp, 1) + (char)223 + "C");
      lcd.setCursor(0, 1); lcd.print("AMB T:" + String(ambTemp, 1) + (char)223 + "C");
      break;
    case 7:
  {
    lcd.setCursor(0, 0); lcd.print("HUM:" + String(humidity, 1) + "%");
    String netStatus = WiFi.status() == WL_CONNECTED ? "WiFi" : "GPRS/MTN";
    lcd.setCursor(0, 1); lcd.print("Net:" + netStatus);
    break;
  }
    case 8:
      lcd.setCursor(0, 0);
      if      (criticalAlert) lcd.print("!! CRITICAL !!");
      else if (alertActive)   lcd.print("! WARNING    !");
      else                    lcd.print("System OK      ");
      lcd.setCursor(0, 1);    lcd.print("PERWER v8.0    ");
      break;
  }
  lcdPage = (lcdPage + 1) % 9;
}

// ================================================================
//  READ ALL SENSORS
// ================================================================
void readAllSensors() {
  ds18b20.requestTemperatures();
  pvTemp   = readDS18B20(0);
  battTemp = readDS18B20(1);

  float dhtHum  = dht.readHumidity();
  float dhtTemp = dht.readTemperature();
  humidity = isnan(dhtHum)  ? -1.0 : dhtHum;
  ambTemp  = isnan(dhtTemp) ? -1.0 : dhtTemp;

  pvVoltage   = readDCVoltage(VOLT_PV_PIN,   VOLT_PV_CORRECTION);
  battVoltage = readDCVoltage(VOLT_BATT_PIN, VOLT_BATT_CORRECTION);

  float rawAC = voltageSensor.getRmsVoltage();
  acVoltage   = (rawAC < AC_VOLT_NOISE_FLOOR) ? 0.0 : rawAC;

  float rawACCurr = acCurrentSensor.mA_AC() / 1000.0;
  acCurrent = (rawACCurr < AC_CURR_NOISE_FLOOR) ? 0.0 : rawACCurr;

  float rawPVCurr = pvCurrentSensor.mA_DC() / 1000.0;
  pvCurrent = (rawPVCurr < DC_CURR_NOISE_FLOOR) ? 0.0 : rawPVCurr;
  if (pvCurrent < 0) pvCurrent = 0;

  battCurrent = batCurrentSensor.mA_DC() / 1000.0;
  if (abs(battCurrent) < DC_CURR_NOISE_FLOOR) battCurrent = 0;

  if      (battCurrent >  DC_CURR_NOISE_FLOOR) battState = "charging";
  else if (battCurrent < -DC_CURR_NOISE_FLOOR) battState = "discharging";
  else                                          battState = "idle";

  pvPower   = pvVoltage   * pvCurrent;
  battPower = battVoltage * battCurrent;
  acPower   = acVoltage   * acCurrent;

  irradiance = (analogReadMilliVolts(LDR_PIN) / 3300.0) * 1500.0;
  frequency  = (acVoltage > 0) ? measureFrequency() : 0.0;
  energyKwh += (acPower * (SEND_INTERVAL_MS / 1000.0 / 3600.0)) / 1000.0;
}

// ================================================================
//  PRINT TO SERIAL
// ================================================================
void printReadings() {
  Serial.println("\n-------- Sensor Readings --------");
  Serial.printf("PV Voltage:    %.2f V\n",    pvVoltage);
  Serial.printf("PV Current:    %.2f A\n",    pvCurrent);
  Serial.printf("PV Power:      %.2f W\n",    pvPower);
  Serial.printf("PV Temp:       %.2f C\n",    pvTemp);
  Serial.printf("Irradiance:    %.0f W/m2\n", irradiance);
  Serial.printf("AC Voltage:    %.2f V\n",    acVoltage);
  Serial.printf("AC Current:    %.2f A\n",    acCurrent);
  Serial.printf("AC Power:      %.2f W\n",    acPower);
  Serial.printf("Frequency:     %.1f Hz\n",   frequency);
  Serial.printf("Energy:        %.3f kWh\n",  energyKwh);
  Serial.printf("Batt Voltage:  %.2f V\n",    battVoltage);
  Serial.printf("Batt Current:  %.2f A\n",    battCurrent);
  Serial.printf("Batt Power:    %.2f W\n",    battPower);
  Serial.printf("Batt Temp:     %.2f C\n",    battTemp);
  Serial.printf("Batt State:    %s\n",        battState.c_str());
  Serial.printf("Ambient Temp:  %.2f C\n",    ambTemp);
  Serial.printf("Humidity:      %.2f %%\n",   humidity);
  Serial.printf("Network:       %s\n",        WiFi.status() == WL_CONNECTED ? "WiFi" : "GPRS/MTN");
  Serial.printf("Alerts:        %s\n",
    criticalAlert ? "CRITICAL" : alertActive ? "WARNING" : "None");
  Serial.println("---------------------------------");
}

// ================================================================
//  SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // A9 GPRS Serial
  A9Serial.begin(A9_BAUD, SERIAL_8N1, A9_RX_PIN, A9_TX_PIN);
  delay(1000);

  pinMode(GREEN_LED_PIN,  OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN,     OUTPUT);
  pinMode(DS18B20_PIN,    INPUT_PULLUP);

  digitalWrite(GREEN_LED_PIN,  HIGH);
  digitalWrite(YELLOW_LED_PIN, HIGH);
  delay(500);
  digitalWrite(GREEN_LED_PIN,  LOW);
  digitalWrite(YELLOW_LED_PIN, LOW);

  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();
  lcdPrint("PERWER Solar", "Version 8.0");
  delay(1500);

  voltageSensor.setSensitivity(ZMPT101B_SENSITIVITY);

  Serial.println("[CAL] Calibrating current sensors...");
  lcdPrint("Calibrating...", "No load please!");
  delay(2000);
  acCurrentSensor.autoMidPoint();
  pvCurrentSensor.autoMidPoint();
  batCurrentSensor.autoMidPoint();
  Serial.println("[CAL] Done!");

  ds18b20.begin();
  dht.begin();

  Serial.println("======================================");
  Serial.println("  PERWER Solar Monitor v8.0          ");
  Serial.println("  WiFi + GPRS Dual Connectivity      ");
  Serial.println("======================================");

  // Try WiFi
  lcdPrint("Connecting WiFi", String(WIFI_SSID));
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[OK] WiFi Connected!");
    lcdPrint("WiFi Connected!", WiFi.localIP().toString());
    digitalWrite(GREEN_LED_PIN, HIGH);
  } else {
    Serial.println("\n[INFO] WiFi not available — GPRS will be used");
    lcdPrint("WiFi Offline", "GPRS Standby");
  }
  delay(2000);
  lcd.clear();
}

// ================================================================
//  MAIN LOOP
// ================================================================
void loop() {
  unsigned long now = millis();

  readAllSensors();
  checkAlerts();
  printReadings();

  if (now - lastScrollTime >= LCD_SCROLL_MS) {
    updateLCD();
    lastScrollTime = now;
  }

  if (now - lastSendTime >= SEND_INTERVAL_MS) {
    sendData();   // WiFi first, GPRS fallback
    lastSendTime = now;
  }

  // Reconnect WiFi if dropped
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
  }

  delay(SAMPLE_INTERVAL_MS);
}