#include "Screen.h"

Screen::Screen () {
}

Screen::~Screen() {}

void Screen::activate() {
    active = true;
    onActivate();
}

void Screen::onActivate() {
}

void Screen::onDeactivate() {
}

void Screen::deactivate() {
    active = false;
    onDeactivate();
}
