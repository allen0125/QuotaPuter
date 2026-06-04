// keyboard.h — Cardputer 74HC138 matrix keyboard driver (PRD §9).
//
// The official Cardputer keyboard driver ships only as an Arduino library, so we
// reimplement the documented 74HC138 active-low matrix scan in pure ESP-IDF.
// A scan task polls the matrix every ~20 ms and pushes edge-triggered key events
// (plus one long-press emission per held key) onto a queue the UI drains.
//
// Hardware (verified against m5stack/M5Cardputer source):
//   address pins {GPIO8=A0, GPIO9=A1, GPIO11=A2}, written as the binary scan
//   index 0..7 (little-endian); 7 column inputs {13,15,3,4,5,6,7} with internal
//   pull-ups; a pressed key reads LOW.
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// One key event. `ch` is the decoded printable character (shifted when Shift is
// held), or 0 for non-printable keys. The boolean flags identify special keys
// and the modifier state held during the press. The Cardputer has no dedicated
// arrow/Esc keys — the UI maps `;`/`.`/`,`/`/` to arrows and Backspace to Back.
typedef struct {
    char ch;          // printable char, or 0
    bool enter;       // Enter
    bool del;         // Backspace/Del
    bool tab;         // Tab
    bool space;       // Space (ch is also ' ')
    bool fn, shift, ctrl, alt, opt;  // modifier state at press time
    bool long_press;  // true => the >=500 ms long-press emission for this key
} kb_event_t;

// Configure the matrix GPIOs and start the scan task. Call once, after M5.begin()
// so the display init doesn't reconfigure shared pins afterwards.
esp_err_t keyboard_init(void);

// Pop the next key event (non-blocking). Returns true and fills *ev if available.
bool keyboard_poll(kb_event_t *ev);

#ifdef __cplusplus
}
#endif
