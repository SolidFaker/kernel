#ifndef DRIVERS_TIMER_H
#define DRIVERS_TIMER_H

// PIT frequency programmed in init_timer(); tick/time math relies on it
#define TIMER_HZ 2000

#include "common.h"

void init_timer(u32 frequency);

extern volatile u32 tick;

#endif
