#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

// PN532
#include <SPI.h>
#include <Adafruit_PN532.h>

// For OLED display
#include <U8g2lib.h>
// For I2C
#include <Wire.h>

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

const int pinButton = 0;
const int i2cDisplayAddress = 0x3c;

// If using the breakout with SPI, define the pins for SPI communication.
#define PN532_SCK  (14)
#define PN532_MOSI (13)
#define PN532_SS   (15)
#define PN532_MISO (12)

Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);

// For a http://www. url:
char * url = "anavi.technology/";
uint8_t ndefprefix = NDEF_URIPREFIX_HTTP_WWWDOT;

bool isSensorAvailable(int sensorAddress)
{
    // Check if I2C sensor is present
    Wire.beginTransmission(sensorAddress);
    return 0 == Wire.endTransmission();
}

void checkDisplay()
{
    Serial.print("Mini I2C OLED Display at address ");
    Serial.print(i2cDisplayAddress, HEX);
    if (isSensorAvailable(i2cDisplayAddress))
    {
        Serial.println(": OK");
    }
    else
    {
        Serial.println(": N/A");
    }
}

void drawDisplay(const char *line1, const char *line2 = "", const char *line3 = "")
{
    // Write on OLED display
    // Clear the internal memory
    u8g2.clearBuffer();
    // Set appropriate font
    u8g2.setFont(u8g2_font_ncenR14_tr);
    u8g2.drawStr(0,14, line1);
    u8g2.setFont(u8g2_font_ncenR10_tr);
    u8g2.drawStr(0,39, line2);
    u8g2.drawStr(0,60, line3);
    // Transfer internal memory to the display
    u8g2.sendBuffer();
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();

  Wire.begin();
  checkDisplay();

  u8g2.begin();

  delay(10);

  drawDisplay("ANAVI", "Tag Manager", "NFC / RFID");

  Serial.println("==== ANAVI Tag Manager ====");

  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata) {
    Serial.print("Didn't find PN53x board");
    while (1); // halt
  }
  // Got ok data, print it out!
  Serial.print("Found chip PN5"); Serial.println((versiondata>>24) & 0xFF, HEX); 
  Serial.print("Firmware ver. "); Serial.print((versiondata>>16) & 0xFF, DEC); 
  Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);
  
  // configure board to read RFID tags
  nfc.SAMConfig();

}

void loop() {
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
  uint8_t uidLength;                        // Length of the UID (4 or 7 bytes depending on ISO14443A card type)
  uint8_t dataLength;

  // Require some user feedback before running this example!
  Serial.println("\r\nPlace your NDEF formatted NTAG2xx tag on the reader to update the");
  Serial.println("NDEF record and press any key to continue ...\r\n");
  // Wait for user input before proceeding
  while (!Serial.available());
  // a key was pressed1
  while (Serial.available()) Serial.read();

  // 1.) Wait for an NTAG203 card.  When one is found 'uid' will be populated with
  // the UID, and uidLength will indicate the size of the UID (normally 7)
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);
  
  // It seems we found a valid ISO14443A Tag!
  if (success) 
  {
    // 2.) Display some basic information about the card
    Serial.println("Found an ISO14443A card");
    Serial.print("  UID Length: ");Serial.print(uidLength, DEC);Serial.println(" bytes");
    Serial.print("  UID Value: ");
    nfc.PrintHex(uid, uidLength);
    Serial.println("");
    
    if (uidLength != 7)
    {
      Serial.println("This doesn't seem to be an NTAG203 tag (UUID length != 7 bytes)!");
    }
    else
    {
      uint8_t data[32];
      
      // We probably have an NTAG2xx card (though it could be Ultralight as well)
      Serial.println("Seems to be an NTAG2xx tag (7 byte UID)");    
      
      // NTAG2x3 cards have 39*4 bytes of user pages (156 user bytes),
      // starting at page 4 ... larger cards just add pages to the end of
      // this range:
      
      // See: http://www.nxp.com/documents/short_data_sheet/NTAG203_SDS.pdf

      // TAG Type       PAGES   USER START    USER STOP
      // --------       -----   ----------    ---------
      // NTAG 203       42      4             39
      // NTAG 213       45      4             39
      // NTAG 215       135     4             129
      // NTAG 216       231     4             225      


      // 3.) Check if the NDEF Capability Container (CC) bits are already set
      // in OTP memory (page 3)
      memset(data, 0, 4);
      success = nfc.ntag2xx_ReadPage(3, data);
      if (!success)
      {
        Serial.println("Unable to read the Capability Container (page 3)");
        return;
      }
      else
      {
        // If the tag has already been formatted as NDEF, byte 0 should be:
        // Byte 0 = Magic Number (0xE1)
        // Byte 1 = NDEF Version (Should be 0x10)
        // Byte 2 = Data Area Size (value * 8 bytes)
        // Byte 3 = Read/Write Access (0x00 for full read and write)
        if (!((data[0] == 0xE1) && (data[1] == 0x10)))
        {
          Serial.println("This doesn't seem to be an NDEF formatted tag.");
          Serial.println("Page 3 should start with 0xE1 0x10.");
        }
        else
        {
          // 4.) Determine and display the data area size
          dataLength = data[2]*8;
          Serial.print("Tag is NDEF formatted. Data area size = ");
          Serial.print(dataLength);
          Serial.println(" bytes");
          
          // 5.) Erase the old data area
          Serial.print("Erasing previous data area ");
          for (uint8_t i = 4; i < (dataLength/4)+4; i++) 
          {
            memset(data, 0, 4);
            success = nfc.ntag2xx_WritePage(i, data);
            Serial.print(".");
            if (!success)
            {
              Serial.println(" ERROR!");
              return;
            }
          }
          Serial.println(" DONE!");
          
          // 6.) Try to add a new NDEF URI record
          Serial.print("Writing URI as NDEF Record ... ");
          success = nfc.ntag2xx_WriteNDEFURI(ndefprefix, url, dataLength);
          if (success)
          {
            Serial.println("DONE!");
          }
          else
          {
            Serial.println("ERROR! (URI length?)");
          }
                    
        } // CC contents NDEF record check
      } // CC page read check
    } // UUID length check
    
    // Wait a bit before trying again
    Serial.flush();
    while (!Serial.available());
    while (Serial.available()) {
    Serial.read();
    }
    Serial.flush();    
  } // Start waiting for a new ISO14443A tag
}
