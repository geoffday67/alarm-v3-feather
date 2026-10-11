#include "pcf8563_timer.h"
#include "esp_attr.h"

// Timer countdown periods selected by pcf8563Init()'s debug flag
static const uint8_t PERIOD_NORMAL_S = 60;
static const uint8_t PERIOD_DEBUG_S  = 3;

// Period last written to the timer. Kept in LP RAM so it survives deep
// sleep; zeroed by power-on, reflash, or reset, forcing a reconfigure.
static RTC_DATA_ATTR uint8_t configuredPeriod = 0;

void pcf8563WriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(PCF8563_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t pcf8563ReadReg(uint8_t reg) {
  Wire.beginTransmission(PCF8563_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);  // repeated start, no stop condition
  Wire.requestFrom(PCF8563_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

bool pcf8563IsPresent() {
  // 1. Address ACK
  Wire.beginTransmission(PCF8563_ADDR);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  // 2. Register round trip. 0xA5 = AE_M=1 (alarm disabled) + BCD 25,
  //    distinct from the 0x00/0xFF a stuck or floating bus returns.
  const uint8_t pattern = 0xA5;
  pcf8563WriteReg(PCF8563_REG_MIN_ALARM, pattern);
  bool ok = (pcf8563ReadReg(PCF8563_REG_MIN_ALARM) == pattern);

  // Restore power-on state: AE_M = 1, alarm disabled
  pcf8563WriteReg(PCF8563_REG_MIN_ALARM, 0x80);
  return ok;
}

bool pcf8563Init(bool debug) {
  if (!pcf8563IsPresent()) {
    return false;
  }

  // Control_status_1: STOP should only be set mid-pcf8563SetDateTime()
  // (e.g. a reset during that call), TESTC is 1 from power-on. Normal
  // operation wants the whole register at 0. Only written if needed so
  // a running clock is never touched.
  if (pcf8563ReadReg(PCF8563_REG_CTRL1) != 0x00) {
    pcf8563WriteReg(PCF8563_REG_CTRL1, 0x00);
  }

  // CLKOUT off: FE = 0. FD[1:0] are don't-care once FE is clear.
  pcf8563WriteReg(PCF8563_REG_CLKOUT, 0x00);

  // Periodic interrupt: leave alone if already running with the
  // requested period, so the countdown phase survives deep-sleep wakes.
  // The register check also catches the RTC having been reset without
  // the ESP32 (after power-on, TE = 0 and TIE = 0).
  uint8_t period = debug ? PERIOD_DEBUG_S : PERIOD_NORMAL_S;
  uint8_t tmrCtrl = pcf8563ReadReg(PCF8563_REG_TMR_CTRL);
  uint8_t ctrl2   = pcf8563ReadReg(PCF8563_REG_CTRL2);
  bool timerRunning = (tmrCtrl & (PCF8563_BIT_TE | 0x03)) ==
                      (PCF8563_BIT_TE | PCF8563_TD_1HZ);
  bool intEnabled   = (ctrl2 & (PCF8563_BIT_TI_TP | PCF8563_BIT_TIE)) ==
                      PCF8563_BIT_TIE;
  if (!(timerRunning && intEnabled && configuredPeriod == period)) {
    pcf8563ConfigureMinuteInterrupt(period);
  }

  return true;
}

void pcf8563ConfigureMinuteInterrupt(uint8_t periodSeconds) {
  // Disable the timer while it's being reconfigured
  pcf8563WriteReg(PCF8563_REG_TMR_CTRL, 0x00);

  // Countdown value: n counts at a 1 Hz source clock = n s period
  pcf8563WriteReg(PCF8563_REG_TIMER, periodSeconds);

  // Enable timer, select 1 Hz source clock
  pcf8563WriteReg(PCF8563_REG_TMR_CTRL, PCF8563_BIT_TE | PCF8563_TD_1HZ);

  // Control_status_2:
  //   TI_TP = 0  -> INT level-active, tracks TF (needed for GPIO level wakeup)
  //   AF    = 1  -> leave alarm flag unchanged (write-1 = no-op per datasheet)
  //   TF    = 0  -> clear any stale timer flag
  //   AIE   = 0  -> alarm interrupt unused
  //   TIE   = 1  -> enable timer interrupt
  pcf8563WriteReg(PCF8563_REG_CTRL2, PCF8563_BIT_AF | PCF8563_BIT_TIE);

  configuredPeriod = periodSeconds;
}

void pcf8563ClearTimerFlag() {
  uint8_t ctrl2 = pcf8563ReadReg(PCF8563_REG_CTRL2);

  // Per datasheet: writing 0 to a flag bit clears it, writing 1 leaves
  // it unchanged. Preserve AF (write 1) while clearing TF (write 0),
  // and preserve TIE/AIE/TI_TP as currently set.
  ctrl2 = (ctrl2 & ~PCF8563_BIT_TF) | PCF8563_BIT_AF;
  pcf8563WriteReg(PCF8563_REG_CTRL2, ctrl2);
}

static uint8_t toBCD(uint8_t value) {
  return ((value / 10) << 4) | (value % 10);
}

static uint8_t fromBCD(uint8_t bcd) {
  return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

bool pcf8563IsTimeValid() {
  uint8_t vlSeconds = pcf8563ReadReg(PCF8563_REG_VL_SECONDS);
  return (vlSeconds & 0x80) == 0;  // VL bit is bit 7
}

void pcf8563SetDateTime(uint8_t year, uint8_t month, uint8_t day,
                         uint8_t weekday, uint8_t hour, uint8_t minute,
                         uint8_t second) {
  // Freeze the prescaler before writing multi-byte time registers
  // (datasheet Section 8.10). Writing STOP alone also clears TESTC
  // (reset value 1); 0 is the datasheet's value for normal operation.
  pcf8563WriteReg(PCF8563_REG_CTRL1, PCF8563_BIT_STOP);

  // VL_seconds: bit 7 = 0 clears VL, remaining bits are BCD seconds
  pcf8563WriteReg(PCF8563_REG_VL_SECONDS, toBCD(second) & 0x7F);
  pcf8563WriteReg(PCF8563_REG_MINUTES, toBCD(minute));
  pcf8563WriteReg(PCF8563_REG_HOURS, toBCD(hour));
  pcf8563WriteReg(PCF8563_REG_DAYS, toBCD(day));
  pcf8563WriteReg(PCF8563_REG_WEEKDAYS, weekday & 0x07);
  pcf8563WriteReg(PCF8563_REG_CENT_MONTHS, toBCD(month));  // C bit = 0
  pcf8563WriteReg(PCF8563_REG_YEARS, toBCD(year));

  // Release STOP. Per datasheet, the first 1 Hz tick after this point
  // occurs 0.507813-0.507935 s later, and I2C timing accuracy adds up
  // to one 8.192 kHz cycle (~122 us) of uncertainty on top of that.
  pcf8563WriteReg(PCF8563_REG_CTRL1, 0x00);
}

bool pcf8563ReadDateTime(uint8_t &year, uint8_t &month, uint8_t &day,
                          uint8_t &weekday, uint8_t &hour, uint8_t &minute,
                          uint8_t &second) {
  // Registers 0x02-0x08 auto-increment, so one burst read covers the
  // full VL_seconds..Years range (Section 8, register organization).
  Wire.beginTransmission(PCF8563_ADDR);
  Wire.write(PCF8563_REG_VL_SECONDS);
  Wire.endTransmission(false);
  Wire.requestFrom(PCF8563_ADDR, (uint8_t)7);

  uint8_t raw[7];
  for (uint8_t i = 0; i < 7; i++) {
    raw[i] = Wire.available() ? Wire.read() : 0xFF;
  }

  bool vlSet = (raw[0] & 0x80) != 0;
  second  = fromBCD(raw[0] & 0x7F);  // VL_seconds, mask out VL bit
  minute  = fromBCD(raw[1] & 0x7F);  // Minutes, bit 7 unused
  hour    = fromBCD(raw[2] & 0x3F);  // Hours, bits 7:6 unused
  day     = fromBCD(raw[3] & 0x3F);  // Days, bits 7:6 unused
  weekday = raw[4] & 0x07;           // Weekdays, bits 7:3 unused
  month   = fromBCD(raw[5] & 0x1F);  // Century_months, bits 6:5 unused
  year    = fromBCD(raw[6]);         // Years, full byte

  return !vlSet;
}
