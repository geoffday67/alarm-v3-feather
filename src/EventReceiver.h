#ifndef __EVENT_RECEIVER__
#define __EVENT_RECEIVER__

#include "Event.h"

class EventReceiver {
public:
    // Handle event, return whether to continue (false stops further receivers).
    virtual bool onEvent(Event* pevent) = 0;
};

#endif