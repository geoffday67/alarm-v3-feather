#pragma once

#include <stdint.h>

// Serialises access to the shared I2C bus (PCF8563, PIC16F15225 and the
// MAX17048 battery gauge) between tasks.
//
// TwoWire already holds its own mutex from beginTransmission() until
// endTransmission(true), or until requestFrom() returns after a repeated
// start, so every bus transaction is atomic. What it doesn't cover is reading
// the result: Wire.available()/Wire.read() run after requestFrom() has
// released its lock, so a second task's requestFrom() could overwrite the
// receive buffer in between. Hold an I2CBus::Guard for the whole transaction,
// including reading the bytes out.
//
// Not recursive: don't take a Guard while already holding one in the same
// task. Never use from an ISR.
//
// Battery gauge reads need nothing more: each register is one self-contained
// transaction under its own Guard, and no two registers have to be read
// atomically together. They're made from the clock task (and from setup()
// before the tasks start), sequentially with its RTC accesses.
//
// TODO: revisit locking when adding periodic RTC updates from SNTP while
// other tasks are running. pcf8563SetDateTime() locks per register write, so
// another task's transaction can land inside its STOP-to-release timing.
namespace I2CBus {
// Creates the mutex and starts the bus. Call once per boot, before any task
// that uses the bus is started.
void begin(int sdaPin, int sclPin, uint32_t frequency);

void lock();
void unlock();

// Sends a single-byte command to the PIC and logs the result. Takes the bus
// lock itself, so don't call it while holding a Guard.
void sendPicCommand(uint8_t command, const char* name);

// Scoped lock: takes the bus on construction, releases it on destruction.
class Guard {
 public:
  Guard() { lock(); }
  ~Guard() { unlock(); }
  Guard(const Guard&) = delete;
  Guard& operator=(const Guard&) = delete;
};
}  // namespace I2CBus
