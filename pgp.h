
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool pgp_init(void);
bool pgp_is_connected(void);
bool pgp_press_requested(void);
void pgp_send_button_press(void);
void pgp_restart_advertising(void);
void pgp_generate_challenge(uint8_t* out);
