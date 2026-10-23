#include "pgp_handshake.h"

#include <string.h>

static void pgp_handshake_command(PgpHandshakeOutput* output, uint8_t b0, uint8_t b2) {
    output->notify = true;
    memset(output->command, 0, sizeof(output->command));
    output->command[0] = b0;
    output->command[2] = b2;
}

void pgp_handshake_init(
    PgpHandshake* handshake,
    const uint8_t* mac,
    const uint8_t* device_key,
    const uint8_t* blob,
    PgpRandomFill random_fill) {
    memset(handshake, 0, sizeof(PgpHandshake));
    handshake->mac = mac;
    handshake->device_key = device_key;
    handshake->blob = blob;
    handshake->random_fill = random_fill;
}

void pgp_handshake_reset(PgpHandshake* handshake) {
    // Like pgpemu, forget the reconnect key too: a fresh full handshake on every
    // connection is the path known to work with current app versions.
    handshake->state = PgpHandshakeStateChallenge;
    handshake->has_reconnect_key = false;
}

bool pgp_handshake_on_notify(
    PgpHandshake* handshake,
    bool enabled,
    PgpHandshakeOutput* output) {
    memset(output, 0, sizeof(PgpHandshakeOutput));
    if(!enabled) {
        return false;
    }

    if(handshake->has_reconnect_key) {
        memset(handshake->cert_buffer, 0, 36);
        handshake->cert_buffer[0] = 3;
        memcpy(handshake->cert_buffer + 4, handshake->reconnect_challenge, 32);
        output->cert_len = 36;
        pgp_handshake_command(output, 3, 0);
        handshake->state = PgpHandshakeStateReconnect;
    } else {
        handshake->random_fill(handshake->the_challenge, 16);
        handshake->random_fill(handshake->main_nonce, 16);
        handshake->random_fill(handshake->session_key, 16);
        handshake->random_fill(handshake->outer_nonce, 16);

        pgp_cert_generate_chal_0(
            handshake->mac,
            handshake->device_key,
            handshake->blob,
            handshake->the_challenge,
            handshake->main_nonce,
            handshake->session_key,
            handshake->outer_nonce,
            (PgpChallenge0*)handshake->cert_buffer);
        output->cert_len = sizeof(PgpChallenge0);
        pgp_handshake_command(output, 0, 0);
        handshake->state = PgpHandshakeStateChallenge;
    }

    return true;
}

bool pgp_handshake_on_write(
    PgpHandshake* handshake,
    const uint8_t* data,
    size_t len,
    PgpHandshakeOutput* output) {
    memset(output, 0, sizeof(PgpHandshakeOutput));

    switch(handshake->state) {
    case PgpHandshakeStateChallenge:
        // Phone answered the first challenge; the reply is checked by Niantic, not us
        if(len != 20) {
            return false;
        }
        handshake->random_fill(handshake->state_0_nonce, 16);
        memset(handshake->cert_buffer, 0, sizeof(PgpNextChallenge));
        pgp_cert_generate_next_chal(
            NULL,
            handshake->session_key,
            handshake->state_0_nonce,
            (PgpNextChallenge*)handshake->cert_buffer);
        handshake->cert_buffer[0] = 1;
        output->cert_len = sizeof(PgpNextChallenge);
        pgp_handshake_command(output, 1, 0);
        handshake->state = PgpHandshakeStateNextChallenge;
        return true;

    case PgpHandshakeStateNextChallenge:
        // Decrypt the challenge from the app and hand it back
        if(len < sizeof(PgpNextChallenge)) {
            return false;
        }
        memset(handshake->cert_buffer, 0, 20);
        pgp_cert_decrypt_next(data, handshake->session_key, handshake->cert_buffer + 4);
        handshake->cert_buffer[0] = 2;
        output->cert_len = 20;
        pgp_handshake_command(output, 2, 0);
        handshake->state = PgpHandshakeStateConfirm;
        return true;

    case PgpHandshakeStateConfirm:
        // Final confirmation from the app; its content is not needed
        if(len < sizeof(PgpNextChallenge)) {
            return false;
        }
        handshake->random_fill(handshake->reconnect_challenge, 32);
        handshake->has_reconnect_key = true;
        pgp_handshake_command(output, 4, 0x23);
        handshake->state = PgpHandshakeStateDone;
        return true;

    case PgpHandshakeStateReconnect:
        if(len != 20) {
            return false;
        }
        pgp_handshake_command(output, 4, 0x01);
        handshake->state = PgpHandshakeStateReconnectResponse;
        return true;

    case PgpHandshakeStateReconnectResponse:
        if(len < 36) {
            return false;
        }
        memset(handshake->cert_buffer, 0, 20);
        pgp_cert_generate_reconnect_response(
            handshake->session_key, data + 4, handshake->cert_buffer + 4);
        handshake->cert_buffer[0] = 5;
        output->cert_len = 20;
        pgp_handshake_command(output, 5, 0);
        handshake->state = PgpHandshakeStateReconnectConfirm;
        return true;

    case PgpHandshakeStateReconnectConfirm:
        if(len != 5) {
            return false;
        }
        pgp_handshake_command(output, 4, 0x02);
        handshake->state = PgpHandshakeStateDone;
        return true;

    case PgpHandshakeStateDone:
    default:
        return false;
    }
}

bool pgp_handshake_is_done(const PgpHandshake* handshake) {
    return handshake->state == PgpHandshakeStateDone;
}
