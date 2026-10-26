#pragma once

// What happens when the alarm goes off. Stages:
//
//   Idle      - no alarm in progress.
//   Ringing   - start() has started the music, given the Moomin light one
//               touch pulse, cleared the PDLC film (PDLC::on()) and turned
//               the eInk front light on with no time limit.
//   FilmClear - a button press has stopped the music; the film is still
//               clear.
//
// From Ringing, a press moves to FilmClear; if there's no press, the
// sequence ends (film and front light off, back to Idle) when the music
// finishes. From
// FilmClear, a second press ends it, or it ends by itself FILM_TIMEOUT
// (30 s, see AlarmSequence.cpp) after the music was stopped. The Moomin
// light is never turned off: its only control is a touch pulse, and its
// state can't be read back.
//
// Presses reach the sequence through handlePress(), called by the button
// task; the endings that happen by themselves are handled by the sequence's
// own task.
//
// A Sleep inhibit is held from start() until the film and light are turned
// off, so the chip can't deep sleep mid-sequence; every path ends with both
// off and the inhibit released, after which the chip can sleep again.
//
// start() and handlePress() are safe from any task (not from an ISR). Each
// stage change, including the sequence task's own endings, is made under one
// lock, so a press that races with an ending sees the stage after it. They
// send I2C commands (PDLC, Moomin, front light), so don't call them while
// holding an I2CBus::Guard.
namespace AlarmSequence {

// Call on every boot, including each deep-sleep wake, after Music::init()
// and before anything can call start() or handlePress() (Clock::init(),
// TopButton::init()). Starts the (idle) sequence task.
void init();

// Idle -> Ringing. Does nothing (logged) if a sequence is already running.
void start();

// Moves the sequence on for a button press: Ringing -> FilmClear (music
// stopped), FilmClear -> Idle (film and light off). Returns true if the sequence took
// the press; false if it's Idle, in which case the press is the caller's to
// handle as normal.
bool handlePress();

}  // namespace AlarmSequence
