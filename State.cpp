#include "State.h"
#include "Config.h"
#include "Control.h"

// -------- Global state definitions --------
ProcessMode processMode = PROCESS_OFF;
ControlMode controlMode = CONTROL_POWER;

float setpointValue   = 0.0f;
float distillerPower  = 0.0f;
float rectifierPower  = 0.0f;
bool  isRunning       = false;

bool ssr1Enabled = false;
bool ssr2Enabled = false;
bool ssr3Enabled = false;
bool ssr4Enabled = false;
bool ssr5Enabled = false;

float tankTemp = 0.0f;
float roomTemp = 0.0f;
float colTemp  = 0.0f;

float Kp = 0.3f, Ki = 0.05f, Kd = 2.0f;
float pidISum = 0.0f, pidLastErr = 0.0f, pidOutput = 0.0f;

void initState() {
  processMode    = PROCESS_OFF;
  controlMode    = CONTROL_POWER;
  setpointValue  = 0.0f;
  distillerPower = 0.0f;
  rectifierPower = 0.0f;
  isRunning      = false;

  ssr1Enabled = ssr2Enabled = ssr3Enabled = false;
  ssr4Enabled = ssr5Enabled = false;

  pidISum = 0.0f;
  pidLastErr = 0.0f;
  pidOutput = 0.0f;
}

void resetProcessDefaults() {
  ssr1Enabled = ssr2Enabled = ssr3Enabled = false;
  ssr4Enabled = ssr5Enabled = false;
  setpointValue  = 0.0f;
  isRunning      = false;
  pidISum        = 0.0f;
  pidLastErr     = 0.0f;
  distillerPower = 0.0f;
  rectifierPower = 0.0f;

  Serial.println("Process defaults reset");
}
