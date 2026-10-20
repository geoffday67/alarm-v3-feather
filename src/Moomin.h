#pragma once

// The Moomin light. Its only control is a touch input, which the PIC drives
// through an opto-coupler (RA2) with a ~500 ms pulse (430-585 ms, set by the
// PIC's LFINTOSC tolerance) to simulate a finger touch. The light has no
// separate on and off inputs and its state can't be read back, so each pulse
// steps it to whatever the light does next on a touch.
//
// Sends one command over the shared I2C bus and takes the bus lock itself
// (see I2CBus::sendPicCommand()), so don't call it while holding an
// I2CBus::Guard. The bus must already be started. Returns as soon as the
// command is sent; the PIC times the pulse. Calling it again during a pulse
// restarts the 500 ms rather than giving a second touch.
namespace Moomin {
void pulse();
}  // namespace Moomin
