#include "pgp.h"
#include <furi_hal_bt.h>

static volatile bool connected = false;
static volatile bool press_req = false;

/* ---------- very dumb placeholders ---------- */
bool pgp_is_connected(void) {
    return connected;                        // TODO: track GAP events
}

bool pgp_press_requested(void) {
    bool r = press_req;
    press_req = false;                       // auto-reset
    return r;                                // TODO: look at GATT writes
}

void pgp_send_button_press(void) {
    /* TODO: write 0x03 (press+release) to the PGP button characteristic */
}

/*  You will later wire gap_event_cb() → set connected/press_req here. */
