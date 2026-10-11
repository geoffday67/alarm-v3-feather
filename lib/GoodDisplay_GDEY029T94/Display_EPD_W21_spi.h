#ifndef _DISPLAY_EPD_W21_SPI_
#define _DISPLAY_EPD_W21_SPI_
#include "Arduino.h"
#include "../../src/Constants.h"

//IO settings
#define isEPD_W21_BUSY digitalRead(EPD_BUSY_PIN)  //BUSY
#define EPD_W21_RST_0 digitalWrite(EPD_RST_PIN,LOW)  //RES
#define EPD_W21_RST_1 digitalWrite(EPD_RST_PIN,HIGH)
#define EPD_W21_DC_0  digitalWrite(EPD_DC_PIN,LOW) //DC
#define EPD_W21_DC_1  digitalWrite(EPD_DC_PIN,HIGH)
#define EPD_W21_CS_0 digitalWrite(EPD_CS_PIN,LOW) //CS
#define EPD_W21_CS_1 digitalWrite(EPD_CS_PIN,HIGH)


void SPI_Write(unsigned char value);
void EPD_W21_WriteDATA(unsigned char datas);
void EPD_W21_WriteCMD(unsigned char command);


#endif
