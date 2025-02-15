#ifndef display_h
#define display_h

#include <Wire.h>
#include <SSD1306Ascii.h>
#include <SSD1306AsciiWire.h>
// 0X3C+SA0 - 0x3C or 0x3D
#define I2C_ADDRESS 0x3C

void setup_display() ;

extern SSD1306AsciiWire oled;

#endif //display_h

