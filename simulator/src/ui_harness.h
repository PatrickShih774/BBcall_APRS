#ifndef UI_HARNESS_H
#define UI_HARNESS_H
#include <stdint.h>
void ui_init(void);
void ui_handle_key(int key);
void ui_tick(uint32_t ms);
#endif