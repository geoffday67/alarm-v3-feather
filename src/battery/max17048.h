#pragma once

#include <stdint.h>

// MAX17048 ModelGauge LiPo fuel gauge, fitted on the Feather ESP32-C6 itself.
// It's powered from the cell (BAT), so it keeps running, and keeps its
// register contents, through the ESP32's deep sleep and any ESP32 reset.
//
// Registers are 16 bits, MSB first, and must be accessed as a whole word in
// one transaction (datasheet: "8-bit writes cause no effect", "Both register
// bytes must be read in the same transaction for the register data to be
// valid").
//
// Each function takes the I2CBus lock for its own transaction(s), so don't
// call them while holding an I2CBus::Guard. Requires I2CBus::begin() and IO20
// high (bus pull-ups on VSENSOR).

// 7-bit I2C address
#define MAX17048_ADDR          0x36

// Register addresses (datasheet register summary)
#define MAX17048_REG_VCELL     0x02  // cell voltage, 78.125 uV/LSB
#define MAX17048_REG_SOC       0x04  // state of charge, 1/256 %/LSB
#define MAX17048_REG_MODE      0x06  // write-only
#define MAX17048_REG_VERSION   0x08  // 0x001_ on the MAX17048
#define MAX17048_REG_HIBRT     0x0A
#define MAX17048_REG_CONFIG    0x0C
#define MAX17048_REG_VALRT     0x14
#define MAX17048_REG_CRATE     0x16  // charge/discharge rate, 0.208 %/hr/LSB
#define MAX17048_REG_VRESET_ID 0x18
#define MAX17048_REG_STATUS    0x1A
#define MAX17048_REG_CMD       0xFE

// MODE (0x06) bits. Bit 12 (HibStat) is read-only and ignored on write.
#define MAX17048_MODE_QUICK_START (1 << 14)
#define MAX17048_MODE_EN_SLEEP    (1 << 13)

// CONFIG (0x0C) bits, as a 16-bit word (RCOMP is the upper byte).
#define MAX17048_CONFIG_SLEEP     (1 << 7)

// VRESET/ID (0x18) bits, as a 16-bit word: VRESET[7:1] in bits 15-9, Dis in
// bit 8, the read-only ID in the lower byte (writes to it are ignored).
#define MAX17048_VRESET_DIS       (1 << 8)

// Whole-register access. Return false if the gauge doesn't ACK or a read
// returns short; value is then unchanged.
bool max17048ReadReg(uint8_t reg, uint16_t& value);
bool max17048WriteReg(uint8_t reg, uint16_t value);

// True if the gauge responds and VERSION reads 0x001_ (the same check as
// Adafruit's MAX1704X library). version gets the full register.
bool max17048IsPresent(uint16_t& version);

// Makes sure the gauge can't enter sleep mode: clears CONFIG.SLEEP (which
// forces an exit if it's asleep) then MODE.EnSleep. Both are 0 from the
// gauge's power-on reset, so normally this changes nothing.
//
// Sleep needs EnSleep = 1 plus either CONFIG.SLEEP = 1 or SDA and SCL both
// held low for tSLEEP (1.75-2.5 s). The bus pull-ups are on the IO20-switched
// rail, so the lines sit low throughout the ESP32's deep sleep. In sleep the
// gauge "halts all operations" and can't follow the cell, so the SOC would
// be wrong on waking. Hibernate is used instead (see max17048ForceHibernate()).
bool max17048DisableSleep();

// Keeps the gauge in hibernate permanently: HIBRT = 0xFFFF (datasheet HIBRT
// Register). Hibernate draws 3-5 uA against 23-40 uA active, and converts
// (VCELL and SOC update) once every 45 s instead of every 250 ms.
//
// By default (HIBRT = 0x8030) the gauge switches itself: it enters hibernate
// once |CRATE| has been under HibThr (0x80 x 0.208 %/hr = 26.6 %/hr) for more
// than 6 minutes, and leaves it at any ADC sample where |OCV - VCELL| exceeds
// ActThr (0x30 x 1.25 mV = 60 mV) - e.g. the cell's voltage sag under a Wi-Fi
// or audio load - then stays active for at least those 6 minutes.
//
// The datasheet recommends forced hibernate for loads under C/4 (500 mA on a
// 2000 mAh cell); here the peaks are Wi-Fi TX (up to ~350 mA) and charging
// (~196 mA).
bool max17048ForceHibernate();

// Sets VRESET.Dis, turning off the fast analog comparator that detects the
// cell being removed and reinserted, while in hibernate. Hibernate supply
// current is then 3 uA typ / 5 uA max rather than 4 uA typ (Electrical
// Characteristics; the register description says "approximately 0.5uA").
// Removal/reinsertion is still detected by the ADC, just more slowly: the IC
// resets 250 ms after VCELL rises back above VRESET rather than 1 ms. Only
// matters for the SOC estimate, which isn't used. Read-modify-write, so the
// VRESET threshold is unchanged.
bool max17048DisableResetComparator();

// Cell voltage in mV (VCELL * 78.125 uV, rounded).
bool max17048ReadMillivolts(uint16_t& millivolts);
