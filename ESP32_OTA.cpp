#include <WiFi.h>
#include <Update.h>
#include <ETH.h>           //for ethernet
#include <HTTPClient.h>                 // http 통신을 위한 라이브러리
#include <ArduinoJson.h>                // Json을 사용하기 위한 라이브러리

//-----------------------------------------------------------------------------------------------
// Firmware Version Check
//-----------------------------------------------------------------------------------------------
String  PRODUCT_CODE = "TOYS_MRD_001";
String  szFirmwareVersion = "20240523_01";
 
//-----------------------------------------------------------------------------------------------
extern  StaticJsonDocument<32768> get_jsondata;     // 서버에서 받을 Json데이터의 크기 및 공간 확보
extern  StaticJsonDocument<32768> post_jsondata;    // 서버에서 받을 Json데이터의 크기 및 공간 확보
extern  String mac_addr;

//---------------------------------------------------------------------------
// 서버로 OTA 에 대한 결과를 전송하는 함수
//---------------------------------------------------------------------------
void Toysmyth_FirmwareUpdate_to_Server(String LastVersion,int iRetCode,String szComment)
{
  HTTPClient http;
  String requestBody;
  
  try {
    post_jsondata.clear();
    post_jsondata["product_id"] = PRODUCT_CODE;
    post_jsondata["mac"] = mac_addr;
    post_jsondata["current_ver"] = szFirmwareVersion;
    post_jsondata["update_ver"] = LastVersion;
    post_jsondata["result"] = iRetCode;
    post_jsondata["comment"] = szComment;
    serializeJson(post_jsondata, requestBody);
    Serial.println("post_api(): Firmware OTA ");
    Serial.println("\t" + requestBody);
    post_jsondata.clear();
    http.begin("http://toysmyth-api.toysmythiot.com:8000/v1/MIT/insert_log");
    http.addHeader("Content-Type", "Application/json");
    int httpResponseCode = http.POST(requestBody);
    http.end();
  } catch(String error) {
  }
  delay(1000);
}


//---------------------------------------------------------------------------
// Construct the full URL for the firmware binary
//---------------------------------------------------------------------------
void performOTAUpdate(String szUpdateURL,String szLastVerion) {
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
        Toysmyth_FirmwareUpdate_to_Server(szLastVerion,0x00,"Success");
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
  http.begin("http://toysmyth-api.toysmythiot.com:8000/v1/MIT/get_vercheck?product_id=" + PRODUCT_CODE);

  int httpResponseCode = http.GET();
  Serial.printf("  Response Code = %d\n",httpResponseCode);
  if (httpResponseCode > 0) {
    String responseBody = http.getString();
    Serial.printf("  Response Body = %s\n",responseBody.c_str());
//    String  retBody = responseBody.substring(1,responseBody.length()-2);
    // get_jsondata에 responseBody deserialize해서 적용
    deserializeJson(get_jsondata, responseBody);
    szLastVerion = get_jsondata["VERSION"].as<String>();
    szUpdateURL = get_jsondata["BIN_PATH"].as<String>();
    Serial.printf("  Firmware Current Version   = %s\n",szFirmwareVersion.c_str());
    Serial.printf("  Firmware Updatable Version = %s\n",szLastVerion.c_str());
    if (szLastVerion != NULL && szLastVerion.length() >  7) {
      if (szFirmwareVersion < szLastVerion) {    // 현재 펌웨어버전과 다르다면
        Serial.printf("  Firmware Update URL = %s\n",szUpdateURL.c_str());
        for(int i=0;i<3;i++) performOTAUpdate(szUpdateURL,szLastVerion);     // 총 5번의 업데이트를 시도한다.
        Serial.printf("  Firmware Update Failure\n");
        Toysmyth_FirmwareUpdate_to_Server(szLastVerion,0x01,"Firmware Update Failure");
      } else {
        Serial.printf("  Current version is new.\n");
      }
    }
  }
  else {
    int SecondhttpResponseCode = http.GET();
    Serial.printf("ErrorCode #2 = %d\n",SecondhttpResponseCode);
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

