#include <Arduino.h>       //for ethernet
#include <stdio.h>       
#include <HardwareSerial.h> //for rs485 comm
#include "UserDefine.h"

extern HardwareSerial rs485; // rxtx mode 2 of 0,1,2
extern int    iZoneID;
extern int    iDeviceNumber;
extern int    valve;
extern float  volt;

Protocol_MRD  stSendMsg, stRecvMsg;
//---------------------------------------------------------------------------
// MRD protocol 9byte 번째 CheckSum 계산 함수.
//---------------------------------------------------------------------------
byte cal_checksum(byte *ptr, unsigned int len) {
  byte checksum = 0;
  for (int i = 0; i < len; i++)
    checksum += ptr[i];
  return checksum;
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
bool  MRD_SendData(unsigned char *pChkPnt)
{
  rs485.flush();
  delay(200);
// rs485를 통해 data 9byte를 전송
  rs485.write(pChkPnt, 9);
  memcpy((char*)&stSendMsg,pChkPnt,9);

// 전송한 9byte 로그 쓰기
  Serial.println("Write MRD Serial data: ");
  Display_DebugMessage(0x01,pChkPnt,9);
// rs485 비우기
//  rs485.flush();
  return(true);
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
bool  MRD_SendDataP(char cmd0,char cmd1,char data0,char data1,char data2)
{
  Protocol_MRD  stSendData;

  stSendData.STX = 0x7C;
  stSendData.ZoneID = iZoneID;
  stSendData.DeviceNumber = iDeviceNumber;
  stSendData.Cmd0 = cmd0;
  stSendData.Cmd1 = cmd1;
  stSendData.Data[0] = data0;
  stSendData.Data[1] = data1;
  stSendData.Data[2] = data2;
  stSendData.CheckData = cal_checksum((byte*)&stSendData,8);

  MRD_SendData((byte*)&stSendData);
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
unsigned char* MRD_ReceiveData(int LimitTime)
{
  static unsigned char szBuffer[128],CheckSum;
  unsigned long timepoint = millis();
  int     iCount = 0;

//  Serial.println("Receive MRD Serial Start !!! ");
  timepoint = millis();
  memset((char*)&stRecvMsg,0x00,sizeof(Protocol_MRD));  // 버퍼초기화
  while ((millis()-timepoint) < (LimitTime) && iCount < 9) {    // 주어진 임계치 시간보다 작다면 반복문 수행.
    if ((rs485.available() > 0) && (iCount < 9)) {
      szBuffer[iCount] = rs485.read();
// 0 번째 코드가 0x7C 가 아닐경우에는 Count 값을 증가시키지 않는다.  즉 들어오는값이 무시된다.
      if (iCount==0x00) {
        if (szBuffer[0]==0x7C) iCount++;
      } else iCount++;
    }
  }
  Serial.println("Receive MRD Serial data: ");
  Display_DebugMessage(0x00,szBuffer,iCount);
  memcpy((char*)&stRecvMsg,szBuffer,iCount);
  if (iCount==9) {
    CheckSum = cal_checksum(szBuffer,8);
    if (szBuffer[8]==CheckSum) {
      return(szBuffer);
    }
  }
  return(NULL);
}

//---------------------------------------------------------------------------
// pSendData : MRD 프로토콜의 9바이트 데이터가 적재되어 있어야 한다.
// pRecvData : 버퍼는 할당되어 있어야하며, 수신된 데이터가 들어간다.
// 리턴값 : 0x00(정상) , 이외에는 에러코드값이 들어간다.
//    0x01 : ZoneID,DeviceID 가 일치하지 않는다.
//    0x02 : 밸브 내부 습도 높음
//    0x03 : 밸브 통신 불가능
//    0x10 : 수신데이터 없음.
//---------------------------------------------------------------------------
int  MRD_Send_Receive(unsigned char *pSendData,unsigned char *pRecvvData,int iLimitTime=10000)
{
  int   iRetryCount = 0;
  char  szBuffer[128];
  Protocol_MRD  *pChkPnt=NULL;

  do {
    MRD_SendData(pSendData);
// 벨브 작동이나 스케쥴 변경 시간(35초) 대기
    if ((pSendData[3]==0xA3 && pSendData[4]==0xC7) || (pSendData[3]==0xA8 && pSendData[4]==0xC7)) delay(35000);
    delay(100);
    pChkPnt = (Protocol_MRD*)MRD_ReceiveData(iLimitTime);      // 기본 10,000 ms 대기
    if (pChkPnt != NULL) {
      if (pChkPnt->Cmd0==0xB1 && (pChkPnt->Cmd1==0xE0 || pChkPnt->Cmd1==0xE1)) {    // 알람코드일경우 재수신여부 확인
        if (rs485.available() > 0) {    // 추가적인 데이터가 남아있다면
          pChkPnt = (Protocol_MRD*)MRD_ReceiveData(iLimitTime);      // 기본 10,000 ms 대기
        }
      }
    }
    iRetryCount++;
  }while(pChkPnt==NULL && iRetryCount < 5);
  if (pChkPnt != NULL) {    
    memcpy(pRecvvData,pChkPnt,0x09);
// 에러코드 확인하여 리턴값 변경.
    if (pChkPnt->Cmd0==0xE1) {
      sprintf(szBuffer,"MRD ErrorCode = %02X",pChkPnt->Cmd1);
      MRD_Exception_to_Server((byte*)&stSendMsg,(byte*)&stRecvMsg,0x02,szBuffer,NULL,NULL);
      return(pChkPnt->Cmd1);
    }
  } else {    // pChkPnt == NULL 이라면 응답없음으로 봐야함.
    if (iRetryCount==5) {
      MRD_Exception_to_Server((byte*)&stSendMsg,(byte*)&stRecvMsg,0x02,"MRD Protocol Not Response",NULL,NULL);
    }
    return(0x10);
  }
  return(0x00);
}

//---------------------------------------------------------------------------
//  RS485 통신 관련 함수
// RS485 통신으로 배터리 값을 요청 후 받아오는 함수
//---------------------------------------------------------------------------
void Battery_Read(void) {
  //rs485.begin(9600, SERIAL_8N1, RXD2, TXD2);// 기존에는 작동했지만 어느 순간부터 RS485 begin 함수가 2번 나오면 노딕보드가 멈추는 현상이 나타남.
  Protocol_MRD  stSendData,stRecvData,*pChkPnt=NULL;
  int     iChkRet,iCount;

  stSendData.STX = 0x7C;
  stSendData.ZoneID = iZoneID;
  stSendData.DeviceNumber = iDeviceNumber;
  stSendData.Cmd0 = MRD_BATTERY_STATUS;
  stSendData.Cmd1 = 0xE0;
  stSendData.Data[0] = 0x0D;
  stSendData.Data[1] = 0x0D;
  stSendData.Data[2] = 0x0D;
  stSendData.CheckData = cal_checksum((byte*)&stSendData,8);

  volt = 0;
  iCount = 0;
  do {
    iChkRet = MRD_Send_Receive((byte*)&stSendData,(byte*)&stRecvData);
    if (iChkRet==0x00) {
      pChkPnt = (Protocol_MRD*)&stRecvData;
      memcpy(getbatterydata,(byte*)&stRecvData,0x09);
      battery_value[0] = pChkPnt->Data[0];
      battery_value[1] = pChkPnt->Data[1];
      battery_value[2] = pChkPnt->Data[2];
      if (pChkPnt->Cmd0==0xA4 && (pChkPnt->Cmd1==0xE0 || pChkPnt->Cmd1==0x01 || pChkPnt->Cmd1==0x00) ) {
        getbatterydata[4]=0xE0;
        Serial.println( "Battery_Read Success " );
        volt = Calculate_Volt(getbatterydata[5],getbatterydata[6],getbatterydata[7]);  
        break;
      }
    } else {
      Serial.print( "Battery_Read Error Code = " );
      Serial.println( iChkRet );
      break;
    }
  }while(++iCount < 5);   // iChkRet==0x00(데이터는 수신) 이면서 정상적인 값이 아닐경우 총 5회 반복수행.

  if (iChkRet==0x10) {                  // 무응답일 경우
    memset(getbatterydata,0x00,0x09);   // 데이터 초기화
    getbatterydata[3] = MRD_BATTERY_STATUS; // 예시: 0x01
    getbatterydata[4] = 0XE0; // 예시: 0x02
  }
  rs485.flush();
}


//---------------------------------------------------------------------------
// RS485 통신으로 밸브 값을 요청 후 받아오는 함수
//---------------------------------------------------------------------------
void Valve_Read() 
{
  Protocol_MRD  stSendData,stRecvData,*pChkPnt=NULL;
  int     iCount,iChkRet;

  stSendData.STX = 0x7C;
  stSendData.ZoneID = iZoneID;
  stSendData.DeviceNumber = iDeviceNumber;
  stSendData.Cmd0 = MRD_VALVE_STATUS;
  stSendData.Cmd1 = 0xE0;
  stSendData.Data[0] = 0x0D;
  stSendData.Data[1] = 0x0D;
  stSendData.Data[2] = 0x0D;
  stSendData.CheckData = cal_checksum((byte*)&stSendData,8);
//  MRD_SendData((byte*)&stSendData);

  iCount = 0;
  do {
    iChkRet = MRD_Send_Receive((byte*)&stSendData,(byte*)&stRecvData);
    if (iChkRet==0x00) {
      pChkPnt = (Protocol_MRD*)&stRecvData;
      memcpy(getvalvedata,(byte*)&stRecvData,0x09);
      if (pChkPnt->Cmd0==0xA3 && (pChkPnt->Cmd1==0xE0 || pChkPnt->Cmd1==0x01 || pChkPnt->Cmd1==0x00) ) {
        if (pChkPnt->Data[0]==0xb0 || pChkPnt->Data[0]==0xb1) {
          valve = pChkPnt->Data[0]==0xb0 ? 0x00 : 0x01;
          Serial.println( "Valve_Read Success " );
        }
      }
    } else {
      Serial.print( "Valve_Read Error Code = " );
      Serial.println( iChkRet );
      break;
    }
  }while(++iCount < 5);   // iChkRet==0x00(데이터는 수신) 이면서 정상적인 값이 아닐경우 총 5회 반복수행.

  if (iChkRet==0x10) {                 // 무응답일 경우
    memset(getvalvedata,0x00,0x09);   // 데이터 초기화
  }
  rs485.flush();
}
