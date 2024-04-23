#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>

const char* ssid = "AlwaysSeoul";
const char* password = "Ng97974226!!";
const char* otaServer = "192.168.219.108:8000";
const char* otaFilePath = "/images/SimpleTime.ino.bin";
const int BUTTON_PIN = 2;

/*
void setup() {
  Serial.begin(115200);

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }

  Serial.println("Connected to WiFi");

  pinMode(BUTTON_PIN, INPUT);
}

void loop() {
  Serial.println("Waiting 5 seconds before starting OTA update...");
  delay(5000); // Wait for 5 seconds

  performOTAUpdate();

  // Your main loop code here
}
*/

void performOTAUpdate() {
  Serial.println("Starting OTA update...");

  // Construct the full URL for the firmware binary
  String otaURL = "http://192.168.219.108:8000/images/SimpleTime.ino.bin";

  // Begin HTTP client
  HTTPClient http;
  http.begin(otaURL);

  // Start the update process
  if (Update.begin(UPDATE_SIZE_UNKNOWN)) {
    Serial.println("Downloading...");

    // Start the download
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      WiFiClient& stream = http.getStream();
      uint8_t buffer[1024];
      int bytesRead;

      // Write the stream to the Update library in chunks
      while ((bytesRead = stream.readBytes(buffer, sizeof(buffer))) > 0) {
        if (Update.write(buffer, bytesRead) != bytesRead) {
          Serial.println("Error during OTA update. Please try again.");
          Update.end(false); // false parameter indicates a failed update
          return;
        }
      }

      // End the update process
      if (Update.end(true)) {
        Serial.println("OTA update complete. Rebooting...");
        ESP.restart();
      } else {
        Serial.println("Error during OTA update. Please try again.");
        Update.end(false); // false parameter indicates a failed update
      }
    } else {
      Serial.println("Failed to download firmware.");
      Update.end(false); // false parameter indicates a failed update
    }
  } else {
    Serial.println("Failed to start OTA update.");
  }

  // End HTTP client
  http.end();
}