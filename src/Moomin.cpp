#include "Moomin.h"

#include "Constants.h"
#include "I2CBus.h"

void Moomin::pulse() {
  I2CBus::sendPicCommand(I2C_COMMAND_PULSE_MOOMIN, "Moomin pulse");
}
