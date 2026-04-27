#pragma once

typedef void (*boot_button_press_cb_t)(void);

void boot_button_start(boot_button_press_cb_t onShortPress, boot_button_press_cb_t onLongPress);
