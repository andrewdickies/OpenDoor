#pragma once

#include <stddef.h>
#include <stdbool.h>

typedef void (*door_control_state_cb_t)(const char *source, bool doorStatus, int openCount);

void door_control_set_state_callback(door_control_state_cb_t cb);
void door_control_open(const char *source);
void door_control_close(const char *source);
int door_control_build_status_json(char *out, size_t outSize);
