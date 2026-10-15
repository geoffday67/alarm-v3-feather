#define TOP_SWITCH_PIN   6

#define LADDER_POWER_PIN 15
#define HOUR_LADDER_PIN 1
#define MINUTE_LADDER_PIN 4

#define RTC_INTERRUPT_PIN 7

#define I2S_LRC 14
#define I2S_BLCK 0
#define I2S_DIN 8

// I2C bus to the PIC16F15225 client (default Feather ESP32-C6 SCL/SDA pins).
#define I2C_SCL_PIN 18
#define I2C_SDA_PIN 19

// Must match the address programmed into the PIC's SSP1ADD.
#define PIC_I2C_ADDRESS 0x08

// Must match the PIC.
#define I2C_COMMAND_EINK_LIGHT_ON 0x01
#define I2C_COMMAND_EINK_LIGHT_OFF 0x02
#define I2C_COMMAND_PDLC_ON 0x03
#define I2C_COMMAND_PDLC_OFF 0x04
#define I2C_COMMAND_PULSE_MOOMIN 0x05

#define SCREEN_LIGHT_TIME 4000

// Wiring between the Feather ESP32C6 and the DESPI-C02 adapter board carrying
// the GDEY029T94-FL03 (SSD1680) e-paper panel.
#define EPD_BUSY_PIN 23
#define EPD_RST_PIN  22
#define EPD_DC_PIN   21
#define EPD_CS_PIN   2
#define EPD_SCK_PIN  3
#define EPD_MOSI_PIN 5

// The panel's SSD1680 RAM is native portrait (128 wide x 296 tall), but the
// panel is mounted/used landscape. Rotation applied when mapping logical
// (x,y) drawing coordinates onto the native buffer: 0 = no rotation
// (portrait), 1 = 90 deg CW, 2 = 180 deg, 3 = 90 deg CCW. If text comes out
// upside down or mirrored, try the other 90 deg value (1 <-> 3).
#define EPD_ROTATION 3

// Set to 1 if the image needs reflecting about a horizontal axis (top/bottom
// flip) to appear correctly on the panel, 0 otherwise.
#define EPD_FLIP_H 1

// Local time zone as a POSIX TZ string (applied with setenv/tzset). Europe/London:
// GMT (UTC+0) in winter, BST (UTC+1) from the last Sunday in March at 01:00 GMT
// to the last Sunday in October at 02:00 BST. Hard-coded until there's a
// configuration UI.
#define TIMEZONE_POSIX "GMT0BST,M3.5.0/1,M10.5.0"
