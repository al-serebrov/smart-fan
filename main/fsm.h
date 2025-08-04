// fsm.h
#ifndef FSM_H
#define FSM_H

#include <stdbool.h>
#include <stdint.h>
#include "ssd1306.h"

typedef enum {
    IDLE,
    COOLING,
    WAITING,
    FORCE
} fsm_state_t;

typedef struct {
    fsm_state_t state;
    uint32_t last_transition_time;
    uint32_t humidity_above_threshold_time;
    float threshold;
    bool fan_on;
} fsm_t;

void fsm_init(void);
void fsm_update(float humidity);
fsm_state_t fsm_get_state(void);
bool fsm_is_fan_on(void);
void fsm_get_display_lines(char *fan_line, char *timer_line, char *state_line);
void fsm_set_manual_override(bool fan_state);
uint8_t *fsm_get_state_icon(void);


#endif