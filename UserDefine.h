#ifndef User_DataStruct_Define
#define User_DataStruct_Define
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
#define   ERROR_NO_MATCH_ID      0x01
#define   ERROR_VALVE_HIGH       0x02
#define   ERROR_VALVE_NO_COMM    0x03
//---------------------------------------------------------------------------
#define   MRD_BATTERY_STATUS     0xA4
#define   MRD_VALVE_STATUS       0xA3
#define   MRD_GET_DEVICE_INFO    0xA9
//---------------------------------------------------------------------------
typedef struct  _Protocol_MRD {
  unsigned char   STX;            // Start Code : 0x7C
  unsigned char   ZoneID;         // 1 ~ 250
  unsigned char   DeviceNumber;   // 1 ~ 250
  unsigned char   Cmd0;
  unsigned char   Cmd1;
  union {
    unsigned char   Data[2];
    struct {
      unsigned char M01:2;
      unsigned char M02:2;
      unsigned char M03:2;
      unsigned char M04:2;
      unsigned char M05:2;
      unsigned char M06:2;
      unsigned char M07:2;
      unsigned char M08:2;
      unsigned char M09:2;
      unsigned char M10:2;
      unsigned char M11:2;
      unsigned char M12:2;
    } Month;
  };
  unsigned char   CheckData;      // 1 ~ 8 byte Sum
} Protocol_MRD;

//---------------------------------------------------------------------------
// 하루단위로 전압과 밸브의 상태를 저장한다. 날자는 파일명으로 구분YYYY-MM-DD.DAT
//---------------------------------------------------------------------------
typedef struct  _RecordData_Status {
  unsigned char   Status_Volt[24];    // 0x00:Not Send, XX.X 로 표기(전압값 * 10)
  unsigned char   Status_Valve[24];   // 0x00:Close, 0x01:Open
} RecordData_Status;

//---------------------------------------------------------------------------
extern RecordData_Status   stServerStatus;
extern bool eth_connected;
extern byte battery_value[3], getbatterydata[9], getvalvedata[9];

//---------------------------------------------------------------------------
//extern  void WiFiEvent(WiFiEvent_t event);
extern  void  EEPROM_Set_DeviceInformation();
extern  void  LTE_OFF(); 
extern  void  Display_DebugMessage(int iMode,unsigned char *szCommand, int iSize);
extern  byte  cal_checksum(byte *ptr, unsigned int len);
extern  bool  MRD_SendData(unsigned char *pChkPnt);
extern  unsigned char* MRD_ReceiveData(int LimitTime);
extern  int  MRD_Send_Receive(unsigned char *pSendData,unsigned char *pRecvvData,int iLimitTime);
extern  void Battery_Read(void);
extern  void Valve_Read(); 
extern  void post_battery_to_server(int to_server);
extern  void post_valve_to_server(int to_server); 
extern  void get_api(); 
extern  void get_device_config(); 
extern  void MRD_DataValue_to_Server(int to_server,byte *pRecvData,char *OptionKey,char *OptionValue);
extern  void MRD_Exception_to_Server(byte *pSendData,byte *pRecvData,int iLogLevel,char *Exception,char *OptionKey,char *OptionValue);
extern  void MRD_StatusData_to_Server_byDay();
extern  void MRD_StatusData_to_Server();
extern  float Calculate_Volt(unsigned char Volt1,unsigned char Volt2,unsigned char Volt3);

//---------------------------------------------------------------------------
// SPIFFS 파일관련
//---------------------------------------------------------------------------
extern  void  RecordData_Status_byWrite(unsigned char iVolt,unsigned char iValve);
extern  int   RecordData_Status_byRead(short int iYear,short int iMonth,short int iDay);

//---------------------------------------------------------------------------
#endif
