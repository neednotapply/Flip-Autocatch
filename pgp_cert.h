/**
 * Pokemon GO Plus certificate crypto.
 *
 * Ported from pgpemu (https://github.com/spezifisch/pgpemu, BSD-2-Clause,
 * (c) 2018 Yohanes Nugroho, (c) 2023 spezifisch). Pure C, no Flipper
 * dependencies, so it can be unit tested on a PC (see tests/).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PGP_MAC_LEN  6
#define PGP_KEY_LEN  16
#define PGP_BLOB_LEN 256

/* Inner payload of the first challenge, encrypted with the device key */
typedef struct __attribute__((packed)) {
    uint8_t bt_addr[6];
    uint8_t key[16];
    uint8_t nonce[16];
    uint8_t encrypted_challenge[16];
    uint8_t encrypted_hash[16];
    uint8_t flash_data[10];
} PgpMainChallenge;

/* First message read by the phone from SFIDA_TO_CENTRAL (378 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t state[4];
    uint8_t nonce[16];
    uint8_t encrypted_main_challenge[80];
    uint8_t encrypted_hash[16];
    uint8_t bt_addr[6];
    uint8_t blob[PGP_BLOB_LEN];
} PgpChallenge0;

/* Follow-up challenges exchanged in both directions (52 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t state[4];
    uint8_t nonce[16];
    uint8_t encrypted_challenge[16];
    uint8_t encrypted_hash[16];
} PgpNextChallenge;

_Static_assert(sizeof(PgpMainChallenge) == 80, "PgpMainChallenge must be 80 bytes");
_Static_assert(sizeof(PgpChallenge0) == 378, "PgpChallenge0 must be 378 bytes");
_Static_assert(sizeof(PgpNextChallenge) == 52, "PgpNextChallenge must be 52 bytes");

/**
 * Build the first challenge.
 * @param mac         device MAC in display order (e.g. 98:B6:E9:..)
 * @param device_key  16 byte device key dumped from the PGP
 * @param blob        256 byte blob dumped from the PGP
 * @param the_challenge, main_nonce, main_key, outer_nonce  16 random bytes each;
 *                    main_key becomes the session key
 */
void pgp_cert_generate_chal_0(
    const uint8_t* mac,
    const uint8_t* device_key,
    const uint8_t* blob,
    const uint8_t* the_challenge,
    const uint8_t* main_nonce,
    const uint8_t* main_key,
    const uint8_t* outer_nonce,
    PgpChallenge0* output);

/** Encrypt 16 bytes of data (or the 0xAA marker block when data is NULL) */
void pgp_cert_generate_next_chal(
    const uint8_t* data,
    const uint8_t* key,
    const uint8_t* nonce,
    PgpNextChallenge* output);

/** Decrypt a 52 byte PgpNextChallenge into 16 bytes; returns true if the hash matches */
bool pgp_cert_decrypt_next(const uint8_t* data, const uint8_t* key, uint8_t* output);

/** Answer a 32 byte reconnect challenge with 16 bytes */
void pgp_cert_generate_reconnect_response(
    const uint8_t* key,
    const uint8_t* challenge,
    uint8_t* output);
