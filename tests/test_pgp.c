/**
 * Host tests for the protocol logic (crypto, handshake, LED parsing).
 * Run with: make -C tests
 *
 * Known-answer checksums were produced by pgpemu's reference implementation
 * with the same fixed inputs; they use dummy keys, not real device secrets.
 */
#include "../pgp_cert.h"
#include "../pgp_handshake.h"
#include "../pgp_led.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if(!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            failures++;                                                \
        }                                                              \
    } while(0)

static uint32_t fnv1a(const void* data, size_t len) {
    const uint8_t* bytes = data;
    uint32_t hash = 2166136261u;
    while(len--) {
        hash ^= *bytes++;
        hash *= 16777619u;
    }
    return hash;
}

static const uint8_t test_mac[PGP_MAC_LEN] = {0x98, 0xB6, 0xE9, 0x00, 0x00, 0x01};
static uint8_t test_key[PGP_KEY_LEN];
static uint8_t test_blob[PGP_BLOB_LEN];

static void test_setup(void) {
    for(int i = 0; i < PGP_KEY_LEN; i++) test_key[i] = i;
    for(int i = 0; i < PGP_BLOB_LEN; i++) test_blob[i] = 255 - i;
}

static void test_cert_known_answers(void) {
    uint8_t chal[16], main_nonce[16], main_key[16], outer_nonce[16], reconnect[32];
    memset(chal, 0x41, 16);
    memset(main_nonce, 0x42, 16);
    memset(main_key, 0x43, 16);
    memset(outer_nonce, 0x44, 16);
    for(int i = 0; i < 32; i++) reconnect[i] = 0x80 + i;

    PgpChallenge0 chal_0;
    pgp_cert_generate_chal_0(
        test_mac, test_key, test_blob, chal, main_nonce, main_key, outer_nonce, &chal_0);
    CHECK(fnv1a(&chal_0, sizeof(chal_0)) == 0xcf1b412c);
    CHECK(chal_0.bt_addr[0] == 0x01 && chal_0.bt_addr[5] == 0x98); // reversed MAC
    CHECK(memcmp(chal_0.blob, test_blob, PGP_BLOB_LEN) == 0);

    PgpNextChallenge next;
    memset(&next, 0, sizeof(next));
    pgp_cert_generate_next_chal(NULL, main_key, outer_nonce, &next);
    CHECK(fnv1a(&next, sizeof(next)) == 0xa4225be0);

    uint8_t response[16];
    pgp_cert_generate_reconnect_response(main_key, reconnect, response);
    CHECK(fnv1a(response, sizeof(response)) == 0xf8fc8aab);
}

static void test_cert_roundtrip(void) {
    uint8_t key[16], nonce[16], data[16], out[16];
    memset(key, 0x11, 16);
    memset(nonce, 0x22, 16);
    for(int i = 0; i < 16; i++) data[i] = i * 7;

    PgpNextChallenge chal;
    pgp_cert_generate_next_chal(data, key, nonce, &chal);
    CHECK(pgp_cert_decrypt_next((const uint8_t*)&chal, key, out));
    CHECK(memcmp(out, data, 16) == 0);

    chal.encrypted_challenge[3] ^= 0x01;
    CHECK(!pgp_cert_decrypt_next((const uint8_t*)&chal, key, out));
}

static uint8_t fake_random_counter = 0;

static void fake_random_fill(uint8_t* buffer, size_t len) {
    for(size_t i = 0; i < len; i++) buffer[i] = fake_random_counter++;
}

static void test_handshake_full(void) {
    PgpHandshake hs;
    PgpHandshakeOutput out;
    fake_random_counter = 0;
    pgp_handshake_init(&hs, test_mac, test_key, test_blob, fake_random_fill);

    // Notifications enabled: first challenge
    CHECK(pgp_handshake_on_notify(&hs, true, &out));
    CHECK(out.cert_len == 378);
    CHECK(out.notify && out.command[0] == 0);
    CHECK(memcmp(((PgpChallenge0*)hs.cert_buffer)->blob, test_blob, PGP_BLOB_LEN) == 0);
    CHECK(hs.state == PgpHandshakeStateChallenge);

    // Wrong length is ignored
    uint8_t reply[52] = {0};
    CHECK(!pgp_handshake_on_write(&hs, reply, 19, &out));

    // Phone answers with 20 bytes: device sends 0xAA marker encrypted with the session key
    CHECK(pgp_handshake_on_write(&hs, reply, 20, &out));
    CHECK(out.cert_len == 52 && hs.cert_buffer[0] == 1);
    CHECK(out.notify && out.command[0] == 1);
    uint8_t marker[16];
    CHECK(pgp_cert_decrypt_next(hs.cert_buffer, hs.session_key, marker));
    CHECK(marker[0] == 0xaa && marker[1] == 0);

    // Phone sends its challenge, device must decrypt and return it
    uint8_t phone_chal[16], phone_nonce[16];
    memset(phone_chal, 0x5a, 16);
    memset(phone_nonce, 0x33, 16);
    PgpNextChallenge from_phone;
    pgp_cert_generate_next_chal(phone_chal, hs.session_key, phone_nonce, &from_phone);
    CHECK(!pgp_handshake_on_write(&hs, (uint8_t*)&from_phone, 20, &out));
    CHECK(pgp_handshake_on_write(&hs, (uint8_t*)&from_phone, sizeof(from_phone), &out));
    CHECK(out.cert_len == 20 && hs.cert_buffer[0] == 2);
    CHECK(memcmp(hs.cert_buffer + 4, phone_chal, 16) == 0);
    CHECK(out.notify && out.command[0] == 2);

    // Confirmation
    CHECK(pgp_handshake_on_write(&hs, (uint8_t*)&from_phone, sizeof(from_phone), &out));
    CHECK(out.cert_len == 0);
    CHECK(out.command[0] == 4 && out.command[2] == 0x23);
    CHECK(pgp_handshake_is_done(&hs));

    // Nothing else is expected once done
    CHECK(!pgp_handshake_on_write(&hs, reply, 20, &out));

    // Reconnect path (only reachable if the key is kept between connections)
    uint8_t session_key[16];
    memcpy(session_key, hs.session_key, 16);
    CHECK(pgp_handshake_on_notify(&hs, true, &out));
    CHECK(out.cert_len == 36 && hs.cert_buffer[0] == 3 && out.command[0] == 3);
    CHECK(pgp_handshake_on_write(&hs, reply, 20, &out));
    CHECK(out.cert_len == 0 && out.command[0] == 4 && out.command[2] == 0x01);
    uint8_t recon[36] = {0};
    for(int i = 0; i < 32; i++) recon[4 + i] = i;
    CHECK(pgp_handshake_on_write(&hs, recon, sizeof(recon), &out));
    uint8_t expected[16];
    pgp_cert_generate_reconnect_response(session_key, recon + 4, expected);
    CHECK(out.cert_len == 20 && hs.cert_buffer[0] == 5);
    CHECK(memcmp(hs.cert_buffer + 4, expected, 16) == 0);
    CHECK(pgp_handshake_on_write(&hs, reply, 5, &out));
    CHECK(out.command[0] == 4 && out.command[2] == 0x02);
    CHECK(pgp_handshake_is_done(&hs));

    // Disconnect forgets the reconnect key: next connection is a full handshake
    pgp_handshake_reset(&hs);
    CHECK(!pgp_handshake_is_done(&hs));
    CHECK(pgp_handshake_on_notify(&hs, true, &out));
    CHECK(out.cert_len == 378);

    // Disabling notifications sends nothing
    CHECK(!pgp_handshake_on_notify(&hs, false, &out));
}

/* LED step: duration in 50 ms units, 4 bit red/green/blue */
static void led_step(uint8_t* step, uint8_t duration, uint8_t r, uint8_t g, uint8_t b) {
    step[0] = duration;
    step[1] = (g << 4) | r;
    step[2] = b;
}

static PgpLedEvent led_parse_steps(const uint8_t (*steps)[3], size_t count) {
    uint8_t buffer[4 + 31 * 3] = {0};
    buffer[3] = count;
    memcpy(buffer + 4, steps, count * 3);
    PgpLedPattern pattern;
    if(!pgp_led_parse(buffer, 4 + count * 3, &pattern)) return (PgpLedEvent)-1;
    return pattern.event;
}

static void test_led_patterns(void) {
    uint8_t s[12][3];

    for(int i = 0; i < 6; i++) led_step(s[i], 5, 0, i % 2 ? 0 : 0xf, 0);
    CHECK(led_parse_steps(s, 6) == PgpLedEventPokemon);

    for(int i = 0; i < 6; i++) led_step(s[i], 5, i % 2 ? 0 : 0xf, i % 2 ? 0 : 0xf, 0);
    CHECK(led_parse_steps(s, 6) == PgpLedEventNewPokemon);

    for(int i = 0; i < 6; i++) led_step(s[i], 5, 0, 0, i % 2 ? 0 : 0xf);
    CHECK(led_parse_steps(s, 6) == PgpLedEventPokestop);

    for(int i = 0; i < 4; i++) led_step(s[i], 5, 8, 8, 8);
    CHECK(led_parse_steps(s, 4) == PgpLedEventBagFull);

    for(int i = 0; i < 4; i++) led_step(s[i], 5, i % 2 ? 0 : 0xf, 0, 0);
    CHECK(led_parse_steps(s, 4) == PgpLedEventNoBalls);

    led_step(s[0], 20, 0xf, 0, 0);
    CHECK(led_parse_steps(s, 1) == PgpLedEventBoxFull);

    led_step(s[0], 0, 0, 0, 0);
    CHECK(led_parse_steps(s, 1) == PgpLedEventNone);

    // Ball shakes (white blinks) followed by the result colors
    for(int i = 0; i < 9; i += 3) {
        led_step(s[i], 3, 8, 8, 8);
        led_step(s[i + 1], 9, 0, 0, 0);
        led_step(s[i + 2], 16, 0, 0, 0);
    }
    led_step(s[9], 10, 0, 0xf, 0);
    led_step(s[10], 10, 0, 0, 0xf);
    CHECK(led_parse_steps(s, 11) == PgpLedEventCaught);
    led_step(s[9], 10, 0xf, 0, 0);
    led_step(s[10], 10, 0xf, 0, 0);
    CHECK(led_parse_steps(s, 11) == PgpLedEventFled);

    // Pokestop items: green/red/blue cycling without off steps
    for(int i = 0; i < 6; i++) led_step(s[i], 4, i % 3 == 1 ? 0xf : 0, i % 3 == 0 ? 0xf : 0, i % 3 == 2 ? 0xf : 0);
    CHECK(led_parse_steps(s, 6) == PgpLedEventItems);

    // Duration is in 50 ms units
    PgpLedPattern pattern;
    uint8_t buffer[4 + 3 * 2] = {0, 0, 0, 2, 10, 0xf0, 0, 30, 0, 0};
    CHECK(pgp_led_parse(buffer, sizeof(buffer), &pattern));
    CHECK(pattern.duration_ms == 2000);

    // Truncated writes are rejected, not half-parsed
    CHECK(!pgp_led_parse(buffer, sizeof(buffer) - 1, &pattern));
    CHECK(!pgp_led_parse(buffer, 3, &pattern));
}

static void test_button_pattern(void) {
    for(uint32_t r = 0; r < 100000; r += 7) {
        uint8_t p[2];
        pgp_led_button_pattern(r * 2654435761u, p);
        uint16_t samples = (p[0] << 8) | p[1];
        CHECK((samples & ~0x3ff) == 0);

        // One contiguous press, 5-10 samples long, starting within the first 6
        int first = -1, last = -1, count = 0;
        for(int i = 0; i < 10; i++) {
            if(samples & (1 << (9 - i))) {
                if(first < 0) first = i;
                last = i;
                count++;
            }
        }
        CHECK(first >= 0 && first <= 5);
        CHECK(count == last - first + 1);
        CHECK(count >= 5 && count <= 10);
        if(failures) break;
    }
}

int main(void) {
    test_setup();
    test_cert_known_answers();
    test_cert_roundtrip();
    test_handshake_full();
    test_led_patterns();
    test_button_pattern();

    if(failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All tests passed\n");
    return 0;
}
