/**
 * Pokemon GO Plus certificate handshake state machine.
 *
 * Port of pgpemu's pgp_handshake.c. Transport agnostic: the caller feeds in
 * GATT events and gets back what to publish on SFIDA_TO_CENTRAL (read by the
 * phone) and what to notify on SFIDA_COMMANDS.
 */
#pragma once

#include "pgp_cert.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PGP_CERT_BUFFER_LEN sizeof(PgpChallenge0)

typedef void (*PgpRandomFill)(uint8_t* buffer, size_t len);

typedef enum {
    PgpHandshakeStateChallenge = 0, // sent first challenge, waiting for the phone
    PgpHandshakeStateNextChallenge = 1,
    PgpHandshakeStateConfirm = 2,
    PgpHandshakeStateReconnect = 3,
    PgpHandshakeStateReconnectResponse = 4,
    PgpHandshakeStateReconnectConfirm = 5,
    PgpHandshakeStateDone = 6,
} PgpHandshakeState;

typedef struct {
    const uint8_t* mac;
    const uint8_t* device_key;
    const uint8_t* blob;
    PgpRandomFill random_fill;

    PgpHandshakeState state;
    bool has_reconnect_key;

    uint8_t cert_buffer[PGP_CERT_BUFFER_LEN];
    uint8_t the_challenge[16];
    uint8_t main_nonce[16];
    uint8_t outer_nonce[16];
    uint8_t session_key[16];
    uint8_t state_0_nonce[16];
    uint8_t reconnect_challenge[32];
} PgpHandshake;

typedef struct {
    // Publish handshake->cert_buffer[0..cert_len) on SFIDA_TO_CENTRAL when > 0
    uint16_t cert_len;
    // Send command on SFIDA_COMMANDS when true
    bool notify;
    uint8_t command[4];
} PgpHandshakeOutput;

void pgp_handshake_init(
    PgpHandshake* handshake,
    const uint8_t* mac,
    const uint8_t* device_key,
    const uint8_t* blob,
    PgpRandomFill random_fill);

/** Forget the connection state. Call on disconnect. */
void pgp_handshake_reset(PgpHandshake* handshake);

/** Phone wrote the SFIDA_COMMANDS CCCD. Returns true if output must be sent. */
bool pgp_handshake_on_notify(
    PgpHandshake* handshake,
    bool enabled,
    PgpHandshakeOutput* output);

/** Phone wrote CENTRAL_TO_SFIDA. Returns true if output must be sent. */
bool pgp_handshake_on_write(
    PgpHandshake* handshake,
    const uint8_t* data,
    size_t len,
    PgpHandshakeOutput* output);

bool pgp_handshake_is_done(const PgpHandshake* handshake);
