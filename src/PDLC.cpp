#include "PDLC.h"

#include "Constants.h"
#include "I2CBus.h"

void PDLC::on() {
  I2CBus::sendPicCommand(I2C_COMMAND_PDLC_ON, "PDLC on");
}

void PDLC::off() {
  I2CBus::sendPicCommand(I2C_COMMAND_PDLC_OFF, "PDLC off");
}
