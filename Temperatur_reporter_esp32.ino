//temperatur_reporter_esp32.ino

//A simple program that reads a temperature sensor and sends it to temperatur.nu
//Hardware: ESP32-C3 Super Mini, DS18B20+ temperature sensor
//
//Developed by A. Isaksson 2025
//Used with https://www.temperatur.nu/

/* Libraries needed:
 * OneWire
 * DallasTemperature
 * WiFi (built-in)
 * WebServer (built-in)
 * Preferences (built-in)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>

//=========================
// CONFIGURATION
//=========================
#define ONE_WIRE_BUS 4  // GPIO4 on ESP32-C3
#define TEMPERATURE_PRECISION 12

// AP Mode settings
const char* AP_SSID = "TEMP-Setup";
const char* AP_PASSWORD = "12345678";

const char* SERVER_URL = "http://report.temperatur.nu/rapportera_v2.php";

const unsigned int DATA_SEND_INTERVAL = 3 * 60;      // 3 minutes
const unsigned int DATA_SEND_SHORT_INTERVAL = 1 * 60; // 1 minute on failure
const unsigned int WIFI_TIMEOUT = 30;                 // 30 seconds

//=========================
// GLOBAL OBJECTS
//=========================
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
WebServer server(80);
Preferences preferences;

//=========================
// STATE VARIABLES
//=========================
bool lastUploadSucceeded = true;
unsigned long lastUploadTime = 0;
unsigned long successfulUploads = 0;
bool configMode = false;

String savedSSID = "";
String savedPassword = "";

//=========================
// FUNCTION DECLARATIONS
//=========================
void printESPInfo();
void loadWiFiConfig();
void saveWiFiConfig(String ssid, String password);
void clearWiFiConfig();
bool isFirstBoot();
void markAsBooted();
void startAPMode();
void handleRoot();
void handleSave();
void handleStatus();
void handleResetWiFi();
void handleConfirmReset();
bool connectToWiFi();
bool sendDataToWeb(float temperature, int sensorCount);
float getTemperature();
bool timerDone(unsigned long *lastUpdatedTime, unsigned long intervalTimeS);
void checkWiFiStatus();
int getRSSIasPercentage();

//=========================
// SETUP
//=========================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n╔════════════════════════════════════╗");
  Serial.println("║   Temperatur.nu Reporter Start   ║");
  Serial.println("║          ESP32-C3 Version         ║");
  Serial.println("╚════════════════════════════════════╝\n");

  // Check if this is first boot after programming by comparing compile time
  if(isFirstBoot()) {
    clearWiFiConfig();
    markAsBooted();
  }

  printESPInfo();
  loadWiFiConfig();

  if(savedSSID.length() == 0) {
    Serial.println("⚠ No WiFi configuration found - Starting AP mode");
    startAPMode();
    configMode = true;
  } else {
    Serial.println("✓ WiFi configuration found");
    if(!connectToWiFi()) {
      Serial.println("\n✗ Failed to connect - Starting AP mode for reconfiguration");
      startAPMode();
      configMode = true;
    } else {
      sensors.begin();
      sensors.setResolution(TEMPERATURE_PRECISION);
      Serial.println("\n✓ Setup Complete - Starting web server and main loop...\n");
      
      // Start web server to show status page
      server.on("/", handleStatus);
      server.on("/resetwifi", handleResetWiFi);
      server.on("/confirmreset", HTTP_POST, handleConfirmReset);
      server.begin();
      
      sendDataToWeb(getTemperature(), sensors.getDeviceCount());
    }
  }
}

//=========================
// MAIN LOOP
//=========================
void loop() {
  if(configMode) {
    server.handleClient();
    return;
  }

  // Handle web requests in normal mode too
  server.handleClient();

  // Check WiFi connection
  if(WiFi.status() != WL_CONNECTED) {
    Serial.println("\n⚠ WiFi connection lost - Reconnecting...");
    if(!connectToWiFi()) {
      Serial.println("✗ Reconnection failed - Starting AP mode");
      startAPMode();
      configMode = true;
      return;
    }
  }
  
  if(timerDone(&lastUploadTime, lastUploadSucceeded ? DATA_SEND_INTERVAL : DATA_SEND_SHORT_INTERVAL)) {
    sendDataToWeb(getTemperature(), sensors.getDeviceCount());
  }
  
  delay(100);
}

//=========================
// PREFERENCES FUNCTIONS
//=========================
void loadWiFiConfig() {
  Serial.println("─── Loading WiFi Configuration ───");
  preferences.begin("wifi-config", true); // read-only
  
  savedSSID = preferences.getString("ssid", "");
  savedPassword = preferences.getString("password", "");
  
  preferences.end();
  
  if(savedSSID.length() > 0) {
    Serial.println("  ✓ Configuration loaded");
    Serial.print("  SSID: ");
    Serial.println(savedSSID);
  } else {
    Serial.println("  ⚠ No configuration found");
  }
  Serial.println();
}

void saveWiFiConfig(String ssid, String password) {
  preferences.begin("wifi-config", false); // read-write
  
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  
  preferences.end();
  
  savedSSID = ssid;
  savedPassword = password;
  
  Serial.println("✓ WiFi configuration saved to flash");
}

void clearWiFiConfig() {
  preferences.begin("wifi-config", false);
  preferences.clear();
  preferences.end();
  
  savedSSID = "";
  savedPassword = "";
}

bool isFirstBoot() {
  String compileTime = String(__DATE__) + " " + String(__TIME__);
  
  preferences.begin("boot-marker", true);
  String savedCompileTime = preferences.getString("compile_time", "");
  preferences.end();
  
  return (savedCompileTime != compileTime);
}

void markAsBooted() {
  String compileTime = String(__DATE__) + " " + String(__TIME__);
  
  preferences.begin("boot-marker", false);
  preferences.putString("compile_time", compileTime);
  preferences.end();
}

//=========================
// AP MODE & WEB SERVER
//=========================
void startAPMode() {
  Serial.println("\n╔════════════════════════════════════╗");
  Serial.println("║       Starting AP Mode           ║");
  Serial.println("╚════════════════════════════════════╝");
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  
  Serial.print("  AP SSID:     ");
  Serial.println(AP_SSID);
  Serial.print("  AP Password: ");
  Serial.println(AP_PASSWORD);
  Serial.print("  AP IP:       ");
  Serial.println(WiFi.softAPIP());
  Serial.println("\n  Connect to the AP and go to:");
  Serial.println("  http://192.168.4.1");
  Serial.println("════════════════════════════════════\n");

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/resetwifi", handleResetWiFi);
  server.on("/confirmreset", HTTP_POST, handleConfirmReset);
  server.begin();
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Temperatur.nu Reporter Setup</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; max-width: 500px; margin: 50px auto; padding: 20px; background: #f0f0f0; }";
  html += "h1 { color: #333; text-align: center; }";
  html += ".box { background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
  html += "label { display: block; margin-top: 15px; font-weight: bold; color: #555; }";
  html += "input { width: 100%; padding: 10px; margin-top: 5px; border: 1px solid #ddd; border-radius: 5px; box-sizing: border-box; }";
  html += "button { width: 100%; padding: 12px; margin-top: 20px; background: #4CAF50; color: white; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; }";
  html += "button:hover { background: #45a049; }";
  html += ".info { background: #e3f2fd; padding: 15px; border-radius: 5px; margin-bottom: 20px; }";
  html += ".info p { margin: 5px 0; color: #1976d2; }";
  html += ".footer { text-align: center; color: #999; font-size: 12px; margin-top: 20px; }";
  html += ".footer a { color: #666; text-decoration: none; }";
  html += ".footer a:hover { color: #333; text-decoration: underline; }";
  html += "</style></head><body>";
  html += "<div class='box'>";
  html += "<h1>🌡️ Temperatur.nu Reporter</h1>";
  html += "<div class='info'>";
  html += "<p><strong>PIN:</strong> " + String((uint32_t)ESP.getEfuseMac()) + "</p>";
  html += "<p><strong>MAC:</strong> " + WiFi.macAddress() + "</p>";
  html += "</div>";
  html += "<form action='/save' method='POST'>";
  html += "<label for='ssid'>WiFi SSID:</label>";
  html += "<input type='text' id='ssid' name='ssid' required placeholder='Ditt WiFi-namn'>";
  html += "<label for='password'>WiFi Password:</label>";
  html += "<input type='password' id='password' name='password' required placeholder='Ditt WiFi-lösenord'>";
  html += "<button type='submit'>Spara och Anslut</button>";
  html += "</form>";
  html += "<div class='footer'>";
  html += "<p>Utvecklad av A. Isaksson 2025</p>";
  html += "<p>Används med <a href='https://www.temperatur.nu/' target='_blank'>Temperatur.nu</a></p>";
  html += "</div>";
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

void handleSave() {
  if(server.hasArg("ssid") && server.hasArg("password")) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    
    saveWiFiConfig(ssid, password);
    
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<meta http-equiv='refresh' content='5;url=/'>";
    html += "<title>Saved</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; max-width: 500px; margin: 50px auto; padding: 20px; background: #f0f0f0; text-align: center; }";
    html += ".box { background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
    html += "h1 { color: #4CAF50; }";
    html += "p { color: #555; font-size: 18px; }";
    html += "</style></head><body>";
    html += "<div class='box'>";
    html += "<h1>✓ Konfiguration Sparad!</h1>";
    html += "<p>Enheten startar om och ansluter till WiFi...</p>";
    html += "<p style='color: #999; font-size: 14px;'>Sidan laddas om automatiskt om 5 sekunder...</p>";
    html += "</div></body></html>";
    
    server.send(200, "text/html", html);
    
    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

void handleStatus() {
  float temperature = getTemperature();
  int sensorCount = sensors.getDeviceCount();
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<meta http-equiv='refresh' content='30'>";
  html += "<title>Temperatur.nu Reporter Status</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; max-width: 600px; margin: 50px auto; padding: 20px; background: #f0f0f0; }";
  html += "h1 { color: #333; text-align: center; margin-bottom: 30px; }";
  html += ".box { background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); margin-bottom: 20px; }";
  html += ".register-box { background: #fff3cd; border: 2px solid #ffc107; padding: 20px; border-radius: 10px; margin-bottom: 20px; }";
  html += ".register-box h2 { color: #856404; margin-top: 0; font-size: 20px; }";
  html += ".register-box p { color: #856404; margin: 10px 0; }";
  html += ".pin-display { background: white; padding: 15px; border-radius: 5px; font-size: 24px; font-weight: bold; color: #333; text-align: center; margin: 15px 0; border: 2px dashed #ffc107; }";
  html += ".register-link { display: inline-block; background: #4CAF50; color: white; padding: 12px 24px; text-decoration: none; border-radius: 5px; margin-top: 10px; font-weight: bold; }";
  html += ".register-link:hover { background: #45a049; }";
  html += ".info-row { display: flex; justify-content: space-between; padding: 12px 0; border-bottom: 1px solid #eee; }";
  html += ".info-row:last-child { border-bottom: none; }";
  html += ".info-label { font-weight: bold; color: #666; }";
  html += ".info-value { color: #333; }";
  html += ".temp-big { font-size: 48px; font-weight: bold; color: #2196F3; text-align: center; margin: 20px 0; }";
  html += ".temp-note { text-align: center; color: #666; font-size: 14px; font-style: italic; }";
  html += ".status { display: inline-block; padding: 5px 15px; border-radius: 20px; font-size: 12px; font-weight: bold; }";
  html += ".status-ok { background: #4CAF50; color: white; }";
  html += ".status-error { background: #f44336; color: white; }";
  html += ".footer { text-align: center; color: #999; font-size: 12px; margin-top: 20px; }";
  html += ".footer a { color: #666; text-decoration: none; }";
  html += ".footer a:hover { color: #333; text-decoration: underline; }";
  html += "</style></head><body>";
  
  html += "<h1>🌡️ Temperatur.nu Reporter</h1>";
  
  // Registration box
  html += "<div class='register-box'>";
  html += "<h2>📋 Registrera din station</h2>";
  html += "<p>För att börja rapportera temperatur behöver du registrera din PIN-kod:</p>";
  html += "<div class='pin-display'>" + String((uint32_t)ESP.getEfuseMac()) + "</div>";
  html += "<p style='text-align: center;'>";
  html += "<a href='https://www.temperatur.nu/nystation/' target='_blank' class='register-link'>Registrera på Temperatur.nu →</a>";
  html += "</p>";
  html += "</div>";
  
  // Current temperature
  html += "<div class='box'>";
  html += "<h2 style='text-align: center; color: #666; margin-top: 0;'>Aktuell Temperatur</h2>";
  html += "<div class='temp-big'>" + String(temperature, 1) + " °C</div>";
  
  // Show note if multiple sensors
  if(sensorCount > 1) {
    html += "<p class='temp-note'>Lägsta temperaturen av " + String(sensorCount) + " sensorer</p>";
  }
  
  html += "</div>";
  
  // Device info
  html += "<div class='box'>";
  html += "<h2 style='color: #666; margin-top: 0;'>Enhetsinformation</h2>";
  
  html += "<div class='info-row'>";
  html += "<span class='info-label'>PIN (Chip ID):</span>";
  html += "<span class='info-value'>" + String((uint32_t)ESP.getEfuseMac()) + "</span>";
  html += "</div>";
  
  html += "<div class='info-row'>";
  html += "<span class='info-label'>MAC Address:</span>";
  html += "<span class='info-value'>" + WiFi.macAddress() + "</span>";
  html += "</div>";
  
  html += "<div class='info-row'>";
  html += "<span class='info-label'>IP Address:</span>";
  html += "<span class='info-value'>" + WiFi.localIP().toString() + "</span>";
  html += "</div>";
  
  html += "<div class='info-row'>";
  html += "<span class='info-label'>WiFi SSID:</span>";
  html += "<span class='info-value'>" + WiFi.SSID() + "</span>";
  html += "</div>";
  
  html += "<div class='info-row'>";
  html += "<span class='info-label'>WiFi Signal:</span>";
  html += "<span class='info-value'>" + String(getRSSIasPercentage()) + "%</span>";
  html += "</div>";
  
  html += "<div class='info-row'>";
  html += "<span class='info-label'>Sensorer:</span>";
  html += "<span class='info-value'>" + String(sensorCount) + " st</span>";
  html += "</div>";
  
  html += "<div class='info-row'>";
  html += "<span class='info-label'>Uppladdningar:</span>";
  html += "<span class='info-value'>" + String(successfulUploads) + " st</span>";
  html += "</div>";
  
  html += "<div class='info-row'>";
  html += "<span class='info-label'>Senaste uppladdning:</span>";
  html += "<span class='info-value'>";
  if(lastUploadSucceeded) {
    html += "<span class='status status-ok'>OK</span>";
  } else {
    html += "<span class='status status-error'>MISSLYCKADES</span>";
  }
  html += "</span>";
  html += "</div>";
  
  html += "<div class='info-row'>";
  html += "<span class='info-label'>Nästa uppladdning:</span>";
  unsigned long timeUntilNext = ((lastUploadTime + (lastUploadSucceeded ? DATA_SEND_INTERVAL : DATA_SEND_SHORT_INTERVAL) * 1000) - millis()) / 1000;
  html += "<span class='info-value'>" + String(timeUntilNext) + " sekunder</span>";
  html += "</div>";
  
  html += "</div>";
  
  html += "<div class='footer'>";
  html += "<p>Sidan uppdateras automatiskt var 30:e sekund</p>";
  html += "<p style='margin-top: 15px;'>Utvecklad av A. Isaksson 2025<br>";
  html += "Används med <a href='https://www.temperatur.nu/' target='_blank'>Temperatur.nu</a></p>";
  html += "<p style='margin-top: 20px;'><a href='/resetwifi' style='color: #d32f2f; text-decoration: underline;'>Återställ WiFi-inställningar</a></p>";
  html += "</div>";
  
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleResetWiFi() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Återställ WiFi - Temperatur.nu Reporter</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; max-width: 500px; margin: 50px auto; padding: 20px; background: #f0f0f0; text-align: center; }";
  html += ".box { background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
  html += "h1 { color: #d32f2f; }";
  html += ".warning { background: #ffebee; border: 2px solid #f44336; padding: 20px; border-radius: 5px; margin: 20px 0; }";
  html += ".warning p { color: #c62828; margin: 10px 0; font-size: 16px; }";
  html += "button { padding: 15px 30px; margin: 10px; font-size: 16px; border: none; border-radius: 5px; cursor: pointer; }";
  html += ".confirm { background: #f44336; color: white; }";
  html += ".confirm:hover { background: #d32f2f; }";
  html += ".cancel { background: #999; color: white; }";
  html += ".cancel:hover { background: #777; }";
  html += "</style></head><body>";
  html += "<div class='box'>";
  html += "<h1>⚠️ Återställ WiFi-inställningar</h1>";
  
  html += "<div class='warning'>";
  html += "<p><strong>VARNING: Detta kommer att:</strong></p>";
  html += "<p>✗ Radera dina WiFi-inställningar</p>";
  html += "<p>✗ Starta om enheten i setup-läge</p>";
  html += "<p>✗ Kräva att du konfigurerar WiFi igen</p>";
  html += "</div>";
  
  html += "<p style='margin: 30px 0; color: #666;'>Anslut till <strong>TEMP-Setup</strong> (lösenord: 12345678) för att konfigurera igen.</p>";
  
  html += "<form method='POST' action='/confirmreset'>";
  html += "<button type='submit' class='confirm'>Ja, Återställ WiFi</button>";
  html += "<button type='button' class='cancel' onclick='window.location.href=\"/\"'>Avbryt</button>";
  html += "</form>";
  
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

void handleConfirmReset() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Återställer...</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; max-width: 500px; margin: 50px auto; padding: 20px; background: #f0f0f0; text-align: center; }";
  html += ".box { background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
  html += "h1 { color: #4CAF50; }";
  html += "p { color: #555; font-size: 18px; margin: 20px 0; }";
  html += "</style></head><body>";
  html += "<div class='box'>";
  html += "<h1>✓ WiFi Återställd</h1>";
  html += "<p>Enheten startar om i setup-läge...</p>";
  html += "<p><strong>Anslut till: TEMP-Setup</strong><br>Lösenord: 12345678</p>";
  html += "<p style='color: #999; font-size: 14px;'>Går till 192.168.4.1 för att konfigurera</p>";
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
  
  delay(2000);
  clearWiFiConfig();
  ESP.restart();
}

//=========================
// ESP & WIFI FUNCTIONS
//=========================
void printESPInfo() {
  Serial.println("─── Device Information ───");
  Serial.print("  Chip ID (PIN): ");
  Serial.println((uint32_t)ESP.getEfuseMac());
  Serial.print("  MAC Address:   ");
  Serial.println(WiFi.macAddress());
  Serial.println();
}

bool connectToWiFi() {
  Serial.println("─── WiFi Connection ───");
  Serial.print("  Connecting to: ");
  Serial.println(savedSSID);
  
  // Set hostname based on chip ID
  uint32_t chipId = (uint32_t)ESP.getEfuseMac();
  String hostname = "temperatur_nu_" + String(chipId);
  WiFi.setHostname(hostname.c_str());
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
  
  unsigned long startTime = millis();
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    
    if (millis() - startTime > WIFI_TIMEOUT * 1000) {
      Serial.println("\n  ✗ Connection timeout!");
      return false;
    }
  }
  
  Serial.println();
  Serial.println("  ✓ WiFi Connected!");
  Serial.print("  Hostname:   ");
  Serial.println(hostname);
  Serial.print("  SSID:       ");
  Serial.println(WiFi.SSID());
  Serial.print("  IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("  Signal:     ");
  Serial.print(getRSSIasPercentage());
  Serial.println("%");
  Serial.println();
  
  return true;
}

void checkWiFiStatus() {
  if(WiFi.status() != WL_CONNECTED) {
    Serial.println("\n✗ WiFi not connected - Attempting reconnection...");
    if(!connectToWiFi()) {
      Serial.println("✗ Reconnection failed");
    }
  }
}

int getRSSIasPercentage() {
  int rssi = WiFi.RSSI();
  int quality = 0;

  if (rssi <= -100) {
    quality = 0;
  } else if (rssi >= -50) {
    quality = 100;
  } else {
    quality = 2 * (rssi + 100);
  }
  return quality;
}

//=========================
// DATA SENDING
//=========================
bool sendDataToWeb(float temperature, int sensorCount) {
  Serial.println("╔════════════════════════════════════╗");
  Serial.println("║      Sending Data to Server      ║");
  Serial.println("╚════════════════════════════════════╝");
  
  checkWiFiStatus();
  
  uint32_t chipId = (uint32_t)ESP.getEfuseMac();
  
  String url = String(SERVER_URL) + "?pin=" + String(chipId)
             + "&t=" + String(temperature, 2)
             + "&n=" + String(sensorCount)
             + "&u=" + String(successfulUploads);
  
  Serial.print("  PIN:         ");
  Serial.println(chipId);
  Serial.print("  Temperature: ");
  Serial.print(temperature, 2);
  Serial.println(" °C");
  Serial.print("  Sensors:     ");
  Serial.println(sensorCount);
  Serial.print("  Uploads:     ");
  Serial.println(successfulUploads);
  Serial.println();
  Serial.print("  URL: ");
  Serial.println(url);
  Serial.println();
  
  HTTPClient http;
  http.begin(url);
  
  Serial.println("  Sending HTTP GET request...");
  int httpCode = http.GET();
  bool success = false;
  
  if (httpCode > 0) {
    Serial.print("  Response Code: ");
    Serial.println(httpCode);
    
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.print("  Server Reply: ");
      Serial.println(payload);
      successfulUploads++;
      success = true;
      Serial.println("\n  ✓ Upload Successful!");
    } else {
      Serial.println("\n  ✗ Server returned non-OK status");
    }
  } else {
    Serial.print("  ✗ HTTP Request Failed, code: ");
    Serial.println(httpCode);
  }

  http.end();
  
  lastUploadSucceeded = success;
  
  if(success) {
    Serial.print("\n  Next upload in ");
    Serial.print(DATA_SEND_INTERVAL);
    Serial.println(" seconds");
  } else {
    Serial.print("\n  Retry in ");
    Serial.print(DATA_SEND_SHORT_INTERVAL);
    Serial.println(" seconds");
  }
  
  Serial.println("════════════════════════════════════\n");
  
  return success;
}

//=========================
// TEMPERATURE FUNCTIONS
//=========================
float getTemperature() {
  sensors.requestTemperatures();
  int sensorCount = sensors.getDeviceCount();
  
  if(sensorCount == 0) {
    Serial.println("✗ ERROR: No temperature sensors found!");
    return -127.0;
  }
  
  // Get temperature from first sensor
  float lowestTemp = sensors.getTempCByIndex(0);
  
  // If we have multiple sensors, find the lowest temperature
  if(sensorCount > 1) {
    Serial.println("─── Reading Multiple Sensors ───");
    for(int i = 0; i < sensorCount; i++) {
      float temp = sensors.getTempCByIndex(i);
      Serial.print("  Sensor ");
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(temp);
      Serial.println(" °C");
      
      // Check if valid reading
      if(temp != DEVICE_DISCONNECTED_C && temp < lowestTemp) {
        lowestTemp = temp;
      }
    }
    Serial.print("  → Lowest: ");
    Serial.print(lowestTemp);
    Serial.println(" °C");
    Serial.println();
  }
  
  // Check if valid reading
  if(lowestTemp == DEVICE_DISCONNECTED_C) {
    Serial.println("✗ ERROR: All temperature sensors disconnected!");
    return -127.0;
  }
  
  return lowestTemp;
}

//=========================
// UTILITY FUNCTIONS
//=========================
bool timerDone(unsigned long *lastUpdatedTime, unsigned long intervalTimeS) {
  unsigned long currentTime = millis();
  
  if(currentTime < *lastUpdatedTime + (intervalTimeS * 1000)) {
    return false;
  }
  
  *lastUpdatedTime = currentTime;
  return true;
}