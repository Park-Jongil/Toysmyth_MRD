/*RTC 모듈 및 인터럽트 관련 라이브러리 및 정의*/
#include <stdio.h>
#include <math.h>
#include <EEPROM.h>
#include <Wire.h>
#include "SPIFFS.h"
#include "RTClib.h"
#include "UserDefine.h"
RTC_DS3231 rtc;
DateTime now;
DateTime AlarmTime;
DateTime ATime;
// RTC SQW핀의 wake up 알람을 위한 ESP32의 4번핀
const int wakeupPin = 4;
// 재시작 버튼을 위한 12번
const int buttonPin = 12;

/*노딕보드와 통신을 위한 RS485관련 라이브러리 및 정의*/
#include <HardwareSerial.h> //for rs485 comm
#define RXD2 5
#define TXD2 17//
HardwareSerial rs485(2); // rxtx mode 2 of 0,1,2

/* DEEPSLEEP 관련 초기화 */
//unsigned long long int uS_TO_S_FACTOR = 1000000ULL; //딥슬립의 기준이 us이기 때문에 s로 계산하기 위해 뒤에 TIME_TO_SLEEP과 곱해준다.
//초(s)단위의 실제로 DeepSleep을 하는 시간
unsigned long long int TIME_TO_SLEEP = 0;
//const unsigned long long int Sday = 86400; // 24 * 60 * 60 == 24 hour
//const unsigned long long int Shour = 3600; // 60 * 60      == 60 min
//const unsigned long long int Smin = 60;    // 60           == 1min
// deepsleep 동안 사라지지 않는 변수
RTC_DATA_ATTR int bootCount = 0;

/*랜선 관련 초기화 및 정의*/
#include <Arduino.h>       //for ethernet
#include <ETH.h>           //for ethernet
#define ETH_POWER_PIN   16 // 밑 6개의 define은 WT32-ETH01 보드에서는 정해져 있음.
#define ETH_MDC_PIN     23
#define ETH_MDIO_PIN    18
#define ETH_ADDR        1
#define ETH_CLK_MODE    ETH_CLOCK_GPIO0_IN
#define ETH_TYPE        ETH_PHY_LAN8720
#define ROUTER_CONTROL 2 // gpio pin 2 H or L 

#define abs(x) ((x)>0?(x):-(x))

/*서버 전송 관련 라이브러리 및 초기화*/
#include <HTTPClient.h>                 // http 통신을 위한 라이브러리
#include <string.h>                     // Json에 저장할 String형을 위한 라이브러리 
#include <ArduinoJson.h>                // Json을 사용하기 위한 라이브러리
StaticJsonDocument<32768> get_jsondata; // 서버에서 받을 Json데이터의 크기 및 공간 확보
StaticJsonDocument<1024> post_jsondata; // 서버에 보낼 Json데이터의 크기 및 공간 확

/*NTP관련 라이브러리, 초기화*/
#include "time.h"
const char* ntpServer = "pool.ntp.org"; // ntp 동기화 서버 주소
const long  gmtOffset_sec = 32400;      // 한국시간 계산을 위한 변수 GMT+9 : 3200s*9
const int   daylightOffset_sec = 0;     // summertime 게산을 위한 변수
struct tm timeinfo;                     // ntp 동기화 후 저장되는 구조체


//-----------------------------------------------------------------------------------------------
// 기기 관련 정보 초기화
//-----------------------------------------------------------------------------------------------
String mac_addr = "D8659599000B"; 
String zone_id;
String machine_no;
int valve;
float volt;
int   iZoneID;
int   iDeviceNumber;
char  szMacAddr[16] = { 0, };

//-----------------------------------------------------------------------------------------------
// ESP32_OTA.cpp 에서 선언된 펌웨어 버전
//-----------------------------------------------------------------------------------------------
extern  String  szFirmwareVersion;


//-----------------------------------------------------------------------------------------------
// SPIFFS 관련된 변수
//-----------------------------------------------------------------------------------------------
int   is_SPIFFS;
File  WriteFile,ReadFile;
RecordData_Status   stServerStatus;

//-----------------------------------------------------------------------------------------------
// 각각 배터리 체크를 위한 변환용 데이터, 요청 후 받은 배터리데이터, 요청 후 받 밸브데이터
//-----------------------------------------------------------------------------------------------
byte battery_value[3], getbatterydata[9], getvalvedata[9];

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
extern void WiFiEvent(WiFiEvent_t event);
extern void Toysmyth_Check_FirmwareUpdate(); 

//-------------------------------------------------------------------------------
// EEPROM 에 저장되어 있는 설정값을 획득한다. 
//-------------------------------------------------------------------------------
void  EEPROM_Get_DeviceInformation()
{
  int   iPosition=0x00;
  char  szBuffer[128];

  EEPROM.begin(sizeof(int)+sizeof(int)+16);
  EEPROM.get(iPosition,iZoneID);          // iZoneID 정보를 읽는다.
  iPosition = sizeof(int);
  EEPROM.get(iPosition,iDeviceNumber);    // iDeviceNumber 
  sprintf(szBuffer,"%03d",iZoneID);
  zone_id = String(szBuffer);
  sprintf(szBuffer,"%03d",iDeviceNumber);
  machine_no = String(szBuffer);
  iPosition += sizeof(int);
  EEPROM.get(iPosition,szMacAddr);        // MacAddress 정보를 읽는다.
  if (szMacAddr[0]=='D' && szMacAddr[1]=='8') {   // 정상적인 Mac Address 라고하면 하드코딩된 주소를 대체한다. 
    mac_addr = szMacAddr;
  }
}

//-------------------------------------------------------------------------------
// EEPROM 에 저장되어 있는 설정값을 획득한다. 
//-------------------------------------------------------------------------------
void  EEPROM_Set_DeviceInformation()
{
  int   iPosition=0x00;

  EEPROM.begin(sizeof(int)+sizeof(int)+16);
  EEPROM.put(iPosition,iZoneID);            // iZoneID 정보를 저장한다
  iPosition = sizeof(int);
  EEPROM.put(iPosition,iDeviceNumber);      // iDeviceNumber 정보를 저장한다
  iPosition = sizeof(int)+sizeof(int);
  EEPROM.put(iPosition,szMacAddr);          // MacAddress 정보를 저장한다
  EEPROM.commit();
}

//-------------------------------------------------------------------------------
// SPIFFS 관련된 함수
//-------------------------------------------------------------------------------
void  RecordData_Status_byWrite(unsigned char iVolt,unsigned char iValve)
{
  char      szFileName[256];
  int       iFilePos;
  RecordData_Status   stServerStatus;
  struct tm timeinfo;

  if (getLocalTime(&timeinfo)) {
    sprintf(szFileName,"S%04d-%02d-%02d.Dat",(timeinfo.tm_year+1900),timeinfo.tm_mon+1,timeinfo.tm_mday);
    if (SPIFFS.exists(szFileName)) {
      WriteFile = SPIFFS.open(szFileName, "wb");  //O_RDWR|O_CREAT);
      if (WriteFile) {    // 파일이 존재한다면... 오늘 날자와 시간을 확인하여 파일의 위치를 특정한다. 
        WriteFile.flush();
        iFilePos = timeinfo.tm_hour + timeinfo.tm_min > 50 ? 1:0;
        WriteFile.seek(iFilePos);
        WriteFile.write(iVolt);
        iFilePos = 24 + timeinfo.tm_hour + timeinfo.tm_min > 50 ? 1:0;
        WriteFile.seek(iValve);
        WriteFile.close();
      }            
    } else {    // 파일이 존재하지 않는다면 초기화하면 생성한다.
      WriteFile = SPIFFS.open(szFileName, "wb");  //O_RDWR|O_CREAT);
      if (WriteFile) {
        memset((byte*)&stServerStatus,0x00,sizeof(RecordData_Status));
        WriteFile.flush();
        WriteFile.seek(0);
        WriteFile.write((byte*)&stServerStatus,sizeof(RecordData_Status));
        WriteFile.close();
      }
    }
  }
}

//-------------------------------------------------------------------------------
// SPIFFS 관련된 함수(해당월의 상태값을 읽어오는 함수)
//-------------------------------------------------------------------------------
int  RecordData_Status_byRead(short int iYear,short int iMonth,short int iDay)
{
  char      szFileName[256],iChkRet;

  sprintf(szFileName,"S%04d-%02d-%02d.Dat",iYear,iMonth,iDay);
  if (SPIFFS.exists(szFileName)) {
    ReadFile = SPIFFS.open(szFileName, "wb");  //O_RDWR|O_CREAT);
    if (WriteFile) {    // 파일이 존재한다면... 오늘 날자와 시간을 확인하여 파일의 위치를 특정한다. 
      ReadFile.flush();
      ReadFile.seek(0x00);
      ReadFile.read((byte*)&stServerStatus,sizeof(RecordData_Status));    
      ReadFile.close();
      return(0x01);
    }            
  }
  return(0x00);
}

//---------------------------------------------------------------------------
// 초기화 관련 함수
//---------------------------------------------------------------------------
void Serial_Setting() {
  // Serial 통신속도 115200(esp32 기본 속도) 으로 동작
  Serial.begin(115200);
  // 보드가 연결 후 재부팅하는 동안 시리얼 통신이 제대로 시작되지 않을 수 있기 때문에 1초 딜레이
  delay(1000);                                      
  while (!Serial) {
  }
}

void rs485_Setting() {
  rs485.begin(9600, SERIAL_8N1, RXD2, TXD2); // 의미 : 9600 bps / asynchronous 8 data bit 1 stop bit no parity
  rs485.flush();
}

void rtc_Setting() {
  pinMode(14, INPUT_PULLUP);  // SDA 
  pinMode(15, INPUT_PULLUP);  // SCL
  Wire.begin(15, 14);         // SDA를 14번 핀, SCL을 15번 핀으로 지정
  
  // wakeupPin을 입력으로 설정 및 풀업 저항 활성화
  pinMode(wakeupPin, INPUT_PULLUP);

  // rtc 모듈 초기화 및 작동 확인
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    LTE_OFF();
    Serial.flush();
    ESP.restart();
  }
  // Disable and clear alarm
  rtc.disableAlarm(1);
  rtc.clearAlarm(1);
  rtc.writeSqwPinMode(DS3231_OFF);  
}

void interrupt_Setting(){
  // 내부 풀업 저항을 활성화하여 버튼 핀을 입력으로 설정
  pinMode(buttonPin, INPUT_PULLUP);
  // 버튼 핀에 인터럽트 연결, FALLING은 HIGH에서 LOW로 바뀔 때 인터럽트 발생
  attachInterrupt(digitalPinToInterrupt(buttonPin), restartESP, FALLING);
}

//---------------------------------------------------------------------------
// LTE를 켤 때 함수(현재는 릴레이 스위치 HIGH, LTE연결 확인, 미연결 시 DeepSleep)
// 확인필요 : ntp 시간이 정상적인지 확인하여 기존 RTC 와 차이가 발생한다면 예외처리 필요
//---------------------------------------------------------------------------
void LTE_ON() {
  // 연결확인 용 지역변수 count
  int     cnt = 0;
  long    iCurDay,iChkDay;
  char    szBuffer[256],szDateTime[128];
  
  RSW_HIGH();
  // LTE연결 확인용 do while문
  do {
    // 10 회 반복 후 작동
    if (++cnt >= 10) {
      // LTE를 LOW 설정하지 않으면 HIGH로 계속 설정되어 전력 소모.
      LTE_OFF();
      now = rtc.now();
      // 인터넷이 연결되지 못해 NTP동기화를 못했을 경우 겨울철은 7일 동안 자게 함. (정상 작동 시 월요일 12시 50분에 일어난다고 하면 다음주 월요일 12시 57분에 일어나야 함)
      if (now.month() == 1 || now.month() == 2 || now.month() == 12) {
        ATime = now + TimeSpan(7, 0, 0, 0); // 7일 이후
        AlarmTime = DateTime(ATime.year(), ATime.month(), ATime.day(), ATime.hour(), 58, 0);
        config_sleep_mode();
        Serial.println("Can not connect ethernet -> ESP sleep now(7days - Winter)");
        Serial.flush();
        esp_deep_sleep_start();
      }
      ATime = now + TimeSpan(1, 0, 0, 0); // 1일 이후
      AlarmTime = DateTime(ATime.year(), ATime.month(), ATime.day(), ATime.hour(), 58, 0);
      config_sleep_mode();
      Serial.println("Can not connect ethernet -> ESP sleep now");
      Serial.flush();
      esp_deep_sleep_start();
    }
    Serial.println("Waiting for ethernet connection");
    delay(10000);
  } while (!eth_connected);

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // ntp 동기화
  now = rtc.now();    // 현재 RTC 시간을 확인
  iCurDay = (now.year()*365 + now.month() * 31 + now.day()) * 24 + now.hour();   // 대략적인 월과일 및 시간을 시간단위로 계산
  sprintf(szDateTime,"%d-%02d-%02d %02d:%02d:%02d",now.year(),now.month(),now.day(),now.hour(),now.minute(),now.second());  
  sprintf(szBuffer,"RTC_Time:[%s] BootCount = %d",szDateTime,bootCount);
  Serial.println(szBuffer);                     // 로그용
  MRD_Exception_to_Server(NULL,NULL,0x01,szBuffer,NULL,NULL);
  
  getLocalTime(&timeinfo);
  sprintf(szDateTime,"%d-%02d-%02d %02d:%02d:%02d",1900+timeinfo.tm_year,timeinfo.tm_mon+1,timeinfo.tm_mday,timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec);  
  iChkDay = ((timeinfo.tm_year+1900)*365+(timeinfo.tm_mon+1)*31 + timeinfo.tm_mday) * 24 + timeinfo.tm_hour;
  sprintf(szBuffer,"NTC_Time:[%s] Firmware Version = %s",szDateTime,szFirmwareVersion);
  Serial.println(szBuffer);                     // 로그용
  MRD_Exception_to_Server(NULL,NULL,0x01,szBuffer,NULL,NULL);

  if (abs(iCurDay-iChkDay) > 2) {   // RTC 시간과 NTP 시간이 1시간이상 차이일 경우에는 확인이 필요하다
    iCurDay = iChkDay;              // 기존 RTC 시간을 CurDay 에 넣어두고 NTP를 한번 더 수행한다.
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    getLocalTime(&timeinfo);
    sprintf(szBuffer,"NTC_Time #1:[%s] Days = %ld",szDateTime,iCurDay);
    Serial.println(szBuffer);                     // 로그용
    iChkDay = ((timeinfo.tm_year+1900)*365+(timeinfo.tm_mon+1)*31 + timeinfo.tm_mday) * 24 + timeinfo.tm_hour;
    sprintf(szDateTime,"%d-%02d-%02d %02d:%02d:%02d",1900+timeinfo.tm_year,timeinfo.tm_mon+1,timeinfo.tm_mday,timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec);  
    sprintf(szBuffer,"NTC_Time #2:[%s] Days = %ld",szDateTime,iChkDay);
    Serial.println(szBuffer);                     // 로그용
    if (abs(iCurDay-iChkDay) > 2) {   // 두번의 NTP 가 차이가 있을경우에는 NTP 가 문제가 있다고 판단해야 한다.
      sprintf(szBuffer,"RTC,NTC Time Different");
      MRD_Exception_to_Server(NULL,NULL,0x02,szBuffer,NULL,NULL);
      LTE_OFF();
      ESP.restart();
    }
  }
}

// LTE를 끌 때 함수(현재는 릴레이 스위치 LOW)
void LTE_OFF() {
  for(int i=0;i<10;i++) delay(1000);
  RSW_LOW();
  delay(5000);
}

// rs485 통신 종료 함수
void rs485_off() {
  rs485.end();
}

// 릴레이 스위치 HIGH
void RSW_HIGH() {
  Serial.println("RSW : HIGH");               // 로그용
  // pinMode 2번 핀이 OUTPUT(출력)으로 설정되었으니 출력을 HIGH로 설정해 3.3V를 릴레이 스위치 신호선에 출력해 동작시킨다. 이후 LTE 라우터의 부팅 시간(40초)을 기다려준다
  digitalWrite(ROUTER_CONTROL, HIGH);
  delay(40000);
  Serial.println("done");
}

// 릴레이 스위치 LOW
void RSW_LOW() {
  Serial.println("RSW : LOW");                // 로그용
  // pinMode 2번 핀이 OUTPUT(출력)으로 설정되었으니 출력을 LOW로 설정해 0V를 릴레이 스위치 신호선에 출력해 동작을 멈춘다.
  digitalWrite(ROUTER_CONTROL, LOW);
  Serial.println("done");                     // 로그용
}

/*NTP 관련 함수*/
//NTP 동기화 함수
void setLocalTime() {
  // ntp동기화 재시도를 위한 count
  int cnt = 0;
  do {
    // 5회 반복 후 실행, ESP 재시작
    if (cnt++ > 5) {
      Serial.println("Failed to obtain time");
      LTE_OFF();
      ESP.restart();
      return;
    }
    // getLocalTime()은 Time.h의 내장함수, 현재 시간을 받아온다.
    getLocalTime(&timeinfo);
    Serial.print("NTP Sync try - ");
    Serial.println(cnt);
    delay(1000);
  } while (!getLocalTime(&timeinfo));

// getLocalTime()은 현재년도 - 1900, 현재 월 - 1 값을 리턴하기 떄문에 연산을 추가해준다.
// 변수의 값을 변경하는것보다는 실제 사용하는 부분에서 년도에 1900 , 월에는 1 을 더해주는 방식을 사용하는것이 안전하다.
//  timeinfo.tm_year = timeinfo.tm_year + 1900;
//  timeinfo.tm_mon = timeinfo.tm_mon + 1;
  print_time();
  set_rtc();
  Serial.println("NTP Sync Done");
}

//getLocalTime()으로 받아온 시간 정보를 로그로 찍는다.
void print_time() {
  char szDateTime[128];

  sprintf(szDateTime,"print_time() : %d-%02d-%02d %02d:%02d:%02d",timeinfo.tm_year+1900,timeinfo.tm_mon+1,timeinfo.tm_mday,timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec);  
  Serial.println(szDateTime);
}

void set_rtc(){
  // DateTime 객체 now를 ntp서버에 맞게 초기화
  char szDateTime[128];
  DateTime now(timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec); 
  // RTC.adjust(DateTime(year, month, day, hour, min, sec))
  // 위 함수는 괄호안의 정해진 시간으로 초기화 해주는 함수로, 지금은 now의 시간대로 설정해주는 것이고 이 함수가 실행된 이후부터 RTC가 작동한다.

  rtc.adjust(now);
  sprintf(szDateTime,"set_rtc() - adjust : %d-%02d-%02d %02d:%02d:%02d",now.year(),now.month(),now.day(),now.hour(),now.minute(),now.second());  
  Serial.println(szDateTime);

  now = rtc.now();
  sprintf(szDateTime,"set_rtc() - after  : %d-%02d-%02d %02d:%02d:%02d",now.year(),now.month(),now.day(),now.hour(),now.minute(),now.second());  
  Serial.println(szDateTime);
}

/*인터럽트 관련 함수*/
// 인터럽트 서비스 루틴(ISR)
void restartESP() {
  LTE_OFF();
  // ESP를 재시작하는 함수 호출
  ESP.restart();
}

// 인터럽트 debounce 처리
void restartESPDebounce() {
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = millis();
  
  // 200ms보다 짧은 인터럽트는 무시 (debounce)
  if (interruptTime - lastInterruptTime > 200) {
    LTE_OFF();  
    ESP.restart(); // ESP를 재시작하는 함수 호출
  }
  lastInterruptTime = interruptTime;
}

// TIME_TO_SLEEP 계산
void cal_TTS() {
  char  szBuffer[128],szDateTime1[64],szDateTime2[64];
  now = rtc.now();
  TimeSpan timeDifference = AlarmTime - now;
  long TIME_TO_SLEEP = timeDifference.totalseconds();

  sprintf(szDateTime1,"%d-%02d-%02d %02d:%02d:%02d",now.year(),now.month(),now.day(),now.hour(),now.minute(),now.second());  
  sprintf(szDateTime2,"%d-%02d-%02d %02d:%02d:%02d",AlarmTime.year(),AlarmTime.month(),AlarmTime.day(),AlarmTime.hour(),AlarmTime.minute(),AlarmTime.second());  
  sprintf(szBuffer,"Now=%s , TIME_TO_SLEEP=%s",szDateTime1,szDateTime2);
  MRD_Exception_to_Server(NULL,NULL,0x01,szBuffer,NULL,NULL);

  Serial.print("Time difference in seconds: ");
  Serial.println(TIME_TO_SLEEP);
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
void print_wakeup_reason() {
  // esp32 내장함수
  esp_sleep_wakeup_cause_t wakeup_reason;
  // 자기 전에 설정하거나 자는 도중 들어온 wakeup 이유
  wakeup_reason = esp_sleep_get_wakeup_cause();
  switch (wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0     : Serial.println("Wakeup caused by external signal using RTC_IO");           
//                                     MRD_Exception_to_Server(NULL,NULL,0x02,"Wakeup Reason : RTC_IO",NULL,NULL);
                                     break;
    case ESP_SLEEP_WAKEUP_EXT1     : Serial.println("Wakeup caused by external signal using Reset_Button");     
                                     MRD_Exception_to_Server(NULL,NULL,0x02,"Wakeup Reason : Reset_Button",NULL,NULL);
                                     restartESP();
                                     break;
    case ESP_SLEEP_WAKEUP_TIMER    : Serial.println("Wakeup caused by timer");                                  
                                     break;  //and this
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad");                               
                                     MRD_Exception_to_Server(NULL,NULL,0x02,"Wakeup Reason : TOUCHPAD",NULL,NULL);
                                     break;
    case ESP_SLEEP_WAKEUP_ULP      : Serial.println("Wakeup caused by ULP program");                            
                                     MRD_Exception_to_Server(NULL,NULL,0x02,"Wakeup Reason : ULP program",NULL,NULL);
                                     break;
    default                        : Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); 
                                     break;
  }
}

//---------------------------------------------------------------------------
// 겨울철 및 전압 체크 관련 함수( 12월 ~ 2월)
//---------------------------------------------------------------------------
void Winter_Operation() {
  unsigned long start_time, elapsed_time;
  Serial.println("Winter Operation");

  now = rtc.now();
  
  //timeinfo.tm_wday = 0; // 겨울철 요일 테스트용
  switch (timeinfo.tm_wday) {
    case 0: ATime = now + TimeSpan(1, 0, 0, 0); break; //sun
    case 1: ATime = now + TimeSpan(7, 0, 0, 0); break; //mon
    case 2: ATime = now + TimeSpan(6, 0, 0, 0); break; //tue
    case 3: ATime = now + TimeSpan(5, 0, 0, 0); break; //wed
    case 4: ATime = now + TimeSpan(4, 0, 0, 0); break; //thu
    case 5: ATime = now + TimeSpan(3, 0, 0, 0); break; //fri
    case 6: ATime = now + TimeSpan(2, 0, 0, 0); break; //sat
    default: Serial.println("Winter Month Data Error");
  }
  // 월요일이지만 12시 50분 이전인 경우
  if (timeinfo.tm_wday == 1 && timeinfo.tm_hour <= 12 && timeinfo.tm_min < 50) {
    ATime = now;
  }
  AlarmTime = DateTime(ATime.year(), ATime.month(), ATime.day(), 12, 58, 0);

  // 첫 부팅이 아닌 경우에 서버에 데이터를 보냄(첫 부팅은 이미 이전에 보냈음)
  if (!(bootCount == 1)) {
    Battery_Read();                               // 노딕보드에서 배터리값 받아오기
    Valve_Read();                                 // 노딕보드에서 밸브값 받아오기
    if (getbatterydata[0]==0x00 && getvalvedata[0]==0x00) {   // 둘다 무응답 일경우
      MRD_DataValue_to_Server(0,getvalvedata,NULL,NULL);
    } else {
      post_battery_to_server(0);                      // MIT, 토이스미스 서버에 battery값 송신
      post_valve_to_server(0);                        // MIT, 토이스미스 서버에 valve값 송신
    }
  }
  config_sleep_mode();
  Serial.println("Going to sleep now");
  Serial.flush();
  LTE_OFF();
  esp_deep_sleep_start();
}

//---------------------------------------------------------------------------
// 전송 금지 시간대 작동하는 함수
//---------------------------------------------------------------------------
void Night_Operation() {
  unsigned long start_time, elapsed_time;
  Serial.println("Night Operation");
  
  // 첫 부팅이 아닌 경우에 서버에 데이터를 보냄(첫 부팅은 이미 이전에 보냈음)
  if (!(bootCount == 1)) {
    Battery_Read();                               // 노딕보드에서 배터리값 받아오기
    Valve_Read();                                 // 노딕보드에서 밸브값 받아오기
    if (getbatterydata[0]==0x00 && getvalvedata[0]==0x00) {   // 둘다 무응답 일경우
      MRD_DataValue_to_Server(0,getvalvedata,NULL,NULL);
    } else {
      post_battery_to_server(0);                      // MIT, 토이스미스 서버에 battery값 송신
      post_valve_to_server(0);                        // MIT, 토이스미스 서버에 valve값 송신
    }
  }

  now = rtc.now();
  switch (timeinfo.tm_hour) {
    case 21: ATime = now + TimeSpan(1, 0, 0, 0); break; //TIME_TO_SLEEP = ( 6 * Shour + (50 - timeinfo.tm_min) * Smin ); break;
    case 22: ATime = now + TimeSpan(1, 0, 0, 0); break; //TIME_TO_SLEEP = ( 6 * Shour + (50 - timeinfo.tm_min) * Smin ); break;
    case 23: ATime = now + TimeSpan(1, 0, 0, 0); break; //TIME_TO_SLEEP = ( 6 * Shour + (50 - timeinfo.tm_min) * Smin ); break;
    case 0:  ATime = now; break;     //TIME_TO_SLEEP = ( 5 * Shour + (50 - timeinfo.tm_min) * Smin ); break;
    case 1:  ATime = now; break;     //TIME_TO_SLEEP = ( 4 * Shour + (50 - timeinfo.tm_min) * Smin ); break;
    case 2:  ATime = now; break;     //TIME_TO_SLEEP = ( 3 * Shour + (50 - timeinfo.tm_min) * Smin ); break;
    case 3:  ATime = now; break;     //TIME_TO_SLEEP = ( 2 * Shour + (50 - timeinfo.tm_min) * Smin ); break;
    case 4:  ATime = now; break;     //TIME_TO_SLEEP = ( 1 * Shour + (50 - timeinfo.tm_min) * Smin ); break;
    default: Serial.println("Nitht Time Data Error");
  }
  AlarmTime = DateTime(ATime.year(), ATime.month(), ATime.day(), 5, 58, 0);

  config_sleep_mode();
  Serial.println("Going to sleep now");
  Serial.flush();
  LTE_OFF();
  esp_deep_sleep_start();
}

//---------------------------------------------------------------------------
// 저전력(11.2V 이상 11.7V 미만) 시 작동하는 함수
//---------------------------------------------------------------------------
void Low_battery() {
  Serial.println("Low Battery State");
  
  if (!(bootCount == 1)) {
    Battery_Read();                               // 노딕보드에서 배터리값 받아오기
    Valve_Read();                                 // 노딕보드에서 밸브값 받아오기
    if (getbatterydata[0]==0x00 && getvalvedata[0]==0x00) {   // 둘다 무응답 일경우
      MRD_DataValue_to_Server(0,getvalvedata,NULL,NULL);
    } else {
      post_battery_to_server(0);                      // MIT, 토이스미스 서버에 battery값 송신
      post_valve_to_server(0);                        // MIT, 토이스미스 서버에 valve값 송신
    }
  }

  now = rtc.now();
  //주간: 4시간 sleep
  if ((07 <= timeinfo.tm_hour) && (timeinfo.tm_hour <= 17)) {
    ATime = now + TimeSpan(0, 4, 0, 0);
    AlarmTime = DateTime(ATime.year(), ATime.month(), ATime.day(), ATime.hour(), 58, 0);
    Serial.println("Sleep mode for  4hours");
  }
  //야갼: 익일 오후 12시 sleep
  else {
    if ((18 <= timeinfo.tm_hour) && (timeinfo.tm_hour <= 23)) {
      ATime = now + TimeSpan(1, 0, 0, 0);
      AlarmTime = DateTime(ATime.year(), ATime.month(), ATime.day(), 11, 58, 0);
    }
    else {
      ATime = now;
      AlarmTime = DateTime(ATime.year(), ATime.month(), ATime.day(), 11, 58, 0);
    }
    Serial.println("Sleep mode until 12PM");
  }
  
  config_sleep_mode();
  Serial.flush();
  Serial.println("Going to sleep now");
  LTE_OFF();
  esp_deep_sleep_start();
}

//---------------------------------------------------------------------------
// 방전(11.2V 미만) 시 작동하는 함수 24h DeepSleep
//---------------------------------------------------------------------------
void Discharge() {
  Serial.println("Discharge State");
  
  if (!(bootCount == 1)) {
    Battery_Read();                               // 노딕보드에서 배터리값 받아오기
    Valve_Read();                                 // 노딕보드에서 밸브값 받아오기
    if (getbatterydata[0]==0x00 && getvalvedata[0]==0x00) {   // 둘다 무응답 일경우
      MRD_DataValue_to_Server(0,getvalvedata,NULL,NULL);
    } else {
      post_battery_to_server(0);                      // MIT, 토이스미스 서버에 battery값 송신
      post_valve_to_server(0);                        // MIT, 토이스미스 서버에 valve값 송신
    }
  }

  now = rtc.now();
  ATime = now + TimeSpan(1, 0, 0, 0);
  AlarmTime = DateTime(ATime.year(), ATime.month(), ATime.day(), ATime.hour(), 58, 0);

  config_sleep_mode();
  Serial.flush();
  Serial.println("Going to sleep now");
  LTE_OFF();
  esp_deep_sleep_start();
}

//---------------------------------------------------------------------------
// 정상작동 시 58분 이전에 깨어나는 경우에 작동하는 함수
//---------------------------------------------------------------------------
void Not_Yet() {
  char    iChkRet;
  int     iFilePos;
  now =   rtc.now();
  
  Serial.println("Early wake up");
/*  
  iChkRet = RecordData_Status_byRead(now.year(), now.month(),now.day());
  if (iChkRet==0x01) {      // 데이터를 SPIFFS 에서 읽어왔으면
    iFilePos = now.hour() + now.minute() > 50 ? 1:0;
    if (stServerStatus.Status_Volt[iFilePos]==0x00) {
      Battery_Read();                                 // 노딕보드에서 배터리값 받아오기
      Valve_Read();                                   // 노딕보드에서 밸브값 받아오기
      if (getbatterydata[0]==0x00 && getvalvedata[0]==0x00) {   // 둘다 무응답 일경우
        MRD_DataValue_to_Server(0,getvalvedata,NULL,NULL);
      } else {
        post_battery_to_server(0);                      // MIT, 토이스미스 서버에 battery값 송신
        post_valve_to_server(0);                        // MIT, 토이스미스 서버에 valve값 송신
        RecordData_Status_byWrite((int)(volt*10),valve);          // 서버로 전송했다는걸 남기는 코드
        MRD_Exception_to_Server(NULL,NULL,0x02,"Not_Yet : Addition Volt&Valve Check ",NULL,NULL);
      }
    }
  }
*/
  ATime = now;
  AlarmTime = DateTime(ATime.year(), ATime.month(), ATime.day(), ATime.hour(), 58, 0);
  Serial.println("earlier than 58 min");
  config_sleep_mode();
  Serial.println("Going to sleep now");
  Serial.flush();
  LTE_OFF();
  esp_deep_sleep_start();
}


//---------------------------------------------------------------------------
// 50분 이후 정상작동 알고리즘 (에인루프쪽으로 이동을 체크할 함수)
//---------------------------------------------------------------------------
void Normal_Operation() {
  Serial.println("Normal Operation");

  // 첫 부팅이 아닌 경우에 서버에 데이터를 보냄(첫 부팅은 이미 이전에 보냈음)
  if (!(bootCount == 1)) {
    Battery_Read();                               // 노딕보드에서 배터리값 받아오기
    Valve_Read();                                 // 노딕보드에서 밸브값 받아오기
    if (getbatterydata[0]==0x00 && getvalvedata[0]==0x00) {   // 둘다 무응답 일경우
      MRD_DataValue_to_Server(0,getvalvedata,NULL,NULL);
    } else {
      post_battery_to_server(0);                      // MIT, 토이스미스 서버에 battery값 송신
      post_valve_to_server(0);                        // MIT, 토이스미스 서버에 valve값 송신
    }
  }

  setLocalTime();                                   // 계속 RTC 를 재설정할 필요는 없다.
  do {
    delay(10*1000);
    getLocalTime(&timeinfo);
  } while (!(2<=timeinfo.tm_min && timeinfo.tm_min<=10));   // 2 분에 작동하도록 루프 조건을 수정

  get_api();                                      // api1
  Valve_Read();                                   // 노딕보드에서 밸브값 받아오기
  if (getvalvedata[0]==0x00) {                    // 밸브데이터 무응답 일경우
    MRD_DataValue_to_Server(0,getvalvedata,NULL,NULL);
  } else {
    post_valve_to_server(0);                        // MIT, 토이스미스 서버에 valve값 송신
    // RecordData_Status_byWrite((int)(volt*10),valve);          // 서버로 전송했다는걸 남기는 코드
  }

  now = rtc.now();
  ATime = now;
  AlarmTime = DateTime(ATime.year(), ATime.month(), ATime.day(), ATime.hour(), 58, 0);
  config_sleep_mode();                            // rs485 통신 종료
  Serial.println("Going to sleep now ");
  Serial.flush();
  LTE_OFF();                                      // LTE를 끔(안끄면 라우터 계속 작동)
  esp_deep_sleep_start();                         // DeepSleep
}


//---------------------------------------------------------------------------
// DeepSleep 관련 함수
// deepsleep 하는 시간을 적용시키고 자는 시간 일어나는 이유를 로그로 출력함.
//---------------------------------------------------------------------------
void config_sleep_mode() {
  rtc.setAlarm1(AlarmTime, DS3231_A1_Date);
  Serial.print("Current time: ");
  now = rtc.now();
  Serial.println(now.timestamp(DateTime::TIMESTAMP_FULL));
  Serial.println(now.day());

  cal_TTS();
  
  // LOW를 인식하면 깨어나게 합니다
  esp_sleep_enable_ext0_wakeup(gpio_num_t(wakeupPin), LOW);
  uint64_t bitmask = 1ULL << buttonPin;
  esp_sleep_enable_ext1_wakeup(bitmask, ESP_EXT1_WAKEUP_ANY_HIGH);
}

//---------------------------------------------------------------------------
// MRD 프로토콜을 화면에 출력하기 위하여 만들어진 함수
//---------------------------------------------------------------------------
void Display_DebugMessage(int iMode,unsigned char *szCommand, int iSize)
{
    //TODO: Add your source code here
  struct tm       timeinfo;
  char  szBuffer[512],szTemp[32],szDateTime[128];
  int     i , j , iCount , iValue , iChkColor;

  if (getLocalTime(&timeinfo)) {
    sprintf(szDateTime,"%s %d-%02d-%02d %02d:%02d:%02d [Count=%d]",iMode==0x00?"[Recv]":"[Send]",1900+timeinfo.tm_year,timeinfo.tm_mon+1,timeinfo.tm_mday,timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec,iSize);  
    Serial.println( szDateTime );
  }
  for(i=0;i<iSize;i+=0x10) {
    memset( szBuffer , 0x00 , sizeof(szBuffer) );
    sprintf( szBuffer , "  [%04X] : " , i );  // Offset Display
    for(j=0;j<0x10;j++) {
      if (i+j<iSize) sprintf( szTemp , "%02X " , szCommand[i+j] );
       else sprintf( szTemp , "   " );
      strcat( szBuffer , szTemp );
    }
    Serial.println( szBuffer );
  }
}

//---------------------------------------------------------------------------
// 초기화 및 초기 설정
void init_Setting() {
  unsigned char   nData;

  // 시리얼 통신 시작
  Serial_Setting();                             
  for(int i=0;i<5;i++) {
    Serial.printf("Wait %d Second....[ Press 'e' key to EEPROM reset ]\n",5-i);
    delay(1000);
  }
  if (Serial.available() > 0) {     // 시리얼 버퍼에 내용이 존재한다면
    while (Serial.available() > 0) {
      nData = Serial.read();
      if (nData=='E' || nData=='e') {
        Serial.print(" InitMode : EEPROM Clear !!!\n");
        Serial.flush();
        iZoneID = 0x00;
        iDeviceNumber = 0x00;
        memset(szMacAddr,0x00,sizeof(szMacAddr));      
        EEPROM_Set_DeviceInformation();
        ESP.restart();   
        break;
      }
    }
  }

  // rs485 통신 시작
  rs485_Setting();

  // rtc 기본 세팅
  rtc_Setting();

  // 인터럽트 기본 세팅
  interrupt_Setting();

  // 부팅 횟수 증가
  ++bootCount;
  
  // void setup() 함수 시작 알림
  Serial.print("Setup() : ");
  
  // deepsleep에서 깰 때마다 실행, booting횟수 추적
  Serial.println("Boot number: " + String(bootCount));
  
  // 버전 확인용 로그
  Serial.print("악취저감장치 V4.0 240328 - ");
  Serial.println(mac_addr);
  
  // 릴레이 스위치 gpoi 출력 설정
  pinMode(ROUTER_CONTROL, OUTPUT);
 
  // 이벤트 발생마다(인터넷 상태마다) 인터럽트 식으로 괄호 안의 함수를 실행. esp32 기본 내장 함수
  WiFi.onEvent(WiFiEvent);
  
  // 랜선관련 설정(WT01_ETH01 데이터시트 참고), ETH.h 내장함수
  ETH.begin(ETH_ADDR, ETH_POWER_PIN, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_TYPE, ETH_CLK_MODE);
  
  // time.h 내장 함수, 시차 및 섬머타임과 ntp서버를 설정해준다.
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  /*
  if (!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    is_SPIFFS = false;
  } else {
    is_SPIFFS = true;
  }
  */
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
void setup() {
// EEPROM 에서 ZoneID 와 MachineID 및 MacAddr 을 읽어온다.
  EEPROM_Get_DeviceInformation();               // EEPROM 에서 ZoneID 와 MachineID 를 읽어온다.

  init_Setting();                               // 초기화 부분 - pinMode(RSW), ETH Setting, ntp 설정
  LTE_ON();                                     // LTE 연결
  print_wakeup_reason();                        // 깨어난 이유를 출력한다. (LTE 가 활성화 되어야 한다.) 리셋버튼에 대한 처리를 한다.

  setLocalTime();                               // NTP 동기화
  get_device_config();                          // zone_id machine_no 받아오기(http 통신 - restAPI)

  do {
  } while(!getLocalTime(&timeinfo)); 
  Toysmyth_Check_FirmwareUpdate(); 

  if (bootCount == 1) {                         // 첫 동작시에만 작동
    Battery_Read();                               // 노딕보드에서 배터리값 받아오기
    Valve_Read();                                 // 노딕보드에서 밸브값 받아오기
    if (getbatterydata[0]==0x00 && getvalvedata[0]==0x00) {   // 둘다 무응답 일경우
      MRD_DataValue_to_Server(0,getvalvedata,NULL,NULL);
    } else {
      post_battery_to_server(0);                      // MIT, 토이스미스 서버에 battery값 송신
      post_valve_to_server(0);                        // MIT, 토이스미스 서버에 valve값 송신
    }
  } else {    // 향후 데이터를 항상 서버로 전송할지를 결정
  }

  switch (timeinfo.tm_mon+1) {                    // 겨울철 확인
    case 12: case 1: case 2: {                  // 겨울철인 경우
        MRD_Exception_to_Server(NULL,NULL,0x01,"Mode :Winter_Operation()",NULL,NULL);
        Winter_Operation();
        break;
      }
    default: {                                  // 봄, 여름, 가울인 경우
        // 비작동 시간 대역
        if (timeinfo.tm_hour >= 21 || timeinfo.tm_hour < 5) {
          MRD_Exception_to_Server(NULL,NULL,0x01,"Mode :Night_Operation()",NULL,NULL);
          // MRD_StatusData_to_Server_byDay();
          Night_Operation();
        }
        if (0 < volt && volt < 11.2) {                  // 방전
          MRD_Exception_to_Server(NULL,NULL,0x02,"Mode :Discharge()",NULL,NULL);
          Discharge();
          break;
        } else if (11.2 <= volt && volt <= 11.7) {      // 저전력
          MRD_Exception_to_Server(NULL,NULL,0x02,"Mode :Low_battery()",NULL,NULL);
          Low_battery();
          break;
        } else {                                    
          if (10 <= timeinfo.tm_min && timeinfo.tm_min < 56) {   // 56분 이전            
            MRD_Exception_to_Server(NULL,NULL,0x01,"Mode :Not_Yet()",NULL,NULL);
            Not_Yet();
            break;
          } else {                                      // 정상동작
            MRD_Exception_to_Server(NULL,NULL,0x01,"Mode :Normal_Operation()",NULL,NULL);
            Normal_Operation();                     
            break;
          }
        }
      }
  }
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
void loop() {
  // put your main code here, to run repeatedly:
/*  
  struct tm       timeinfo;
  char  szBuffer[512],szDateTime[128];
  static int iFirstTime = 1;
  static int iTerminated = 0;
  static unsigned long timepoint = millis();

  if (iFirstTime==0x01) {
    iFirstTime = 0x00;
    Serial.println("Normal Operation");
    Battery_Read();                                 // 노딕보드에서 배터리값 받아오기
    Valve_Read();                                   // 노딕보드에서 밸브값 받아오기
    post_battery_to_server(0);                      // MIT, 토이스미스 서버에 battery값 송신
    post_valve_to_server(0);                        // MIT, 토이스미스 서버에 valve값 송신
  }
  if ((millis()-timepoint)>(990U)) {                  //time interval: 0.99s , 
    timepoint = millis();
    getLocalTime(&timeinfo);
    if (timeinfo.tm_min>=2) {                         // 매시간 2분이 넘었을 경우에만 수행.
      setLocalTime();                                 // RTC 는 한번만 수행한다.
      get_api();                                      // api1
      Valve_Read();                                   // 노딕보드에서 밸브값 받아오기
      post_valve_to_server(0);                        // MIT, 토이스미스 서버에 valve값 송신    }
      iTerminated = 1;
    }
    if (timeinfo.tm_min>=2 && iTerminated==1) {
      now = rtc.now();
      AlarmTime = DateTime(now.year(), now.month(), now.day(), now.hour(), 58, 0);
      LTE_OFF();                                      // LTE를 끔(안끄면 라우터 계속 작동)
      delay(1000);                                    // /LTE 연결 관련 코드로 확인할까? 굳이?
      config_sleep_mode();                            // rs485 통신 종료
      Serial.println("Going to sleep now ");
      Serial.flush();
      esp_deep_sleep_start();                         // DeepSleep          
    }
  }
*/  
}
