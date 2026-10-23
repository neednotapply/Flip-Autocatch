#include "pgp_led.h"

#include <string.h>

#define PGP_LED_HEADER_LEN   4
#define PGP_LED_STEP_LEN     3
#define PGP_LED_STEP_MS      50
#define PGP_BUTTON_SAMPLES   10
#define PGP_BUTTON_MIN_SHIFT 4

bool pgp_led_parse(const uint8_t* data, size_t len, PgpLedPattern* pattern) {
    memset(pattern, 0, sizeof(PgpLedPattern));
    if(len < PGP_LED_HEADER_LEN) {
        return false;
    }

    size_t steps = data[3] & 0x1f;
    if(len < PGP_LED_HEADER_LEN + steps * PGP_LED_STEP_LEN) {
        return false;
    }

    int count_red = 0, count_green = 0, count_blue = 0;
    int count_yellow = 0, count_white = 0;
    int count_off = 0, count_on = 0;
    int ball_shakes = 0;
    uint32_t duration = 0;

    for(size_t i = 0; i < steps; i++) {
        const uint8_t* step = &data[PGP_LED_HEADER_LEN + PGP_LED_STEP_LEN * i];
        uint8_t red = step[1] & 0xf;
        uint8_t green = (step[1] >> 4) & 0xf;
        uint8_t blue = step[2] & 0xf;

        duration += step[0];

        if(!red && !green && !blue) {
            count_off++;
            continue;
        }
        count_on++;

        // Up to three quick white blinks at the start are the ball shaking
        if(i <= 3 * 3 && red && green && blue) {
            ball_shakes++;
        }

        if(red && !green && !blue) {
            count_red++;
        } else if(!red && green && !blue) {
            count_green++;
        } else if(!red && !green && blue) {
            count_blue++;
        } else if(red && green && !blue) {
            count_yellow++;
        } else if(red && green && blue) {
            count_white++;
        }
    }

    pattern->duration_ms = duration * PGP_LED_STEP_MS;
    pattern->ball_shakes = ball_shakes;

    if(!count_on) {
        pattern->event = PgpLedEventNone;
    } else if(count_white == count_on) {
        pattern->event = PgpLedEventBagFull;
    } else if(count_red == count_on) {
        // blinking red vs. solid red
        pattern->event = count_off ? PgpLedEventNoBalls : PgpLedEventBoxFull;
    } else if(count_green == count_on) {
        pattern->event = PgpLedEventPokemon;
    } else if(count_yellow == count_on) {
        pattern->event = PgpLedEventNewPokemon;
    } else if(count_blue == count_on) {
        pattern->event = PgpLedEventPokestop;
    } else if(ball_shakes) {
        if(count_blue && count_green) {
            pattern->event = PgpLedEventCaught;
        } else if(count_red) {
            pattern->event = PgpLedEventFled;
        } else {
            pattern->event = PgpLedEventUnknown;
        }
    } else if(count_red && count_green && count_blue && !count_off) {
        pattern->event = PgpLedEventItems;
    } else {
        pattern->event = PgpLedEventUnknown;
    }

    return true;
}

const char* pgp_led_event_name(PgpLedEvent event) {
    switch(event) {
    case PgpLedEventNone:
        return "-";
    case PgpLedEventPokemon:
        return "Pokemon nearby";
    case PgpLedEventNewPokemon:
        return "New Pokemon nearby";
    case PgpLedEventPokestop:
        return "Pokestop nearby";
    case PgpLedEventCaught:
        return "Caught!";
    case PgpLedEventFled:
        return "Pokemon fled";
    case PgpLedEventItems:
        return "Got items";
    case PgpLedEventBagFull:
        return "Bag is full";
    case PgpLedEventBoxFull:
        return "Pokemon box is full";
    case PgpLedEventNoBalls:
        return "Out of balls / stop gone";
    case PgpLedEventUnknown:
    default:
        return "Unknown pattern";
    }
}

void pgp_led_button_pattern(uint32_t random, uint8_t* pattern) {
    // Press starts at sample 0-5 and lasts at least PGP_BUTTON_MIN_SHIFT + 1 samples
    uint32_t press_start = random % 6;
    random /= 6;
    uint32_t press_last =
        press_start + PGP_BUTTON_MIN_SHIFT +
        random % (PGP_BUTTON_SAMPLES - press_start - PGP_BUTTON_MIN_SHIFT);

    // First sample is the most significant of the 10 bits
    uint16_t samples = 0;
    for(uint32_t i = 0; i < PGP_BUTTON_SAMPLES; i++) {
        samples <<= 1;
        if(i >= press_start && i <= press_last) {
            samples |= 1;
        }
    }

    pattern[0] = (samples >> 8) & 0x03;
    pattern[1] = samples & 0xff;
}
