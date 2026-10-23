/**
 * Pokemon GO Plus LED pattern parsing and button press generation.
 *
 * The phone tells the PGP what to show by writing LED/vibration patterns; the
 * colors say what is going on (Pokemon nearby, Pokestop, caught, ...). Ported
 * from pgpemu's pgp_led_handler.c and pgp_autobutton.c. Pure C.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PgpLedEventNone, // LEDs off
    PgpLedEventPokemon,
    PgpLedEventNewPokemon,
    PgpLedEventPokestop,
    PgpLedEventCaught,
    PgpLedEventFled,
    PgpLedEventItems,
    PgpLedEventBagFull,
    PgpLedEventBoxFull,
    PgpLedEventNoBalls, // or Pokestop went out of range
    PgpLedEventUnknown,
} PgpLedEvent;

typedef struct {
    PgpLedEvent event;
    uint32_t duration_ms;
    uint8_t ball_shakes;
} PgpLedPattern;

/** Parse an LED write. Returns false if the data is incomplete or malformed. */
bool pgp_led_parse(const uint8_t* data, size_t len, PgpLedPattern* pattern);

const char* pgp_led_event_name(PgpLedEvent event);

/**
 * Build the 2 byte button notification: 10 samples of 50 ms, pressed for a
 * random 200-500 ms window starting in the first 300 ms.
 */
void pgp_led_button_pattern(uint32_t random, uint8_t* pattern);
