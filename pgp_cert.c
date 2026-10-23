#include "pgp_cert.h"
#include "aes.h"

#include <string.h>

/* Unknown 10 bytes of PGP flash, zeros work */
static const uint8_t pgp_flash_data[10] = {0};

static void pgp_aes_encrypt(const struct AES_ctx* ctx, const uint8_t* input, uint8_t* output) {
    memcpy(output, input, 16);
    AES_ECB_encrypt(ctx, output);
}

static void pgp_init_nonce_hash(const uint8_t* nonce, int datalen, uint8_t* nonce_hash) {
    nonce_hash[0] = 57;
    memcpy(nonce_hash + 1, nonce, 13);
    nonce_hash[14] = (datalen >> 8) & 0xff;
    nonce_hash[15] = datalen & 0xff;
}

static void pgp_init_nonce_ctr(const uint8_t* nonce, uint8_t* nonce_ctr) {
    nonce_ctr[0] = 1;
    memcpy(nonce_ctr + 1, nonce, 13);
    nonce_ctr[14] = 0;
    nonce_ctr[15] = 0;
}

/* CBC-MAC over count bytes (count must be a multiple of 16) */
static void pgp_aes_hash(
    const struct AES_ctx* ctx,
    const uint8_t* nonce,
    const uint8_t* data,
    int count,
    uint8_t* output) {
    uint8_t nonce_hash[16];
    uint8_t tmp[16];
    uint8_t tmp2[16];

    pgp_init_nonce_hash(nonce, count, nonce_hash);
    pgp_aes_encrypt(ctx, nonce_hash, tmp);

    for(int i = 0; i < count / 16; i++) {
        for(int j = 0; j < 16; j++) {
            tmp[j] ^= data[i * 16 + j];
        }
        memcpy(tmp2, tmp, 16);
        pgp_aes_encrypt(ctx, tmp2, tmp);
    }
    memcpy(output, tmp, 16);
}

/* Encrypt the hash with counter block 0 */
static void pgp_encrypt_block(
    const struct AES_ctx* ctx,
    const uint8_t* hash,
    const uint8_t* nonce,
    uint8_t* output) {
    uint8_t nonce_ctr[16];
    uint8_t tmp[16];

    pgp_init_nonce_ctr(nonce, nonce_ctr);
    pgp_aes_encrypt(ctx, nonce_ctr, tmp);
    for(int i = 0; i < 16; i++) {
        output[i] = tmp[i] ^ hash[i];
    }
}

static void pgp_inc_ctr(uint8_t* ctr) {
    ctr[15]++;
    if(ctr[15] == 0) {
        ctr[14]++;
    }
}

/* CTR mode starting at counter block 1 (count must be a multiple of 16) */
static void pgp_aes_ctr(
    const struct AES_ctx* ctx,
    const uint8_t* nonce,
    const uint8_t* data,
    int count,
    uint8_t* output) {
    uint8_t ctr[16];
    uint8_t ectr[16];

    pgp_init_nonce_ctr(nonce, ctr);
    for(int i = 0; i < count / 16; i++) {
        pgp_inc_ctr(ctr);
        pgp_aes_encrypt(ctx, ctr, ectr);
        for(int j = 0; j < 16; j++) {
            output[i * 16 + j] = ectr[j] ^ data[i * 16 + j];
        }
    }
}

void pgp_cert_generate_chal_0(
    const uint8_t* mac,
    const uint8_t* device_key,
    const uint8_t* blob,
    const uint8_t* the_challenge,
    const uint8_t* main_nonce,
    const uint8_t* main_key,
    const uint8_t* outer_nonce,
    PgpChallenge0* output) {
    uint8_t revmac[PGP_MAC_LEN];
    uint8_t tmp_hash[16];
    struct AES_ctx ctx;
    PgpMainChallenge main_data;

    for(int i = 0; i < PGP_MAC_LEN; i++) {
        revmac[i] = mac[PGP_MAC_LEN - 1 - i];
    }

    // Inner layer, encrypted with the session key
    memcpy(main_data.bt_addr, revmac, sizeof(revmac));
    memcpy(main_data.key, main_key, 16);
    memcpy(main_data.nonce, main_nonce, 16);
    memcpy(main_data.flash_data, pgp_flash_data, sizeof(pgp_flash_data));

    AES_init_ctx(&ctx, main_key);
    pgp_aes_ctr(&ctx, main_data.nonce, the_challenge, 16, main_data.encrypted_challenge);
    pgp_aes_hash(&ctx, main_data.nonce, the_challenge, 16, tmp_hash);
    pgp_encrypt_block(&ctx, tmp_hash, main_data.nonce, main_data.encrypted_hash);

    // Outer layer, encrypted with the device key
    memset(output->state, 0, sizeof(output->state));
    memcpy(output->nonce, outer_nonce, 16);
    memcpy(output->bt_addr, revmac, sizeof(revmac));
    memcpy(output->blob, blob, PGP_BLOB_LEN);

    AES_init_ctx(&ctx, device_key);
    pgp_aes_hash(&ctx, output->nonce, (const uint8_t*)&main_data, sizeof(main_data), tmp_hash);
    pgp_encrypt_block(&ctx, tmp_hash, output->nonce, output->encrypted_hash);
    pgp_aes_ctr(
        &ctx,
        output->nonce,
        (const uint8_t*)&main_data,
        sizeof(main_data),
        output->encrypted_main_challenge);
}

void pgp_cert_generate_next_chal(
    const uint8_t* data,
    const uint8_t* key,
    const uint8_t* nonce,
    PgpNextChallenge* output) {
    struct AES_ctx ctx;
    uint8_t block[16];
    uint8_t tmp_hash[16];

    if(data) {
        memcpy(block, data, 16);
    } else {
        memset(block, 0, 16);
        block[0] = 0xaa;
    }

    memcpy(output->nonce, nonce, 16);

    AES_init_ctx(&ctx, key);
    pgp_aes_ctr(&ctx, output->nonce, block, 16, output->encrypted_challenge);
    pgp_aes_hash(&ctx, output->nonce, block, 16, tmp_hash);
    pgp_encrypt_block(&ctx, tmp_hash, output->nonce, output->encrypted_hash);
}

bool pgp_cert_decrypt_next(const uint8_t* data, const uint8_t* key, uint8_t* output) {
    struct AES_ctx ctx;
    PgpNextChallenge chal;
    uint8_t expected_hash[16];
    uint8_t actual_hash[16];

    memcpy(&chal, data, sizeof(chal));

    AES_init_ctx(&ctx, key);
    pgp_aes_ctr(&ctx, chal.nonce, chal.encrypted_challenge, 16, output);
    pgp_encrypt_block(&ctx, chal.encrypted_hash, chal.nonce, expected_hash);
    pgp_aes_hash(&ctx, chal.nonce, output, 16, actual_hash);

    return memcmp(actual_hash, expected_hash, 16) == 0;
}

void pgp_cert_generate_reconnect_response(
    const uint8_t* key,
    const uint8_t* challenge,
    uint8_t* output) {
    struct AES_ctx ctx;

    AES_init_ctx(&ctx, key);
    pgp_aes_encrypt(&ctx, challenge, output);
    for(int i = 0; i < 16; i++) {
        output[i] ^= challenge[i + 16];
    }
}
