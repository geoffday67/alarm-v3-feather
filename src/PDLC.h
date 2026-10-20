#pragma once

// The PDLC film panel. The PIC drives the external PDLC driver's enable pin
// (RC2) and holds it through its own sleep, so the panel stays in whichever
// state was last commanded until told otherwise, including while this chip
// is in deep sleep.
//
// Both functions send one command over the shared I2C bus and take the bus
// lock themselves (see I2CBus::sendPicCommand()), so don't call them while
// holding an I2CBus::Guard. The bus must already be started.
namespace PDLC {
void on();
void off();
}  // namespace PDLC
