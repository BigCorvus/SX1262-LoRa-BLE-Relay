# SX1262-LoRa-BLE-Relay
![LoraBLERelay22](https://github.com/BigCorvus/SX1262-LoRa-BLE-Relay/blob/master/LORA-BLE-RelayBrd22dbm.png)
![LoraBLERelay30](https://github.com/BigCorvus/SX1262-LoRa-BLE-Relay/blob/master/LORA-BLE-RelayBrd30dbm_v1.1.png)
![LoraRelayTop](https://github.com/BigCorvus/SX1262-LoRa-BLE-Relay/blob/master/LoraRelaysTop.jpg)
![LoraRelayBot](https://github.com/BigCorvus/SX1262-LoRa-BLE-Relay/blob/master/loraRelaysBot.jpg)
These are multipurpose dev boards for the Semtech SX1262 chipset. EBYTE or CDSNET or whatever their name is sell multiple versions of RF modules based on this new LoRa chip on Aliexpress. One of its main advantages is the low RX current draw of about 5ma, which is that of the previous versions cut in half.  

This project can be used as a Relay between some BLE device and other LoRa nodes to implement a simple off-grid communicator or some day, as soon as LoRaWAN drivers for the SX1262 exist, it can connect to TTN. Probably the most interesting application: this device is planned to be used as hardware for the Meshtastic project https://github.com/meshtastic/Meshtastic-device  

These PCBs are designed for the SPI-versions of the LORA modules only (E22-900M30S which is the 1W version and E22-900M22S which transmits at 160mW max). Both operate in the 868 and 915Mhz band.  The MCU module is a cheap YJ-18010 by Holyiot which has an nRF52840, a ceramic antenna and low frequency crystal on board. The same module is also produced by Waveshare. It runs a Feather-nRF52840 bootloader and has roughly the same pinout, at least for UART, SPI and I2C. A tiny 160x80 IPS screen based on the ST7735 chipset and 3 buttons are provided for basic data visualisation and interfacing. The power management is done by this concept: https://github.com/BigCorvus/PowerManager basically without modification. In my first tests I had problems getting it to work properly, so in the next version I will probably return to a simpler on/off concept. As a workaround I did not populate the capacitor between the pushbutton and EN of the LDO and connected EN to VHI via a switch (see pictures). When turned off the fuel gauge draws about 25-65µA. While turned on and in receive mode with the backlight off the 30dbm version draws about 18mA. The Backlight is additional 10mA depending on the current limiting resistor and PWM duty cycle. The smaller version draws only 9ma with bluetooth connected and in receive mode!! While transmitting the 30dbm version pulls about 1,2A from the battery!
Note that the 30dbm version of the board needs an extra 5V boost converter, which is a G5177C.  

A simple Arduino test sketch demonstrates the most important functions and can be used for communication already. It will be improved in the future.  

Update: v1.1 of the 30dbm version is out. It has a simple slide switch and uses clickier buttons (LS12T2 from digikey). There's also a JST-SH 1mm connector for GPS modules and a high-side switch (generic SOT23 P-channel mosfet) controlled from P1.00. LED2 has been moved to P1.10 and the LEDs rearranged. The boost converter is now controlled from P0.05. 

