#pragma once

void time_sync_init(void);     // call this during setup
bool time_is_valid(void);      // to check if time was synced
void print_current_time(void); // (optional) for debugging