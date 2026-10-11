#pragma once

#include <vector>
#include <queue>
#include <Time.h>
#include "Event.h"
#include "EventReceiver.h"

#define EVENT_KEY           1
#define EVENT_TIME          2
#define EVENT_ENCODER       3
#define EVENT_INACTIVITY    4
#define EVENT_MENU          5
#define EVENT_SWITCH        6

/*****************************************************************************/
class KeyEvent: public Event {
public:
    KeyEvent(int key);
    int key;
};

/*****************************************************************************/
class TimeEvent: public Event {
public:
    TimeEvent(time_t time);
    time_t time;
};

/*****************************************************************************/
class InactivityEvent: public Event {
public:
    InactivityEvent();
};

/*****************************************************************************/
class EncoderEvent: public Event {
public:
    EncoderEvent(int position);
    int position;
};

/*****************************************************************************/
class MenuEvent: public Event {
public:
    MenuEvent(int choice);
    int choice;
};

/*****************************************************************************/
class SwitchEvent: public Event {
public:
    SwitchEvent(bool pressed);
    bool pressed;
};

/*****************************************************************************/
class RegisteredReceiver {
public:
    RegisteredReceiver(EventReceiver *per, int type);
    EventReceiver *pEventReceiver;
    int type;
    bool fired;
};

/*****************************************************************************/
class classEventManager {
public:
    void addListener(int type, EventReceiver *per);
    void removeListener(EventReceiver *per);
    void queueEvent (Event* pevent);
    void processEvents();
    void emptyQueue();

private:
    std::queue<Event*> queueEvents;
    std::vector<RegisteredReceiver> listReceivers;
};

extern classEventManager EventManager;
