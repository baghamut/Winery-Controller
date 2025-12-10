#pragma once
#include <Arduino.h>

// -------- Process / control modes --------
enum ProcessMode {
  PROCESS_OFF = 0,
  PROCESS_DISTILLER = 1,
  PROCESS_RECTIFIER = 2
};

enum ControlMode {
  CONTROL_POWER = 0,
  CONTROL_TEMP  = 1
};

// -------- Global state declarations (extern) --------
extern ProcessMode processMode;
extern ControlMode controlMode;

extern float setpointValue;
extern float distillerPower;
extern float rectifierPower;
extern bool  isRunning;

extern bool ssr1Enabled;
extern bool ssr2Enabled;
extern bool ssr3Enabled;
extern bool ssr4Enabled;
extern bool ssr5Enabled;

// Temperatures
extern float tankTemp;
extern float roomTemp;
extern float colTemp;

// PID parameters and state
extern float Kp, Ki, Kd;
extern float pidISum, pidLastErr, pidOutput;

// -------- API --------
void initState();
void resetProcessDefaults();
