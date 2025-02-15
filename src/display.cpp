#include <Wire.h>
#include "display.h"

// 0X3C+SA0 - 0x3C or 0x3D
#define I2C_ADDRESS 0x3C

// Define proper RST_PIN if required.
#define RST_PIN -1

SSD1306AsciiWire oled;

void setup_display() {
  Wire.begin();
  Wire.setClock(400000L);
  
#if RST_PIN >= 0
  oled.begin(&Adafruit128x32, I2C_ADDRESS, RST_PIN);
#else // RST_PIN >= 0
  oled.begin(&Adafruit128x64, I2C_ADDRESS);
#endif // RST_PIN >= 0

  oled.setFont(Callibri14);

  uint32_t m = micros();
  oled.clear();
  // first row
  oled.println(" Init");

  // second row
  oled.set2X();
  oled.println("set2X test");

}