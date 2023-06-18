#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <uri/UriBraces.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
// #include <BluetoothA2DPSource.h>

// WiFi params
const char* SSID = "ARRIS-70C1";
const char* PASSWORD = "5632548C85AD4E18";

class Station {
  private:
    char name[32];
    char url[70];

  public:
    Station(void) {
      memset(name, 0, sizeof(name));
      memset(url, 0, sizeof(url));
    }

    Station(const String& station_name, const String& station_url) {
      strncpy(name, station_name.c_str(), sizeof(name) - 1);
      strncpy(url, station_url.c_str(), sizeof(url) - 1);
      name[sizeof(name) - 1] = '\0'; 
      url[sizeof(url) - 1] = '\0';  
    }

    // Getter for the name as String
    String getName() const {
      return String(name);
    }

    // Getter for the URL as String
    String getUrl() const {
      return String(url);
    }
};


// station params
const int EEPROM_SIZE = 1024;
const int MAX_STATIONS = 10;
int stations_nr = 0;

Station stations[MAX_STATIONS];

// server params
const char* SERVER_NAME = "esp32";

WebServer server(80);
// BluetoothA2DPSource a2dp_source;

void setup(void) {
  Serial.begin(115200);

  setupWifi();
  // setupBluetooth();
  setupServer(); 
}

void loop(void) {
  server.handleClient();
  delay(2);//allow the cpu to switch to other tasks
}

// setups
void setupWifi(void) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  Serial.print("Connecting to WiFi ");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected!");

  Serial.println("WiFi name: " + String(SSID));
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// void setupBluetooth(void) {
//   a2dp_source.start("WI-C310");  
//   a2dp_source.set_volume(150);
// }

void setupServer(void) {
  if(MDNS.begin(SERVER_NAME)) {
    Serial.println("Server name: " + String(SERVER_NAME) + ".local");
  }

  // run once to clear eeprom
  // removeStationsFromEEPROM();
  readStationsFromEEPROM();

  server.on("/", handleRoot);

  server.on("/stations", HTTP_GET, handleGetStations);
  server.on("/stations", HTTP_POST, handleAddStation);


  server.on(UriBraces("/stations/{}"), HTTP_GET, handleGetStation);
  server.on(UriBraces("/stream/{}"), HTTP_GET, handleStreamStation);

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

void removeStationsFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  for (int addr = 0; addr < EEPROM_SIZE; addr++) {
    EEPROM.write(addr, 0);
  }
  EEPROM.commit();
  EEPROM.end();
}

void readStationsFromEEPROM() {
  int addr = 0;
  EEPROM.begin(EEPROM_SIZE);

  EEPROM.get(addr, stations_nr);
  addr += sizeof(Station);

  for (int stations_ctr = 0; stations_ctr < stations_nr; stations_ctr++) {
    Station station;
    EEPROM.get(addr, station);
    stations[stations_ctr] = station;
    addr += sizeof(Station);
  }

  EEPROM.end();
}

void saveStationToEEPROM(Station station) {
  EEPROM.begin(EEPROM_SIZE);

  int stations_nr_addr = 0;
  int station_addr = stations_nr_addr + sizeof(stations_nr) + sizeof(Station) * (stations_nr - 1);

  EEPROM.put(stations_nr_addr, stations_nr);
  EEPROM.put(station_addr, station);
  
  EEPROM.commit();
  EEPROM.end();
}

void updateStations(Station station) {
  stations[stations_nr] = station;
  saveStationToEEPROM(station);
  stations_nr++;
}

void handleRoot() {
  Serial.println("root");
  server.send(200, "text/plain", "Welcome into esp32 Internet Radio!");
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void handleAddStation() {  
  if (stations_nr == MAX_STATIONS) {
    server.send(400, "text/plain", "No available slots for new stations");
  }

  // Parse the JSON data from the request body
  StaticJsonDocument<200> jsonBuffer;
  DeserializationError error = deserializeJson(jsonBuffer, server.arg("plain"));
  
  if (error) {
    server.send(400, "text/plain", "Invalid JSON data");
    return;
  }
  
  // Extract the parameters from the JSON object
  String name = jsonBuffer["name"];
  String url = jsonBuffer["url"];
  Station station(name, url);

  updateStations(station);
  
  server.send(200, "text/plain", "Station added successfully");
}

void handleGetStations() {  
  String message = "List of available stations:\n";
  for (int stations_ctr = 0; stations_ctr < stations_nr; stations_ctr++) {
    Station station = stations[stations_ctr];
    message += String(stations_ctr + 1) + ": " + station.getName() + " [" + station.getUrl() + "]\n";
  }
  server.send(200, "text/plain", message);
}

void handleGetStation() {  
  int station_nr = server.pathArg(0).toInt();
  if(station_nr <= 0 || station_nr > stations_nr ) {
    handleNotFound();
  } else {
    Station station = stations[station_nr - 1];
    server.send(200, "text/plain", "Name: " + station.getName() + "\nUrl: " +  station.getUrl());
  }
}

void handleStreamStation() {
  int station_nr = server.pathArg(0).toInt();
  if(station_nr <= 0 || station_nr > stations_nr ) {
    handleNotFound();
    return;
  }
  
  Station station = stations[station_nr - 1];
  
  // Connect to the station's URL
  WiFiClient client;
  if (!client.connect(station.getUrl().c_str(), 80)) {
    server.send(500, "text/plain", "Failed to connect to station");
    return;
  }
  Serial.println("Connecting to station!");
  
  // Send GET request to the station's URL
  client.print(String("GET ") + station.getUrl() + " HTTP/1.1\r\n" +
               "Host: " + station.getUrl() + "\r\n" +
               "Connection: close\r\n\r\n");

  Serial.println("Sending request to station!");
  
  // Wait for the response headers to complete
  while (!client.available()) {
    if (!client.connected()) {
      server.send(500, "text/plain", "Station connection lost");
      return;
    }
    delay(1);
  }

  Serial.println("Waiting for a response");
  
  // Skip the HTTP response headers
  while (client.available()) {
    if (client.read() == '\r' && client.peek() == '\n') {
      client.read();
      break;
    }
  }

  Serial.println("Getting a response");
  
  // Send the audio data to the client in chunks
  while (client.connected()) {
    Serial.println("Fetching data!");
    delay(1000);
  }  
}