#include <WiFi.h>
#include <Update.h>
#include <ETH.h>           //for ethernet
#include <HTTPClient.h>                 // http 통신을 위한 라이브러리
#include <ArduinoJson.h>                // Json을 사용하기 위한 라이브러리

//-----------------------------------------------------------------------------------------------
// Firmware Version Check
//-----------------------------------------------------------------------------------------------
String  szFirmwareVersion = "V2024.04.24_01";
String  PRODUCT_CODE = "SWMRD_Z01";
  
//-----------------------------------------------------------------------------------------------
extern StaticJsonDocument<32768> get_jsondata; // 서버에서 받을 Json데이터의 크기 및 공간 확보


//---------------------------------------------------------------------------
// Construct the full URL for the firmware binary
//---------------------------------------------------------------------------
void performOTAUpdate(String szUpdateURL) {
  HTTPClient http;
  http.begin(szUpdateURL);

//  String otaURL = "http://192.168.219.108:8000/images/SimpleTime.ino.bin";
  Serial.println("Starting OTA update...");
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


//---------------------------------------------------------------------------
// 토이스미스 서버에 해당장비에 대한 펌웨어 업데이트 대상여부를 확인한다.
//---------------------------------------------------------------------------
void Toysmyth_Check_FirmwareUpdate() {
  String szLastVerion;
  String szUpdateURL;

  Serial.println("Toysmyth_Check_FirmwareUpdate");
  get_jsondata.clear();
  HTTPClient http;
  http.begin("http://collect2.toysmythiot.com:5020/getLastVersion=" + PRODUCT_CODE);

  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    String responseBody = http.getString();
    Serial.printf("  Response Code = %d\n",httpResponseCode);
    Serial.printf("  Response Body = %s\n",responseBody);
    
    // get_jsondata에 responseBody deserialize해서 적용
    deserializeJson(get_jsondata, responseBody);
    szLastVerion = get_jsondata["LastVerion"].as<String>();
    szUpdateURL = get_jsondata["UpdateURL"].as<String>();
    Serial.printf("  Firmware Current Version  = %s\n",szFirmwareVersion);
    Serial.printf("  Firmware Check Version  = %s\n",szLastVerion);
    if (szFirmwareVersion != szLastVerion) {    // 현재 펌웨어버전과 다르다면
      Serial.printf("  Firmware Update URL = %s\n",szUpdateURL);
      for(int i=0;i<3;i++) performOTAUpdate(szUpdateURL);     // 총 5번의 업데이트를 시도한다.
      Serial.printf("  Firmware Update Failure\n");
    }
  }
  else {
    int SecondhttpResponseCode = http.GET();
    Serial.println(SecondhttpResponseCode);
  }
  http.end();
}


/*
const char* ssid = "AlwaysSeoul";
const char* password = "Ng97974226!!";
const char* otaServer = "192.168.219.108:8000";
const char* otaFilePath = "/images/SimpleTime.ino.bin";
const int BUTTON_PIN = 2;

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

