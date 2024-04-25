#include <Arduino.h>       //for ethernet
#include <ETH.h>           //for ethernet
#include <HTTPClient.h>                 // http 통신을 위한 라이브러리
#include <ArduinoJson.h>                // Json을 사용하기 위한 라이브러리
#include "UserDefine.h"

bool eth_connected = false;
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
extern String mac_addr;
extern String zone_id;
extern String machine_no;
extern float  volt;
extern int    valve;;
extern int    iZoneID;
extern int    iDeviceNumber;
extern char   szMacAddr[16];

extern StaticJsonDocument<32768> get_jsondata; // 서버에서 받을 Json데이터의 크기 및 공간 확보
extern StaticJsonDocument<1024> post_jsondata; // 서버에 보낼 Json데이터의 크기 및 공간 확
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
extern HardwareSerial rs485; // rxtx mode 2 of 0,1,2

//---------------------------------------------------------------------------
// WIFI 이벤트 인터럽트 및 로직
//---------------------------------------------------------------------------
void WiFiEvent(WiFiEvent_t event)
{
  switch (event) {
    case ARDUINO_EVENT_ETH_START:        // 이더넷이 시작됨.
      Serial.println("ETH Started");
      ETH.setHostname("esp32-ethernet"); // set eth hostname here
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:    // 이더넷이 연결됨.
      Serial.println("ETH Connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:       // IP를 할당받음.
      Serial.print("ETH MAC: ");
      Serial.print(ETH.macAddress());
      Serial.print(", IPv4: ");
      Serial.print(ETH.localIP());
      if (ETH.fullDuplex()) {
        Serial.print(", FULL_DUPLEX");
      }
      Serial.print(", ");
      Serial.print(ETH.linkSpeed());
      Serial.println("Mbps");
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:  // 이더넷이 해제됨.
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:          // 이더넷이 멈춤.
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;
  }
}

//---------------------------------------------------------------------------
// api3을 이용한 zone_id와 machine_no를 받아오는 함수
//---------------------------------------------------------------------------
void get_device_config() {
  String tmp_zone_id;
  String tmp_machine_no;

  // jsondata를 비워준다.
  get_jsondata.clear();

  HTTPClient http;
  http.begin("http://www.ocms.kr/RestAPI/api3?mac_addr=" + mac_addr);
  Serial.println("get dev data");

  int httpResponseCode = http.GET();
  Serial.println(httpResponseCode);

  if (httpResponseCode > 0) {
    String responseBody = http.getString();
    Serial.println("get_api(): httpResponseCode");
    Serial.println("get_api(): http.getString");
    Serial.println(responseBody);
    
    // get_jsondata에 responseBody deserialize해서 적용
    deserializeJson(get_jsondata, responseBody);
    
    // 여기서 zond_id를 받아와 이후 서버 통신에 사용한다.
    tmp_zone_id = get_jsondata["zone_id"].as<String>();
    // 여기서 machine_no를 받아와 이후 서버 통신에 사용한다.
    tmp_machine_no = get_jsondata["machine_no"].as<String>();

    if (tmp_zone_id==zone_id && tmp_machine_no==machine_no) {   //}  && String(szMacAddr)==mac_addr) {
    } else {
      zone_id = tmp_zone_id;
      machine_no = tmp_machine_no;
      iZoneID = zone_id.toInt();
      iDeviceNumber = machine_no.toInt();
//      memset(szMacAddr,0x00,16);
//      mac_addr.toCharArray(szMacAddr,mac_addr.length());
      EEPROM_Set_DeviceInformation();
    }
  }
  else {
    int SecondhttpResponseCode = http.GET();
    Serial.println(SecondhttpResponseCode);
  }
  http.end();
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
void MRD_Protocol_SendMesssage(Protocol_MRD  *pSendData,Protocol_MRD  *pRecvData) {
  Protocol_MRD  *pChkPnt;
  int         iRetryCount,iChkRet;
  HTTPClient  http;
  String      requestBody;
  String      requestBody1;  

  iRetryCount = 0;
  do {
    iChkRet = MRD_Send_Receive((byte*)pSendData,(byte*)pRecvData,10000);
    if (iChkRet==0x00) {      // 정상수신 
      pChkPnt = pRecvData;
// 데이터에 따른 파싱후 정상데이터면 루프빠져나감.      
      if (pChkPnt->Cmd0 == 0xF7) {  // 기존코드 확인필요(0xF7 일경우에 명령을 다시 보내는 이유)

      }
      if ((pChkPnt->Cmd1 != 0xE0) && (pChkPnt->Cmd0 != 0xE1) && (pChkPnt->Cmd0 != 0xB1) ) {
        Serial.print("Modify data[4]: ");
        Serial.println(pChkPnt->Cmd1, HEX);
        pChkPnt->Cmd1 = 0xE0;        
      }
      if (pChkPnt->Cmd0 == 0xF7) {
        pChkPnt->Cmd0 = 0xE1;        
        pChkPnt->Cmd1 = 0x03;        
      }
// 서버로 결과를 전송하는 부분
      MRD_DataValue_to_Server(0x00,(byte*)pRecvData,NULL,NULL);
      return;
    } else if (iChkRet==0x01 || iChkRet==0x02 || iChkRet==0x03 ) {   // ErrorCode
      break;
    } else if (iChkRet==0x10 ) {   // 응답없음.
      break;
    }
  } while(pChkPnt==NULL && iRetryCount < 5);
//  rs485.flush();
}

//---------------------------------------------------------------------------
// getapi()에서 사용하는 RS485 통신 관련 함수
//---------------------------------------------------------------------------
void MRD_Protocol_Parser(Protocol_MRD  *pSendData)
{
  Protocol_MRD  stSendData,stRecvData;
  int     iRetryCount,iChkRet;

  if ((pSendData->Cmd0 == 0xA8) && (pSendData->Cmd1== 0xE0)) {
// 노딕보드에서 모든 시케쥴 데이터를 받아오는 명령어인 경우
    memcpy((byte*)&stSendData,(byte*)pSendData,0x09);  
    stSendData.Cmd0 = 0xA6;
    stSendData.Data[0] = 0x00;
    for (int i = 0; i < 2; i++) {
      stSendData.Cmd0 += i;
      byte num = 0x10;
      for (int j = 0; j < 12; j++) {
        for (int k = 0; k < 9; k++) {
          stSendData.Data[0] = num;
          stSendData.CheckData = cal_checksum((byte*)&stSendData,8);
          MRD_Protocol_SendMesssage(&stSendData,&stRecvData); //, output_msg);
          if (k == 8) {
            num += 8;
          }
          else {
            num += 1;
          }
        }
      }
    }
  } else if ((pSendData->Cmd1 == 0xE0) && ((pSendData->Cmd0 == 0xA6) || (pSendData->Cmd0 == 0xA7))) {\
//노딕보드에서 한 달 간의 스케쥴 데이터를 받아오는 명령어인 경우
    memcpy((byte*)&stSendData,(byte*)pSendData,0x09);  
    for (int k = 0; k < 9; k++) {
      stSendData.Data[0] = pSendData->Data[0] + k;
      stSendData.CheckData = cal_checksum((byte*)&stSendData,8);
      MRD_Protocol_SendMesssage(&stSendData,&stRecvData); //, output_msg);
    }
  } else {  // 위의 케이스에 해당하지 않는 명령일 경우
    MRD_Protocol_SendMesssage(pSendData,&stRecvData);
  }
}

//---------------------------------------------------------------------------
// 서버 통신 관련 함수
// api1을 통해 관제 홈페이지에서 내린 명령들을 노딕보드에 전송하는 함수
//---------------------------------------------------------------------------
void get_api() {
//  command_bundle get_command;
  HTTPClient    http;
  Protocol_MRD  stSendData;

  Serial.println("get_api(): start");
  get_jsondata.clear();

  try {
    http.begin("http://www.ocms.kr/RestAPI/api1?zone_id=" + zone_id + "&machine_no=" + machine_no);
    Serial.println("get_api(): http.begin()");
    int httpResponseCode = http.GET();
    // http통신 상태를 다시 체크하는 함수 new command check ***
    if (httpResponseCode > 0) {
      // json data(parameter) to discriminate command re-check ***
      String responseBody = http.getString();
      Serial.println("get_api(): httpResponseCode");
      Serial.println(httpResponseCode);
      Serial.println("get_api(): http.getString");
      Serial.println(responseBody);

      // get_jsondata에 responseBody deserialize해서 적용
      deserializeJson(get_jsondata, responseBody);
      // HTTP에서 받은 result값으로 에러 확인을 함(에러 시 예외사항을 고려해야함.)
      if (get_jsondata["result"] != "success") {
        // error handling for api result
        Serial.println("get_api(): Response error");
      }
      else {
        Serial.println("get_api(): Response success");
      }
      // zone_id를 못받아은 경우
      if (get_jsondata["zone_id"] != zone_id)
        Serial.println("get_api(): zone_id error");
        
      // machine_no를 못받아은 경우
      if (get_jsondata["machine_no"] != machine_no)
        Serial.println("get_api(): machine_no error");

      // 받은 명령어들을 JsonArray형태로 초기화 후 rs485통신 후 서버로 전송
      JsonArray array = get_jsondata["List"].as<JsonArray>();
      for (JsonVariant v : array) {
        // 여기서 명령어들을 16진수로 변환 및 정리한다.
        if (v["command_0"] != NULL) {
          stSendData.STX = 0x7C;
          stSendData.ZoneID = iZoneID;
          stSendData.DeviceNumber = iDeviceNumber;
          stSendData.Cmd0 = strtol(v["command_0"], NULL, 16);
          stSendData.Cmd1 = strtol(v["command_1"], NULL, 16);
          stSendData.Data[0] = strtol(v["data_0"], NULL, 16);
          stSendData.Data[1] = strtol(v["data_1"], NULL, 16);
          stSendData.Data[2] = strtol(v["data_2"], NULL, 16);
          stSendData.CheckData = cal_checksum((byte*)&stSendData,8);
          if ((stSendData.Cmd0 != 0) && (stSendData.Cmd0 != ' ')) {
            // 여기서 명령어를 rs485통신 후 서버로 전송한다.
            MRD_Protocol_Parser(&stSendData);
  //          post_get_command(get_command);
          }
        }
      }
    }
    // httpResponseCode가 0보다 작은 경우(아마 가능성은 없다. 통신 오류인 경우 400번대를 보내줄 것 이것을 코드에 적용하여 바꿀 필요가 있다.)
    else { 
      Serial.println("get_api command failed");
    }
    get_jsondata.clear();
    http.end();
  } catch(String error) {
  }    
}

//---------------------------------------------------------------------------
float Calculate_Volt(unsigned char Volt1,unsigned char Volt2,unsigned char Volt3)
{
  float  fValue;

  fValue = String(Volt1-48).toFloat()*10 + String(Volt2-48).toFloat() + (String(Volt3-48).toFloat()/10.0); 
  return(fValue);
}

//---------------------------------------------------------------------------
// 서버 전송 관련 함수
// 서버로 battery값을 보내는 함수
//---------------------------------------------------------------------------
void post_battery_to_server(int to_server) {
  char  szVolt[32];

  volt = Calculate_Volt(getbatterydata[5],getbatterydata[6],getbatterydata[7]);  
  sprintf(szVolt,"%5.2f",volt);
  MRD_DataValue_to_Server(to_server,getbatterydata,"volt",szVolt);
}

//---------------------------------------------------------------------------
// 서버로 Valve값을 전송하는 함수
//---------------------------------------------------------------------------
void post_valve_to_server(int to_server) {
  char  szValve[32];

  if (getvalvedata[5] == 0xb0) valve = 0;
   else if (getvalvedata[5] == 0xb1) valve = 1;
   else valve = -1;
  sprintf(szValve,"%d",valve);
  MRD_DataValue_to_Server(to_server,getvalvedata,"valve",szValve);
}

//---------------------------------------------------------------------------
// 서버로 수신데이터를 전송하는 함수
//---------------------------------------------------------------------------
void MRD_DataValue_to_Server(int to_server,byte *pRecvData,char *OptionKey,char *OptionValue)
{
  HTTPClient http;

  // MIT 서버에 보낼 requestBody 구성
  String requestBody;
  // 우리 서버에 보낼 requestBody 구성
  String requestBody1;
  

  try {
    post_jsondata.clear();
    post_jsondata["zone_id"] = zone_id;
    post_jsondata["machine_no"] = machine_no;
    post_jsondata["command_0"] = String(pRecvData[3], 16); // 16진수 --> string
    post_jsondata["command_1"] = String(pRecvData[4], 16);
    post_jsondata["data_0"] = String(pRecvData[5], 16);
    post_jsondata["data_1"] = String(pRecvData[6], 16);
    post_jsondata["data_2"] = String(pRecvData[7], 16);

    //requestBody에 post_jsondata의 값을 JSON 형태로 전달한다.
    serializeJson(post_jsondata, requestBody);
    post_jsondata.clear();
    Serial.println("post_api(): requestBody");
    //requestBody 데이터 확인 로그
    Serial.println(requestBody);
    post_jsondata["mac"] = mac_addr;
    post_jsondata["zone_id"] = zone_id;
    post_jsondata["machine_no"] = machine_no;
    post_jsondata["command_0"] = String(pRecvData[3], 16); // 16진수 --> string
    post_jsondata["command_1"] = String(pRecvData[4], 16);
    post_jsondata["data_0"] = String(pRecvData[5], 16);
    post_jsondata["data_1"] = String(pRecvData[6], 16);
    post_jsondata["data_2"] = String(pRecvData[7], 16);
    if (OptionKey != NULL) {
      post_jsondata[OptionKey] = String(OptionValue);
    }

    serializeJson(post_jsondata, requestBody1);
    post_jsondata.clear();
    Serial.println("post_api(): requestBody1");
    Serial.println("\t" + requestBody1);

    // 0일 때 MIT, 토이스미스 두 서버 모두 전송
    if (to_server == 0) {
      Serial.println("to_server : both");
      http.begin("http://www.ocms.kr/RestAPI/api2");
      http.addHeader("Content-Type", "Application/json");
      int httpResponseCode = http.POST(requestBody);
      http.end();

      http.begin("http://collect2.toysmythiot.com:5020/sewer_reduction");
      http.addHeader("Content-Type", "Application/json");
      httpResponseCode = http.POST(requestBody1);
      http.end();
    }
    // 1일 때 토이스미스에만 전송
    else if (to_server == 1) {
      Serial.println("to_server : one");
      http.begin("http://collect2.toysmythiot.com:5020/sewer_reduction");
      http.addHeader("Content-Type", "Application/json");
      int httpResponseCode = http.POST(requestBody1);
      http.end();
    } else Serial.println("to_server value error");
  } catch(String error) {
  }
}


//---------------------------------------------------------------------------
char *MRD_Protocol2String(byte *pSendData,int iCount=9)
{
  static char szBuffer[256],szTemp[16];

  memset(szBuffer,0x00,sizeof(szBuffer));
  for(int i=0;i<iCount;i++) {
    sprintf( szTemp , "%02X " , pSendData[i] );
    strcat( szBuffer , szTemp );
  }
  return(szBuffer);
}

//---------------------------------------------------------------------------
// 서버로 예외처리 및 이벤트를 전송하는 함수
//---------------------------------------------------------------------------
void MRD_Exception_to_Server(byte *pSendData,byte *pRecvData,int iLogLevel,char *Exception,char *OptionKey,char *OptionValue)
{
  HTTPClient http;
  String requestBody;
  
  try {
    post_jsondata.clear();
    post_jsondata["mac"] = mac_addr;
    post_jsondata["zone_id"] = zone_id;
    post_jsondata["machine_no"] = machine_no;
    if (pSendData != NULL) post_jsondata["send_data"] = String(MRD_Protocol2String(pSendData));  // Ex: "7C 0A 0D 3A 01 0D 0D 0D 41"
    if (pRecvData != NULL) post_jsondata["recv_data"] = String(MRD_Protocol2String(pRecvData));  // Ex: "7C 0A 0D 3A 01 0D 0D 0D 41"
    post_jsondata["loglevel"] = iLogLevel;
    post_jsondata["exception"] = String(Exception);
  //
    if (OptionKey != NULL) {
      post_jsondata[OptionKey] = String(OptionValue);
    }
    serializeJson(post_jsondata, requestBody);
    Serial.println("post_api(): Exception");
    Serial.println("\t" + requestBody);
    post_jsondata.clear();

    http.begin("http://collect2.toysmythiot.com:5020/sewer_exception");
    http.addHeader("Content-Type", "Application/json");
    int httpResponseCode = http.POST(requestBody);
    http.end();
  } catch(String error) {
  }

  delay(1000);
}

//---------------------------------------------------------------------------
// 서버로 전압 및 밸브상태를 전송하는 함수
//---------------------------------------------------------------------------
void MRD_StatusData_to_Server_byDay()
{
  char        szBuffer[128],szDataBuf[512];
  int         iChkRet;
  struct tm   Chktime;

  if (getLocalTime(&Chktime)) {     // 이 함수는 메일저녁 Night_Operation() 일때 수행한다.
    iChkRet = RecordData_Status_byRead(Chktime.tm_year+1900,Chktime.tm_mon+1,Chktime.tm_mday);
    if (iChkRet==0x01) {
      strcpy(szDataBuf,"Volt=");
      for(int i=0;i<24;i++) {
        strcpy(szBuffer,"0,");
        if (stServerStatus.Status_Volt[i]!=0x00) sprintf(szBuffer,"%3.1f," ,(float)stServerStatus.Status_Volt[i]/10.0);
        strcat(szDataBuf,szBuffer);
      }
      MRD_Exception_to_Server(NULL,NULL,0x09,szDataBuf,NULL,NULL);
    }
  }
}


//---------------------------------------------------------------------------
// 서버로 전압 및 밸브상태를 전송하는 함수
//---------------------------------------------------------------------------
void MRD_StatusData_to_Server()
{
  HTTPClient  http;
  String      requestBody;
  char        szBuffer[128],szDataBuf[512];
  int         iChkRet;
  struct tm   Chktime;

  if (getLocalTime(&Chktime)) {     // 이 함수는 매월 1일 수행된다.  따라서 전달 마지막 날자를 찾아야한다.
    Chktime.tm_mday -= 1;
    mktime(&Chktime);
    iChkRet = RecordData_Status_byRead(Chktime.tm_year+1900,Chktime.tm_mon+1,Chktime.tm_mday);
    if (iChkRet==0x01) {
      post_jsondata.clear();
      post_jsondata["mac"] = mac_addr;
      post_jsondata["zone_id"] = zone_id;
      post_jsondata["machine_no"] = machine_no;
      post_jsondata["year"] = Chktime.tm_year+1900;
      post_jsondata["month"] = Chktime.tm_mon+1;
      post_jsondata["day"] = Chktime.tm_mday;
      memset(szDataBuf,0x00,sizeof(szDataBuf));
      for(int i=0;i<24;i++) {
        strcpy(szBuffer,"0,");
        if (stServerStatus.Status_Volt[i]!=0x00) sprintf(szBuffer,"%3.1f," ,(float)stServerStatus.Status_Volt[i]/10.0);
        strcat(szDataBuf,szBuffer);
      }
      post_jsondata["Volt"] = String(szDataBuf);
      memset(szDataBuf,0x00,sizeof(szDataBuf));
      for(int i=0;i<24;i++) {
        sprintf(szBuffer,"%d,",stServerStatus.Status_Valve[i]);
        strcat(szDataBuf,szBuffer);
      }
      post_jsondata["Valve"] = String(szDataBuf);

      serializeJson(post_jsondata, requestBody);
      Serial.println("post_api(): Status Data");
      Serial.println(requestBody);
      post_jsondata.clear();

      http.begin("http://collect2.toysmythiot.com:5020/sewer_status");
      http.addHeader("Content-Type", "Application/json");
      int httpResponseCode = http.POST(requestBody);
      http.end();
      delay(3000);
    }
  }
}
