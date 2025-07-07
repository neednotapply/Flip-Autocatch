#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool pgp_is_connected(void);      // true while the phone stays paired
bool pgp_press_requested(void);   // true when Go asks for a button press
void pgp_send_button_press(void); // send the press-release packet

#ifdef __cplusplus
}
#endif
