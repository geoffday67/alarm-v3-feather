#include "EventManager.h"
#include "Event.h"

using namespace std;

classEventManager EventManager;

KeyEvent::KeyEvent(int key): Event(EVENT_KEY) {
    this->key = key;
}

TimeEvent::TimeEvent(time_t time): Event(EVENT_TIME) {
    this->time = time;
}

EncoderEvent::EncoderEvent(int position): Event(EVENT_ENCODER) {
    this->position = position;
}

MenuEvent::MenuEvent(int choice): Event(EVENT_MENU) {
    this->choice = choice;
}

SwitchEvent::SwitchEvent(bool pressed): Event(EVENT_SWITCH) {
    this->pressed = pressed;
}

InactivityEvent::InactivityEvent(): Event(EVENT_INACTIVITY) {
}

/*****************************************************************************/
/*****************************************************************************/
RegisteredReceiver::RegisteredReceiver(EventReceiver *per, int type) {
    this->pEventReceiver = per;
    this->type = type;
    this->fired = false;
}

/*****************************************************************************/
/*****************************************************************************/
void classEventManager::addListener(int type, EventReceiver *per) {
    RegisteredReceiver receiver(per, type);
    listReceivers.push_back(receiver);
}

/*****************************************************************************/
void classEventManager::removeListener(EventReceiver *per) {
    if (listReceivers.empty()) {
        return;
    }

    vector<RegisteredReceiver>::iterator i = listReceivers.begin();
    while (i != listReceivers.end()) {
        if (i->pEventReceiver == per) {
            i = listReceivers.erase(i);
        } else {
            i++;
        }
    }
}

/*****************************************************************************/
void classEventManager::queueEvent (Event* pevent) {
    queueEvents.push(pevent);
}

/*****************************************************************************/
void classEventManager::emptyQueue() {
    while (!queueEvents.empty()) {
        Event* pevent = queueEvents.front();
        queueEvents.pop();
        delete pevent;
    }
}

/*****************************************************************************/
void classEventManager::processEvents() {
    // Get each event from the queue and fire all interested receivers.
    // So the queue is empty at the end - possibly limit number of events processed so it doesn't take too long??

    if (queueEvents.empty())
        return;

    vector<RegisteredReceiver> receivers(listReceivers);

    while (!queueEvents.empty()) {
        Event* pevent = queueEvents.front();
        queueEvents.pop();

        vector<RegisteredReceiver>::const_iterator i;
        for (i = receivers.begin(); i != receivers.end(); i++) {
            if (i->type == pevent->type) {
                if (!i->pEventReceiver->onEvent(pevent)) {
                    break;
                }
            }
        }

        delete pevent;
    }
}
