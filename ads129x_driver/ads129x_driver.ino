/*
   Text-mode driver for ADS129x
   for Arduino Due

   Copyright (c) 2013-2019 by Adam Feuer <adam@adamfeuer.com>
   Copyright (c) 2012 by Chris Rorden

   This library is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this library.  If not, see <http://www.gnu.org/licenses/>.

*/



#include <SPI.h>
#include <stdlib.h>
#include <ArduinoJson.h>
#include "adsCommand.h"
#include "ads129x.h"
#include "SerialCommand.h"
#include "JsonCommand.h"
#include "Base64.h"
#include "SpiDma.h"

#define BAUD_RATE  115200     // WiredSerial ignores this and uses the maximum rate
#define txActiveChannelsOnly  // reduce bandwidth: only send data for active data channels
#define WiredSerial SerialUSB // use Due's Native USB port

#define NOP_COMMAND 0
#define VERSION_COMMAND 1
#define STATUS_COMMAND 2
#define SERIALNUMBER_COMMAND 3
#define TEXT_COMMAND 4
#define JSONLINES_COMMAND 5
#define MESSAGEPACK_COMMAND 6
#define LEDON_COMMAND 7
#define LEDOFF_COMMAND 8
#define BOARDLEDOFF_COMMAND 9
#define BOARDLEDON_COMMAND 10
#define WAKEUP_COMMAND 11
#define STANDBY_COMMAND 12
#define RESET_COMMAND 13
#define START_COMMAND 14
#define STOP_COMMAND 15
#define RDATAC_COMMAND 16
#define SDATAC_COMMAND 17
#define GETDATA_COMMAND 18
#define RDATA_COMMAND 19
#define RREG_COMMAND 20
#define WREG_COMMAND 21
#define BASE64_COMMAND 22
#define HEX_COMMAND 23
#define MICROS_COMMAND 24
#define HELP_COMMAND 25

#define TEXT_MODE 0
#define JSONLINES_MODE 1
#define MESSAGEPACK_MODE 2

#define RESPONSE_OK 200
#define RESPONSE_BAD_REQUEST 400
#define RESPONSE_ERROR 500
#define RESPONSE_NOT_IMPLEMENTED 501

const char *STATUS_TEXT_OK = "Ok";
const char *STATUS_TEXT_BAD_REQUEST = "Bad request";
const char *STATUS_TEXT_ERROR = "Error";
const char *STATUS_TEXT_NOT_IMPLEMENTED = "Not Implemented";

int protocol_mode = TEXT_MODE;
int max_channels = 0;
int num_active_channels= 0;
boolean gActiveChan[9]; // reports whether channels 1..9 are active
boolean isRdatac = false;
boolean base64Mode = true;

char hexDigits[] = "0123456789ABCDEF";
uint8_t serialBytes[200];
char sampleBuffer[1000];

const char *hardware_type = "unknown";
const char *board_name = "HackEEG";
const char *maker_name = "Starcat LLC";
const char *driver_version = "v0.3.0";

SerialCommand serialCommand;
JsonCommand jsonCommand;

void arduinoSetup();
void adsSetup();
void detect_active_channels();
void unrecognized(const char*);
void nop_command(unsigned char unused1, unsigned char unused2);
void version_command(unsigned char unused1, unsigned char unused2);
void status_command(unsigned char unused1, unsigned char unused2);
void serial_number_command(unsigned char unused1, unsigned char unused2);
void text_command(unsigned char unused1, unsigned char unused2);
void jsonlines_command(unsigned char unused1, unsigned char unused2);
void messagepack_command(unsigned char unused1, unsigned char unused2);
void led_on_command(unsigned char unused1, unsigned char unused2);
void led_off_command(unsigned char unused1, unsigned char unused2);
void board_led_off_command(unsigned char unused1, unsigned char unused2);
void board_led_on_command(unsigned char unused1, unsigned char unused2);
void wakeup_command(unsigned char unused1, unsigned char unused2);
void standby_command(unsigned char unused1, unsigned char unused2);
void reset_command(unsigned char unused1, unsigned char unused2);
void start_command(unsigned char unused1, unsigned char unused2);
void stop_command(unsigned char unused1, unsigned char unused2);
void rdatac_command(unsigned char unused1, unsigned char unused2);
void sdatac_command(unsigned char unused1, unsigned char unused2);
void getdata_command(unsigned char unused1, unsigned char unused2);
void rdata_command(unsigned char unused1, unsigned char unused2);
void base64_mode_on(unsigned char unused1, unsigned char unused2);
void hex_mode_on_command(unsigned char unused1, unsigned char unused2);
void help_command(unsigned char unused1, unsigned char unused2);
void read_register_command(unsigned char unused1, unsigned char unused2);
void write_register_command(unsigned char unused1, unsigned char unused2);
void read_register_command_direct(unsigned char register_number);
void write_register_command_direct(unsigned char register_number, unsigned char register_value);


void setup() {
  WiredSerial.begin(BAUD_RATE);
  pinMode(PIN_LED, OUTPUT);     // Configure the onboard LED for output
  digitalWrite(PIN_LED, LOW);   // default to LED off

  protocol_mode = TEXT_MODE;
  arduinoSetup();
  adsSetup();

  // Setup callbacks for SerialCommand commands
  serialCommand.addCommand("nop", nop_command);                     // No operation (does nothing)
  serialCommand.addCommand("micros", micros_command);               // Returns number of microseconds since the program began executing
  serialCommand.addCommand("version", version_command);             // Echos the driver version number
  serialCommand.addCommand("status", status_command);               // Echos the driver status
  serialCommand.addCommand("serialnumber", serial_number_command);  // Echos the board serial number (UUID from the onboard 24AA256UID-I/SN I2S EEPROM)
  serialCommand.addCommand("text", text_command);                   // Sets the communication protocol to text 
  serialCommand.addCommand("jsonlines", jsonlines_command);         // Sets the communication protocol to JSONLines 
  serialCommand.addCommand("messagepack", messagepack_command);     // Sets the communication protocol to MessagePack
  serialCommand.addCommand("ledon", led_on_command);                // Turns Arduino Due onboard LED on
  serialCommand.addCommand("ledoff", led_off_command);              // Turns Arduino Due onboard LED off
  serialCommand.addCommand("boardledoff", board_led_off_command);   // Turns HackEEG ADS1299 GPIO4 LED off
  serialCommand.addCommand("boardledon", board_led_on_command);     // Turns HackEEG ADS1299 GPIO4 LED on
  serialCommand.addCommand("wakeup", wakeup_command);               // Send the WAKEUP command 
  serialCommand.addCommand("standby", standby_command);             // Send the STANDBY command
  serialCommand.addCommand("reset", reset_command);                 // Reset the ADS1299
  serialCommand.addCommand("start", start_command);                 // Send START command
  serialCommand.addCommand("stop", stop_command);                   // Send STOP command
  serialCommand.addCommand("rdatac", rdatac_command);               // Enter read data continuous mode, clear the ringbuffer, and read new data into the ringbuffer
  serialCommand.addCommand("sdatac", sdatac_command);               // Stop read data continuous mode; ringbuffer data is still available
  serialCommand.addCommand("getdata", getdata_command);             // Get data from the ringbuffer 
  serialCommand.addCommand("rdata", rdata_command);                 // Read one sample of data from each active channel
  serialCommand.addCommand("rreg", read_register_command);          // Read ADS129x register, argument in hex, print contents in hex
  serialCommand.addCommand("wreg", write_register_command);         // Write ADS129x register, arguments in hex
  serialCommand.addCommand("base64", base64_mode_on_command);         // RDATA commands send base64 encoded data - default
  serialCommand.addCommand("hex", hex_mode_on_command);               // RDATA commands send hex encoded data
  serialCommand.addCommand("help", help_command);                   // Print list of commands
  serialCommand.setDefaultHandler(unrecognized);                    // Handler for any command that isn't matched

  jsonCommand.addCommand("nop", nop_command);                       // No operation (does nothing)
  jsonCommand.addCommand("micros", micros_command);                 // Returns number of microseconds since the program began executing
  jsonCommand.addCommand("ledon", led_on_command);                  // Turns Arduino Due onboard LED on
  jsonCommand.addCommand("ledoff", led_off_command);                // Turns Arduino Due onboard LED off
  jsonCommand.addCommand("boardledoff", board_led_off_command);     // Turns HackEEG ADS1299 GPIO4 LED off
  jsonCommand.addCommand("boardledon", board_led_on_command);       // Turns HackEEG ADS1299 GPIO4 LED on
  jsonCommand.addCommand("status", status_command);                 // Returns the driver status
  jsonCommand.addCommand("serialnumber", serial_number_command);    // Returns the board serial number (UUID from the onboard 24AA256UID-I/SN I2S EEPROM)
  jsonCommand.addCommand("jsonlines", jsonlines_command);           // Sets the communication protocol to JSONLines 
  jsonCommand.addCommand("messagepack", messagepack_command);       // Sets the communication protocol to MessagePack
//  jsonCommand.addCommand("rreg", read_register_command_direct);     // Read ADS129x register
//  jsonCommand.addCommand("wreg", write_register_command_direct);    // Write ADS129x register

  WiredSerial.println("Ready");
}

void loop() {
  switch (protocol_mode) {
    case TEXT_MODE: 
      serialCommand.readSerial();
      break;
    case JSONLINES_MODE:
      jsonCommand.readSerial();
      break;
    case MESSAGEPACK_MODE:
      // TODO: not implemented
      break;
    default:
      // do nothing 
      ;
  }
}

long hexToLong(char *digits) {
  using namespace std;
  char *error;
  long n = strtol(digits, &error, 16);
  if ( *error != 0 ) {
    return -1; // error
  }
  else {
    return n;
  }
}

void outputHexByte(int value) {
  int clipped = value & 0xff;
  char charValue[3];
  sprintf(charValue, "%02X", clipped);
  WiredSerial.print(charValue);
}

void encodeHex(char* output, char* input, int inputLen) {
  register int count = 0;
  for (register int i = 0; i < inputLen; i++) {
    register uint8_t lowNybble = input[i] & 0x0f;
    register uint8_t highNybble = input[i] >> 4;
    output[count++] = hexDigits[highNybble];
    output[count++] = hexDigits[lowNybble];
  }
  output[count] = 0;
}

void send_response_ok() {
  send_response(RESPONSE_OK, STATUS_TEXT_OK);
}

void send_response_error() {
  send_response(RESPONSE_ERROR, STATUS_TEXT_ERROR);
}

void send_response(int status_code, const char *status_text) {
  switch (protocol_mode) {
    case TEXT_MODE: 
      char response[128];
      sprintf(response, "%d %s", status_code, status_text);  
      WiredSerial.println(response);
      break;
    case JSONLINES_MODE:
      jsonCommand.send_jsonlines_response(status_code, (char *)status_text);
      break;
    case MESSAGEPACK_MODE:
      // TODO: not implemented yet 
      break;
    default:
      // unknown protocol
      ;
  }
}

void send_jsonlines_data(int status_code, char data, char *status_text) {
  StaticJsonDocument<1024> doc;
  JsonObject root = doc.to<JsonObject>();
  root[STATUS_CODE_KEY] = status_code;
  root[STATUS_TEXT_KEY] = status_text;
  root[DATA_KEY] = data;
  serializeJson(doc, SerialUSB);
  SerialUSB.println();
  doc.clear();
}

void version_command(unsigned char unused1, unsigned char unused2) {
  send_response(RESPONSE_OK, driver_version);
}

void status_command(unsigned char unused1, unsigned char unused2) {
  detect_active_channels();
  if (protocol_mode == TEXT_MODE) {
      WiredSerial.println("200 Ok");
      WiredSerial.print("Driver version: ");
      WiredSerial.println(driver_version);
      WiredSerial.print("Board name: ");
      WiredSerial.println(board_name);
      WiredSerial.print("Board maker: ");
      WiredSerial.println(maker_name);
      WiredSerial.print("Hardware type: ");
      WiredSerial.println(hardware_type);
      WiredSerial.print("Max channels: ");
      WiredSerial.println(max_channels);
      WiredSerial.print("Number of active channels: ");
      WiredSerial.println(num_active_channels);
      WiredSerial.println();
      return;
  }
  StaticJsonDocument<1024> doc;
  JsonObject root = doc.to<JsonObject>();
  root[STATUS_CODE_KEY] = STATUS_OK; 
  root[STATUS_TEXT_KEY] = STATUS_TEXT_OK;
  JsonObject status_info = root.createNestedObject(DATA_KEY);
  status_info["driver_version"] = driver_version;
  status_info["board_name"] = board_name;
  status_info["maker_name"] = maker_name;
  status_info["hardware_type"] = hardware_type;
  status_info["max_channels"] = max_channels;
  status_info["active_channels"] = num_active_channels;
  switch (protocol_mode) {
    case JSONLINES_MODE:
      jsonCommand.send_jsonlines_doc_response(doc);
      break;
    case MESSAGEPACK_MODE:
      // TODO: not implemented yet 
      break;
    default:
      // unknown protocol
      ;
  }
}

void nop_command(unsigned char unused1, unsigned char unused2) {
  send_response_ok();
}

void micros_command(unsigned char unused1, unsigned char unused2) {
  unsigned long microseconds = micros();
  if (protocol_mode == TEXT_MODE) {
      WiredSerial.println("200 Ok");
      WiredSerial.println(microseconds);
      return;
  }
  StaticJsonDocument<1024> doc;
  JsonObject root = doc.to<JsonObject>();
  root[STATUS_CODE_KEY] = STATUS_OK; 
  root[STATUS_TEXT_KEY] = STATUS_TEXT_OK;
  root[DATA_KEY] = microseconds;
  switch (protocol_mode) {
    case JSONLINES_MODE:
      jsonCommand.send_jsonlines_doc_response(doc);
      break;
    case MESSAGEPACK_MODE:
      // TODO: not implemented yet 
      break;
    default:
      // unknown protocol
      ;
  }
}

void serial_number_command(unsigned char unused1, unsigned char unused2) {
  send_response(RESPONSE_NOT_IMPLEMENTED, STATUS_TEXT_NOT_IMPLEMENTED);
}

void text_command(unsigned char unused1, unsigned char unused2) {
  protocol_mode = TEXT_MODE;
  send_response_ok();
}

void jsonlines_command(unsigned char unused1, unsigned char unused2) {
  protocol_mode = JSONLINES_MODE;
  send_response_ok();
}

void messagepack_command(unsigned char unused1, unsigned char unused2) {
  protocol_mode = MESSAGEPACK_MODE;
  send_response_ok();
}

void getdata_command(unsigned char unused1, unsigned char unused2) {
  WiredSerial.println("200 Ok");
  WiredSerial.println("Not implemented yet. ");
  WiredSerial.println();
}

void led_on_command(unsigned char unused1, unsigned char unused2) {
  digitalWrite(PIN_LED, HIGH);
  send_response_ok();
}

void led_off_command(unsigned char unused1, unsigned char unused2) {
  digitalWrite(PIN_LED, LOW);
  send_response_ok();
}

void board_led_on_command(unsigned char unused1, unsigned char unused2) {
  int state = adc_rreg(ADS129x::GPIO);
  state = state & 0xF7;
  state = state | 0x80;
  adc_wreg(ADS129x::GPIO, state);
  send_response_ok();
}

void board_led_off_command(unsigned char unused1, unsigned char unused2) {
  int state = adc_rreg(ADS129x::GPIO);
  state = state & 0x77;
  adc_wreg(ADS129x::GPIO, state);
  send_response_ok();
}

void base64_mode_on_command(unsigned char unused1, unsigned char unused2) {
  base64Mode = true;
  WiredSerial.println("200 Ok");
  WiredSerial.println("Base64 mode on - rdata command will respond with base64 encoded data.");
  WiredSerial.println();
}

void hex_mode_on_command(unsigned char unused1, unsigned char unused2) {
  base64Mode = false;
  WiredSerial.println("200 Ok");
  WiredSerial.println("Hex mode on - rdata command will respond with hex encoded data");
  WiredSerial.println();
}

void help_command(unsigned char unused1, unsigned char unused2) {
  WiredSerial.println("200 Ok");
  WiredSerial.println("Available commands: ");
  serialCommand.printCommands();
  WiredSerial.println();
}

void read_register_command(unsigned char unused1, unsigned char unused2) {
  using namespace ADS129x;
  char *arg1;
  arg1 = serialCommand.next();
  if (arg1 != NULL) {
    long registerNumber = hexToLong(arg1);
    if (registerNumber >= 0) {
      int result = adc_rreg(registerNumber);
      WiredSerial.print("200 Ok");
      WiredSerial.print(" (Read Register ");
      outputHexByte(registerNumber);
      WiredSerial.print(") ");
      WiredSerial.println();
      outputHexByte(result);
      WiredSerial.println();
    }
    else {
      WiredSerial.println("402 Error: expected hexidecimal digits.");
    }
  }
  else {
    WiredSerial.println("403 Error: register argument missing.");
  }
  WiredSerial.println();
}

void read_register_command_direct(unsigned char register_number) {
  using namespace ADS129x;
  if (register_number >= 0) {
    unsigned char result = adc_rreg(register_number);
    StaticJsonDocument<1024> doc;
    JsonObject root = doc.to<JsonObject>();
    root[STATUS_CODE_KEY] = STATUS_OK; 
    root[STATUS_TEXT_KEY] = STATUS_TEXT_OK;
    root[DATA_KEY] = result;
    jsonCommand.send_jsonlines_doc_response(doc);
  } else {
    send_response_error();
  }
}

void write_register_command(unsigned char unused1, unsigned char unused2) {
  char *arg1, *arg2;
  arg1 = serialCommand.next();
  arg2 = serialCommand.next();
  if (arg1 != NULL) {
    if (arg2 != NULL) {
      long registerNumber = hexToLong(arg1);
      long registerValue = hexToLong(arg2);
      if (registerNumber >= 0 && registerValue >= 0) {
        adc_wreg(registerNumber, registerValue);
        WiredSerial.print("200 Ok");
        WiredSerial.print(" (Write Register ");
        outputHexByte(registerNumber);
        WiredSerial.print(" ");
        outputHexByte(registerValue);
        WiredSerial.print(") ");
        WiredSerial.println();
      }
      else {
        WiredSerial.println("402 Error: expected hexidecimal digits.");
      }
    }
    else {
      WiredSerial.println("404 Error: value argument missing.");
    }
  }
  else {
    WiredSerial.println("403 Error: register argument missing.");
  }
  WiredSerial.println();
}


void write_register_command_direct(unsigned char register_number, unsigned char register_value) {
  if (register_number >= 0 && register_value >= 0) {
    adc_wreg(register_number, register_value);
	  send_response_ok();
  } else {
    send_response_error();
  }
}

void wakeup_command(unsigned char unused1, unsigned char unused2) {
  using namespace ADS129x;
  adc_send_command(WAKEUP);
  WiredSerial.println("200 Ok ");
  WiredSerial.println("Wakeup command sent.");
  WiredSerial.println();
}

void standby_command(unsigned char unused1, unsigned char unused2) {
  using namespace ADS129x;
  adc_send_command(STANDBY);
  WiredSerial.println("200 Ok ");
  WiredSerial.println("Standby command sent.");
  WiredSerial.println();
}

void reset_command(unsigned char unused1, unsigned char unused2) {
  using namespace ADS129x;
  adc_send_command(RESET);
  WiredSerial.println("200 Ok ");
  WiredSerial.println("Reset command sent.");
  WiredSerial.println();
}

void start_command(unsigned char unused1, unsigned char unused2) {
  using namespace ADS129x;
  adc_send_command(START);
  WiredSerial.println("200 Ok ");
  WiredSerial.println("Start command sent.");
  WiredSerial.println();
}

void stop_command(unsigned char unused1, unsigned char unused2) {
  using namespace ADS129x;
  adc_send_command(STOP);
  WiredSerial.println("200 Ok ");
  WiredSerial.println("Stop command sent.");
  WiredSerial.println();
}

void rdata_command(unsigned char unused1, unsigned char unused2) {
  using namespace ADS129x;
  while (digitalRead(IPIN_DRDY) == HIGH);
  adc_send_command_leave_cs_active(RDATA);
  WiredSerial.println("200 Ok ");
  sendSample();
  WiredSerial.println();
}

void rdatac_command(unsigned char unused1, unsigned char unused2) {
  using namespace ADS129x;
  detect_active_channels();
  if (num_active_channels > 0) {
    isRdatac = true;
    adc_send_command(RDATAC);
    WiredSerial.println("200 Ok");
    WiredSerial.println("RDATAC mode on.");
  } else {
    WiredSerial.println("405 Error: no active channels.");
  }
  WiredSerial.println();
}

void sdatac_command(unsigned char unused1, unsigned char unused2) {
  using namespace ADS129x;
  isRdatac = false;
  adc_send_command(SDATAC);
  using namespace ADS129x;
  WiredSerial.println("200 Ok");
  WiredSerial.println("RDATAC mode off.");
  WiredSerial.println();
}

// This gets set as the default handler, and gets called when no other command matches.
void unrecognized(const char *command) {
  WiredSerial.println("406 Error: Unrecognized command.");
  WiredSerial.println();
}


void detect_active_channels() {  //set device into RDATAC (continous) mode -it will stream data
  if ((isRdatac) ||  (max_channels < 1)) return; //we can not read registers when in RDATAC mode
  //Serial.println("Detect active channels: ");
  using namespace ADS129x;
  num_active_channels = 0;
  for (int i = 1; i <= max_channels; i++) {
    delayMicroseconds(1);
    int chSet = adc_rreg(CHnSET + i);
    gActiveChan[i] = ((chSet & 7) != SHORTED);
    if ( (chSet & 7) != SHORTED) num_active_channels ++;
  }
}

//#define testSignal //use this to determine if your software is accurately measuring full range 24-bit signed data -8388608..8388607
#ifdef testSignal
int testInc = 1;
int testPeriod = 100;
byte testMSB, testLSB;
#endif

inline void sendSamples(void) {
  if ((!isRdatac) || (num_active_channels < 1) )  return;
  if (digitalRead(IPIN_DRDY) == HIGH) return;
  sendSample();
}

// Use SAM3X DMA
inline void sendSample(void) {
  digitalWrite(PIN_CS, LOW);
  register int numSerialBytes = (3 * (max_channels + 1)); //24-bits header plus 24-bits per channel
  uint8_t returnCode = spiRec(serialBytes, numSerialBytes);
  digitalWrite(PIN_CS, HIGH);
  register unsigned int count = 0;
  if (base64Mode == true) {
    base64_encode(sampleBuffer, (char *)serialBytes, numSerialBytes);
  }
  else {
    encodeHex(sampleBuffer, (char *)serialBytes, numSerialBytes);
  }
  WiredSerial.println(sampleBuffer);
}

void adsSetup() { //default settings for ADS1298 and compatible chips
  using namespace ADS129x;
  // Send SDATAC Command (Stop Read Data Continuously mode)
  delay(1000); //pause to provide ads129n enough time to boot up...
  adc_send_command(SDATAC);
  // delayMicroseconds(2);
  delay(100);
  int val = adc_rreg(ID) ;
  switch (val & B00011111 ) { //least significant bits reports channels
    case  B10000: //16
      hardware_type = "ADS1294";
      max_channels = 4;
      break;
    case B10001: //17
      hardware_type = "ADS1296";
      max_channels = 6;
      break;
    case B10010: //18
      hardware_type = "ADS1298";
      max_channels = 8;
      break;
    case B11110: //30
      hardware_type = "ADS1299";
      max_channels = 8;
      break;
    default:
      max_channels = 0;
  }
  WiredSerial.println("Max channels: " + max_channels);
  if (max_channels == 0) { //error mode
    while (1) { //loop forever
      digitalWrite(PIN_LED, HIGH);   // turn the LED on (HIGH is the voltage level)
      delay(500);               // wait for a second`
      digitalWrite(PIN_LED, LOW);    // turn the LED off by making the voltage LOW
      delay(500);
    } //while forever
  } //error mode
}

void arduinoSetup() {
  pinMode(PIN_LED, OUTPUT);
  using namespace ADS129x;
  //prepare pins to be outputs or inputs
  //pinMode(PIN_SCLK, OUTPUT); //optional - SPI library will do this for us
  //pinMode(PIN_DIN, OUTPUT); //optional - SPI library will do this for us
  //pinMode(PIN_DOUT, INPUT); //optional - SPI library will do this for us
  //pinMode(PIN_CS, OUTPUT);
  pinMode(PIN_START, OUTPUT);
  pinMode(IPIN_DRDY, INPUT);
  pinMode(PIN_CLKSEL, OUTPUT);// *optional
  pinMode(IPIN_RESET, OUTPUT);// *optional
  //pinMode(IPIN_PWDN, OUTPUT);// *optional
  digitalWrite(PIN_CLKSEL, HIGH); // internal clock
  //start Serial Peripheral Interface
  spiBegin(PIN_CS);
  spiInit(MSBFIRST, SPI_MODE1, SPI_CLOCK_DIVIDER);
  //Start ADS1298
  delay(500); //wait for the ads129n to be ready - it can take a while to charge caps
  digitalWrite(PIN_CLKSEL, HIGH);// *optional
  delay(10); // wait for oscillator to wake up
  digitalWrite(IPIN_PWDN, HIGH); // *optional - turn off power down mode
  digitalWrite(IPIN_RESET, HIGH);
  delay(1000);// *optional
  digitalWrite(IPIN_RESET, LOW);
  delay(1);// *optional
  digitalWrite(IPIN_RESET, HIGH);
  delay(1);  // *optional Wait for 18 tCLKs AKA 9 microseconds, we use 1 millisecond
} 





