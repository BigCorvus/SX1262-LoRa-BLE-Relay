/*********************************************************************
  This is a basic firmware for for the LORA BLE relays
  Based on the BLE UART example by Adafruit.
  Tested on https://github.com/adafruit/Adafruit_nRF52_Arduino core 0.20.1
  Demontrates basic RX TX functionality by forwarding BLE data to LORA and vice versa, so the nRF UART app can be used to have a conversation via LORA.
  Also implements all the housekeeping such as the BQ27441 fuel gauge, ST7735 LCD and buttons.
  If using the Feather nrf52840 express bootloader the SWITCH button can be used to enter bootloader mode if pressed on startup. LED1 is the bootloader LED
  Note that you need to modify the variant.h and variant.cpp as described below in order to get software acces to certain pins. See the core documentation for more info on where to find those files.
  Also note that the PHOLD pin was separated from the rest of the circuit by cutting a trace on the PCB between the P0.05 pad on the nrf52840 module and the via next to it so the power latching circuit is not used.
  This will be corrected in an upcoming version of the design files.

*********************************************************************/
#include <bluefruit.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <SPI.h>
#include <Wire.h>
#include <RadioLib.h>
#include <SparkFunBQ27441.h>
//#include "Arduino.h"

// Set BATTERY_CAPACITY to the design capacity of your battery.
const unsigned int BATTERY_CAPACITY = 700; // e.g. 850mAh battery


unsigned int soc = 0;
/*
   the following changes need to be made:
   in variant.cpp uncomment those pins:
  // The remaining pins are not connected (per schematic)
  // The following pins were never listed as they were considered unusable
    0,      // P0.00 is XL1   (attached to 32.768kHz crystal)
    1,      // P0.01 is XL2   (attached to 32.768kHz crystal)
   18,      // P0.18 is RESET (attached to switch)
   32,      // P1.00 is SWO   (attached to debug header)

  // The remaining pins are not connected (per schematic)
   33,      //D38 is P1.01 is not connected per schematic
   35,      //D39 is P1.03 is not connected per schematic
   36,      //D40 P1.04 is not connected per schematic
   37,      //D41 P1.05 is not connected per schematic
   38,      //D42 P1.06 is not connected per schematic
   39,      //D43 P1.07 is not connected per schematic
   43,      //D44 P1.11 is not connected per schematic
   44,      //D45 is P1.12 is not connected per schematic
   45,      //D46 P1.13 is not connected per schematic
   46,      //D47 is P1.14 is not connected per schematic

   and in variant.h:
   #define PINS_COUNT           (48)
  #define NUM_DIGITAL_PINS     (48)

*/
//the pin definitons are quite confusing, so watch out
#define DIO1 (10)//P0.27
#define DIO2 (17//P0.28
#define TXEN (16)//P0.30
#define RXEN (14)//P0.04
#define L_SS (39)//P1.03 -->35? actually 39
#define BUSY (5)//P1.08
#define L_RST (45)//P1.12 -->44? actually 45

#define BTN_TO_MCU (6)//P0.07
#define PHOLD (15)//P0.05
#define BTN_UP (38)//P1.01 -->33? actually 38
#define SWITCH (7)//P1.02

#define LED2 (44)//P1.11! actually wanted to connect it to 1.10 but made a mistake on the pcb, now it's on 1.11.
#define LED1 (4)//P1.15
#define PIEZO (47)//P1.14 -->46? actually 47

#define D_RES (11)//P0.06
#define D_CS (12)//P0.08
#define BLT (13)//P1.09
#define D_RS (9)//P0.26


// BLE Service
BLEDfu  bledfu;  // OTA DFU service
BLEDis  bledis;  // device information
BLEUart bleuart; // uart over ble
BLEBas  blebas;  // battery

// SX1262 has the following connections:
// NSS pin:
// DIO1 pin:
// NRST pin:  <--? actually DIO2
// BUSY pin:

SX1262 lora = new Module(L_SS, DIO1, -1, BUSY);

// flag to indicate that a packet was received
volatile bool receivedFlag = false;
unsigned long bltTimeout = 0;

// disable interrupt when it's not needed
volatile bool enableInterrupt = true;

// For 1.44" and 1.8" TFT with ST7735 use
Adafruit_ST7735 tft = Adafruit_ST7735(D_CS,  D_RS, D_RES);

void setup()
{
  //WTF, Serial and the fuel gauge has to be initialized prior to the pins....
  //delay(1000);
  Serial.begin(115200);
  delay(5000);
  setupBQ27441();
  soc = lipo.soc();  // Read state-of-charge (%)

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(BLT, OUTPUT);

  pinMode(L_RST, OUTPUT);
  pinMode(TXEN, OUTPUT);
  pinMode(RXEN, OUTPUT);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(SWITCH, INPUT_PULLUP);
  pinMode(BTN_TO_MCU, INPUT);
  pinMode(PHOLD, INPUT); //PHOLD not used in my setup. I even cut its trace close to the nrf module and the EN pin of the LDO is pulled up ti VHI via regular spst switch

  digitalWrite(BLT, HIGH);
  digitalWrite(L_RST, LOW);
  delay(100);
  digitalWrite(L_RST, HIGH);
  prepareRX(); //switch RXEN and TXEN
  //Wire.begin();


  Serial.println("Bluefruit52 BLEUART Example");
  Serial.println("---------------------------\n");

  Serial.print(F("[SX1262] Initializing ... "));
  // initialize SX1262
  // carrier frequency:           868.0 MHz
  // bandwidth:                   125.0 kHz
  // spreading factor:            7
  // coding rate:                 5
  // sync word:                   0x1424 (private network)
  // output power:                22 dBm
  // current limit:               60 mA
  // preamble length:             8 symbols
  // CRC:                         enabled
  //  int16_t begin(float freq = 434.0, float bw = 125.0, uint8_t sf = 9, uint8_t cr = 7, uint8_t syncWord = SX126X_SYNC_WORD_PRIVATE, int8_t power = 14, float currentLimit = 60.0, uint16_t preambleLength = 8, float tcxoVoltage = 1.6, bool useRegulatorLDO = false);
  int state = lora.begin(868.0, 125.0, 7, 5, 0x1424, 22, 100, 8, 2.4, 0);

  if (state == ERR_NONE) {
    Serial.println(F("lora init success!"));
  } else {
    Serial.print(F("lora init failed, code "));
    Serial.println(state);
    //while (true);
  }

  // eByte E22-900M22S uses DIO3 to supply the external TCXO
  if (lora.setTCXO(2.4) == ERR_INVALID_TCXO_VOLTAGE)
  {
    Serial.println(F("Selected TCXO voltage is invalid for this module!"));
  }

  // set the function that will be called
  // when new packet is received
  lora.setDio1Action(setFlag);

  // start listening for LoRa packets
  Serial.print(F("[SX1262] Starting to listen ... "));
  state = lora.startReceive();
  if (state == ERR_NONE) {
    Serial.println(F("success!"));
  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
    while (true);
  }

  // if needed, 'listen' mode can be disabled by calling
  // any of the following methods:
  //
  // lora.standby()
  // lora.sleep()
  // lora.transmit();
  // lora.receive();
  // lora.readData();
  // lora.scanChannel();


  // Use this initializer (uncomment) if you're using a 0.96" 180x60 TFT
  tft.initR(INITR_18GREENTAB);   // initialize a ST7735S chip, this worked fine for my mini display, however the colors are different
  tft.setRotation(3);
  tft.setTextWrap(false);
  tft.fillScreen(ST77XX_WHITE); //actually black
  tft.setCursor(30, 30);
  tft.setTextColor(ST77XX_RED); //actually white
  tft.setTextSize(1);
  tft.println("LORA BLE Relay");
  tft.setCursor(30, 40);
  tft.print("Batt. lev.: ");
  tft.print(soc);
  tft.println("%");
  delay(2000);
  tft.fillScreen(ST77XX_WHITE); //actually black

  // Setup the BLE LED to be enabled on CONNECT
  // Note: This is actually the default behaviour, but provided
  // here in case you want to control this LED manually via PIN 19
  Bluefruit.autoConnLed(true);

  // Config the peripheral connection with maximum bandwidth
  // more SRAM required by SoftDevice
  // Note: All config***() function must be called before begin()
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);

  Bluefruit.begin();
  Bluefruit.setTxPower(4);    // Check bluefruit.h for supported values
  Bluefruit.setName("LORA BLE Relay");
  //Bluefruit.setName(getMcuUniqueID()); // useful testing with multiple central connections
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  // To be consistent OTA DFU should be added first if it exists
  bledfu.begin();

  // Configure and Start Device Information Service
  bledis.setManufacturer("Adafruit Industries");
  bledis.setModel("Bluefruit Feather52");
  bledis.begin();

  // Configure and Start BLE Uart Service
  bleuart.begin();

  // Start BLE Battery Service
  blebas.begin();
  blebas.write(100);

  // Set up and start advertising
  startAdv();

  Serial.println("Please use Adafruit's Bluefruit LE app to connect in UART mode");
  Serial.println("Once connected, enter character(s) that you wish to send");
  // delay(2000);
  // digitalWrite(PHOLD,LOW);
  digitalWrite(BLT, LOW);


  attachInterrupt(BTN_UP, up_callback, ISR_DEFERRED | FALLING);
  attachInterrupt(SWITCH, sw_callback, ISR_DEFERRED | FALLING);
  attachInterrupt(BTN_TO_MCU, ok_callback, ISR_DEFERRED | RISING);
}

void startAdv(void)
{
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();

  // Include bleuart 128-bit uuid
  Bluefruit.Advertising.addService(bleuart);

  // Secondary Scan Response packet (optional)
  // Since there is no room for 'Name' in Advertising packet
  Bluefruit.ScanResponse.addName();

  /* Start Advertising
     - Enable auto advertising if disconnected
     - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
     - Timeout for fast mode is 30 seconds
     - Start(timeout) with timeout = 0 will advertise forever (until connected)

     For recommended advertising interval
     https://developer.apple.com/library/content/qa/qa1931/_index.html
  */
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);    // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds

  //printBatteryStats();
}


void setupBQ27441(void)
{
  // Use lipo.begin() to initialize the BQ27441-G1A and confirm that it's
  // connected and communicating.
  if (!lipo.begin()) // begin() will return true if communication is successful
  {
    // If communication fails, print an error message and loop forever.
    Serial.println("Error: Unable to communicate with BQ27441.");
    Serial.println("  Check wiring and try again.");
    Serial.println("  (Battery must be plugged into Battery Babysitter!)");
    //while (1) ;
  }
  Serial.println("Connected to BQ27441!");

  // Uset lipo.setCapacity(BATTERY_CAPACITY) to set the design capacity
  // of your battery.
  lipo.setCapacity(BATTERY_CAPACITY);
}

void printBatteryStats()
{
  // Read battery stats from the BQ27441-G1A
  unsigned int soc = lipo.soc();  // Read state-of-charge (%)
  unsigned int volts = lipo.voltage(); // Read battery voltage (mV)
  int current = lipo.current(AVG); // Read average current (mA)
  unsigned int fullCapacity = lipo.capacity(FULL); // Read full capacity (mAh)
  unsigned int capacity = lipo.capacity(REMAIN); // Read remaining capacity (mAh)
  int power = lipo.power(); // Read average power draw (mW)
  int health = lipo.soh(); // Read state-of-health (%)

  // Now print out those values:
  String toPrint = String(soc) + "% | ";
  toPrint += String(volts) + " mV | ";
  toPrint += String(current) + " mA | ";
  toPrint += String(capacity) + " / ";
  toPrint += String(fullCapacity) + " mAh | ";
  toPrint += String(power) + " mW | ";
  toPrint += String(health) + "%";

  Serial.println(toPrint);
}

void loop()
{
  //printBatteryStats();

  // Forward data from HW Serial to BLEUART
  while (Serial.available())
  {
    // Delay to wait for enough input, since we have a limited transmission buffer
    delay(2);

    uint8_t buf[64];
    int count = Serial.readBytes(buf, sizeof(buf));
    bleuart.write( buf, count );
  }
  if (bleuart.available() > 0) {
    int BLEbytes = 0;
    uint8_t BLEbuf[64];
    // Forward from BLEUART to HW Serial
    while ( bleuart.available() )
    {
      uint8_t ch;
      ch = (uint8_t) bleuart.read();
      Serial.write(ch);
      BLEbuf[BLEbytes] = ch;
      BLEbytes++;
    }

    //now transmit via LORA
    prepareTX();
    Serial.print(F("[SX1262] Transmitting packet ... "));

    // you can transmit C-string or Arduino string up to
    // 256 characters long
    // NOTE: transmit() is a blocking method!
    //       See example SX126x_Transmit_Interrupt for details
    //       on non-blocking transmission method.
    //int state = lora.transmit("Hello World!");

    // you can also transmit byte array up to 256 bytes long


    int state = lora.transmit(BLEbuf, BLEbytes + 1);


    if (state == ERR_NONE) {
      // the packet was successfully transmitted
      Serial.println(F("success!"));

      // print measured data rate
      Serial.print(F("[SX1262] Datarate:\t"));
      Serial.print(lora.getDataRate());
      Serial.println(F(" bps"));
      delay(200);
      prepareRX();

    } else if (state == ERR_PACKET_TOO_LONG) {
      // the supplied packet was longer than 256 bytes
      Serial.println(F("too long!"));

    } else if (state == ERR_TX_TIMEOUT) {
      // timeout occured while transmitting packet
      Serial.println(F("timeout!"));

    } else {
      // some other error occurred
      Serial.print(F("failed, code "));
      Serial.println(state);

    }

  }
  // check if the flag is set
  if (receivedFlag) {
    // disable the interrupt service routine while
    // processing the data
    enableInterrupt = false;

    // reset flag
    receivedFlag = false;

    // you can read received data as an Arduino String
    String str;
    int state = lora.readData(str);


    // you can also read received data as byte array
    /*
      byte byteArr[8];
      int state = lora.readData(byteArr, 8);
    */

    if (state == ERR_NONE) {
      // packet was successfully received
      Serial.println(F("[SX1262] Received packet!"));

      // print data of the packet
      Serial.print(F("[SX1262] Data:\t\t"));
      Serial.println(str);

      bleuart.print(str); //send via BLE
      tft.fillScreen(ST77XX_WHITE); //actually black
      digitalWrite(BLT, HIGH);
      unsigned int soc = lipo.soc();
      tft.setTextColor(ST77XX_RED);
      //tft.setCursor(135, 95);
      //tft.print(soc);
      //tft.print("%");
      tft.setCursor(0, 30);
      tft.println(str);
      //tft.setTextColor(ST77XX_RED);
      tft.print("RSSI: ");
      tft.print(lora.getRSSI());
      tft.println(" dBm ");
      tft.print("SNR: ");
      tft.print(lora.getSNR());
      tft.println(" dB");

      // print RSSI (Received Signal Strength Indicator)
      Serial.print(F("[SX1262] RSSI:\t\t"));
      Serial.print(lora.getRSSI());
      Serial.println(F(" dBm"));

      // print SNR (Signal-to-Noise Ratio)
      Serial.print(F("[SX1262] SNR:\t\t"));
      Serial.print(lora.getSNR());
      Serial.println(F(" dB"));

    } else if (state == ERR_CRC_MISMATCH) {
      // packet was received, but is malformed
      Serial.println(F("CRC error!"));

    } else {
      // some other error occurred
      Serial.print(F("failed, code "));
      Serial.println(state);

    }

    // put module back to listen mode
    lora.startReceive();

    // we're ready to receive more packets,
    // enable interrupt service routine
    enableInterrupt = true;
  }

  delay(200);
}

//button callbacks
void up_callback(void)
{
  digitalWrite(BLT, HIGH);
  unsigned int soc = lipo.soc();
  tft.setCursor(135, 95);
  tft.print(soc);
  tft.println("%");

}

void sw_callback(void)
{

  digitalWrite(BLT, LOW);
}


void ok_callback(void)
{

  prepareTX();
  Serial.print(F("[SX1262] Transmitting packet ... "));

  // you can transmit C-string or Arduino string up to
  // 256 characters long
  // NOTE: transmit() is a blocking method!
  //       See example SX126x_Transmit_Interrupt for details
  //       on non-blocking transmission method.
  int state = lora.transmit("Test");

  // you can also transmit byte array up to 256 bytes long
  /*
    byte byteArr[] = {0x01, 0x23, 0x45, 0x56, 0x78, 0xAB, 0xCD, 0xEF};
    int state = lora.transmit(byteArr, 8);
  */

  if (state == ERR_NONE) {
    // the packet was successfully transmitted
    Serial.println(F("success!"));

    // print measured data rate
    Serial.print(F("[SX1262] Datarate:\t"));
    Serial.print(lora.getDataRate());
    Serial.println(F(" bps"));
    delay(200);
    prepareRX();

  } else if (state == ERR_PACKET_TOO_LONG) {
    // the supplied packet was longer than 256 bytes
    Serial.println(F("too long!"));

  } else if (state == ERR_TX_TIMEOUT) {
    // timeout occured while transmitting packet
    Serial.println(F("timeout!"));

  } else {
    // some other error occurred
    Serial.print(F("failed, code "));
    Serial.println(state);

  }
}

void prepareTX(void) {
  digitalWrite(TXEN, HIGH);
  digitalWrite(RXEN, LOW);
}

void prepareRX(void) {
  digitalWrite(TXEN, LOW);
  digitalWrite(RXEN, HIGH);
}

// this function is called when a complete packet
// is received by the module
// IMPORTANT: this function MUST be 'void' type
//            and MUST NOT have any arguments!
void setFlag(void) {
  // check if the interrupt is enabled
  if (!enableInterrupt) {
    return;
  }

  // we got a packet, set the flag
  receivedFlag = true;
}

// callback invoked when central connects
void connect_callback(uint16_t conn_handle)
{
  // Get the reference to current connection
  BLEConnection* connection = Bluefruit.Connection(conn_handle);

  char central_name[32] = { 0 };
  connection->getPeerName(central_name, sizeof(central_name));

  Serial.print("Connected to ");
  Serial.println(central_name);
}

/**
   Callback invoked when a connection is dropped
   @param conn_handle connection where this event happens
   @param reason is a BLE_HCI_STATUS_CODE which can be found in ble_hci.h
*/
void disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
  (void) conn_handle;
  (void) reason;

  Serial.println();
  Serial.print("Disconnected, reason = 0x"); Serial.println(reason, HEX);
}
