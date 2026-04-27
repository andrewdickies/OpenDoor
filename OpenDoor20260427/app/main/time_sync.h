#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

void time_sync_start_once(void);
bool time_sync_is_valid(void);
time_t time_sync_now(void);
void time_sync_format_local(char *out, size_t outSize);
