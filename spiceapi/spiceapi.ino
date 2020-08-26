// Arduino <-> Level Converter <-> PN5180 pin mapping:
// 5V             <-->             5V
// 3.3V           <-->             3.3V
// GND            <-->             GND
// 5V      <-> HV
// GND     <-> GND (HV)
//             LV              <-> 3.3V
//             GND (LV)        <-> GND
// SCLK,13 <-> HV1 - LV1       --> SCLK
// MISO,12        <---         <-- MISO
// MOSI,11 <-> HV3 - LV3       --> MOSI
// SS,10   <-> HV4 - LV4       --> NSS (=Not SS -> active LOW)
// BUSY,9         <---             BUSY
// Reset,7 <-> HV2 - LV2       --> RST
//
/*
 * SpiceAPI Wrapper Buffer Sizes
 * 
 * They should be as big as possible to be able to create/parse
 * some of the bigger requests/responses. Due to dynamic memory
 * limitations of some weaker devices, if you set them too high
 * you will probably experience crashes/bugs/problems, one
 * example would be "Request ID is invalid" in the log.
 */
#define SPICEAPI_WRAPPER_BUFFER_SIZE 256
#define SPICEAPI_WRAPPER_BUFFER_SIZE_STR 256

/*
 * WiFi Support
 * Uncomment to enable the wireless API interface.
 */
//#define ENABLE_WIFI

/*
 * WiFi Settings
 * You can ignore these if you don't plan on using WiFi
 */
#ifdef ENABLE_WIFI
#include <ESP8266WiFi.h>
WiFiClient client;
#define SPICEAPI_INTERFACE client
#define SPICEAPI_INTERFACE_WIFICLIENT
#define SPICEAPI_INTERFACE_WIFICLIENT_HOST "192.168.178.143"
#define SPICEAPI_INTERFACE_WIFICLIENT_PORT 1337
#define WIFI_SSID "MySSID"
#define WIFI_PASS "MyWifiPassword"
#endif

/*
 * This is the interface a serial connection will use.
 * You can change this to another Serial port, e.g. with an
 * Arduino Mega you can use Serial1/Serial2/Serial3.
 */
#ifndef ENABLE_WIFI
#define SPICEAPI_INTERFACE Serial
#endif

/*
 * SpiceAPI Includes
 * 
 * If you have the JSON strings beforehands or want to craft them
 * manually, you don't have to import the wrappers at all and can
 * use Connection::request to send and receive raw JSON strings.
 */
#include "connection.h"
#include "wrappers.h"

/*
 * PN5180 Includes
 */
#include "PN5180.h"
#include "PN5180FeliCa.h"
#include "PN5180ISO15693.h"

#define PN5180_NSS  10
#define PN5180_BUSY 9
#define PN5180_RST  7


/*
 * This global object represents the API connection.
 * The first parameter is the buffer size of the JSON string
 * we're receiving. So a size of 512 will only be able to
 * hold a JSON of 512 characters maximum.
 * 
 * An empty password string means no password is being used.
 * This is the recommended when using Serial only.
 */
spiceapi::Connection CON(800, "");

void setup() {

#ifdef ENABLE_WIFI

  /*
   * When using WiFi, we can use the Serial interface for debugging.
   * You can open Serial Monitor and see what IP it gets assigned to.
   */
  Serial.begin(9600);

  // set WiFi mode to station (disables integrated AP)
  WiFi.mode(WIFI_STA);

  // now try connecting to our Router/AP
  Serial.print("Connecting");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // print debug info over serial
  Serial.print("\nLocal IP: ");
  Serial.println(WiFi.localIP());
  
#else

  /*
   * Since the API makes use of the Serial module, we need to
   * set it up using our preferred baud rate manually.
   */
  SPICEAPI_INTERFACE.begin(115200);
  //while (!SPICEAPI_INTERFACE);
  
#endif
//init PN5180


}

byte readstatus = 0;
uint8_t rdpn[8] = {0,0,0,0,0,0,0,0};
byte uid[8];
byte card = 0;
char hex_table[16] = "0123456789ABCDEF";

void loop() {

  PN5180FeliCa nfcFeliCa(PN5180_NSS, PN5180_BUSY, PN5180_RST);
  PN5180ISO15693 nfc15693(PN5180_NSS, PN5180_BUSY, PN5180_RST);
  
  nfcFeliCa.begin();
  nfcFeliCa.reset();
  
  switch (readstatus)
  {
    case 0: //look for ISO15693
    {
      nfc15693.reset();
      nfc15693.setupRF();
    
      //read ISO15693 inventory
      ISO15693ErrorCode rc = nfc15693.getInventory(rdpn);
      if (rc == ISO15693_EC_OK )
      {
        for (int i=0; i<8; i++) //fix uid as ISO15693 protocol sends data backwards
        { 
          uid[i] = rdpn[7-i];
        }

        if (uid[0] == 0xE0 && uid[1] == 0x04) // if correct konami card, job is done
        {
          card = 1; //iso15693
          //SPICEAPI_INTERFACE.println("ISO15693");
          char hex_str[16];
          for(int i = 0;i < 8;i++)
          {
            unsigned char _by = uid[i];
            hex_str[2*i] = hex_table[(_by >> 4)&0x0F];
            hex_str[2*i+1]=hex_table[_by & 0x0F];
          }
          String num;
          for(int i=0;i<16;i++){
            num += hex_str[i];
          }
          //SPICEAPI_INTERFACE.println(num);
          spiceapi::card_insert(CON, 0, num.c_str());//job done
          readstatus = 0;
        }
        else //tag found but bad ID
        {
          readstatus = 1; // try to find a FeliCa
        }
      }
      else //tag not found
      {
        readstatus = 1; // try to find a FeliCa
      }
      break; 
    }


    case 1: //look for FeliCa
    {
      nfcFeliCa.reset();
      nfcFeliCa.setupRF();
      uint8_t uidLength = nfcFeliCa.readCardSerial(rdpn);

      if (uidLength > 0) //tag found
      {
        for (int i=0; i<8; i++)
        {
          uid[i] = rdpn[i];
        }
        card = 2; //felica
        
        //SPICEAPI_INTERFACE.println("Felica");
        char hex_str[16];
          for(int i = 0;i < 8;i++)
          {
            unsigned char _by = uid[i];
            hex_str[2*i] = hex_table[(_by >> 4)&0x0F];
            hex_str[2*i+1]=hex_table[_by & 0x0F];
          }
          String num;
          for(int i=0;i<16;i++){
            num += hex_str[i];
          }
          //SPICEAPI_INTERFACE.println(num);
        spiceapi::card_insert(CON, 0, num.c_str());
        //job done
        
        readstatus = 0;
      }
      else //tag not found
      {
        card = 0;
        //we tried both protocols and found nothing, job done
        readstatus = 0;
      }
      break;
    }
  }
  // insert cards for P1/P2
  //spiceapi::card_insert(CON, 0, "E004012345678901");

}
