// =============================================================================
//  ssr.h  –  SSR driver interface (LEDC PWM)
//
//  MASTER POWER UNIFICATION:
//    The public interface is unchanged (ssrInit / ssrSetDuty / ssrAllOff).
//    Control logic that decides WHICH SSRs get power now lives entirely in
//    control.cpp (applySsrFromState).  This file stays a pure hardware driver.
// =============================================================================
#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// ssrInit
//   Configures all 5 SSR GPIO pins as LEDC PWM outputs (ESP32 core v3 API).
//   Sets all outputs to 0 (OFF).
//   Call once from setup() before starting controlTask().
// ---------------------------------------------------------------------------
void ssrInit();

// ---------------------------------------------------------------------------
// ssrSetDuty
//   Sets the PWM duty cycle for one SSR.
//   ssr:     1–5  (maps to SSR1–SSR5 / PIN_SSR1–PIN_SSR5)
//   percent: 0.0–100.0 (clamped if out of range)
//   0 %  → GPIO stays LOW → SSR off
//   100 %→ GPIO stays HIGH → SSR fully on
// ---------------------------------------------------------------------------
void ssrSetDuty(int ssr, float percent);

// ---------------------------------------------------------------------------
// ssrAllOff
//   Forces all 5 SSR outputs to 0 immediately.
//   Used for emergency shutdown, STOP command, and mode transitions.
// ---------------------------------------------------------------------------
void ssrAllOff();
