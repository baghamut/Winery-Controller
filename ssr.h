// =============================================================================
//  ssr.h  –  SSR driver interface (LEDC PWM)
// =============================================================================
#pragma once
#include <Arduino.h>

// Initialise LEDC channels and set all SSRs to OFF.
// Call once from setup() before starting controlTask().
void ssrInit();

// Set duty for a specific SSR (1–5), value in percent 0.0–100.0.
// 0 %  → output off (GPIO low / 0 duty).
// 100 % → output fully on.
void ssrSetDuty(int ssr, float percent);

// Convenience: set all SSR outputs OFF immediately (safe shutdown).
void ssrAllOff();