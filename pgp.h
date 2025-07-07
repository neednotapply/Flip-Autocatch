
#pragma once
#include <stdbool.h>

bool pgp_init(void);
bool pgp_is_connected(void);
bool pgp_press_requested(void);
void pgp_send_button_press(void);
