#include <Arduino.h>
#include "Config.h"
#include "State.h"
#include "Control.h"

// -------- Helpers --------
static void analogWriteCompat(uint8_t pin, float duty01) {
  uint8_t v = (uint8_t)constrain(duty01 * 255.0f, 0, 255);
  analogWrite(pin, v);
}

// -------- Public API --------
void initPins() {
  pinMode(DISTILLER_PWM1, OUTPUT);
  pinMode(DISTILLER_PWM2, OUTPUT);
  pinMode(DISTILLER_PWM3, OUTPUT);
  pinMode(RECTIFIER_PWM1, OUTPUT);
  pinMode(RECTIFIER_PWM2, OUTPUT);
}

void updateOutputs() {
  if (!isRunning) {
    analogWriteCompat(DISTILLER_PWM1, 0);
    analogWriteCompat(DISTILLER_PWM2, 0);
    analogWriteCompat(DISTILLER_PWM3, 0);
    analogWriteCompat(RECTIFIER_PWM1, 0);
    analogWriteCompat(RECTIFIER_PWM2, 0);
    return;
  }

  float perSSR = 0.0f;

  if (processMode == PROCESS_DISTILLER) {
    int enabledCount = (ssr1Enabled ? 1 : 0) +
                       (ssr2Enabled ? 1 : 0) +
                       (ssr3Enabled ? 1 : 0);
    perSSR = enabledCount > 0 ? distillerPower / enabledCount : 0.0f;

    analogWriteCompat(DISTILLER_PWM1, (ssr1Enabled ? perSSR : 0.0f) / 100.0f);
    analogWriteCompat(DISTILLER_PWM2, (ssr2Enabled ? perSSR : 0.0f) / 100.0f);
    analogWriteCompat(DISTILLER_PWM3, (ssr3Enabled ? perSSR : 0.0f) / 100.0f);

    analogWriteCompat(RECTIFIER_PWM1, 0);
    analogWriteCompat(RECTIFIER_PWM2, 0);
  }
  else if (processMode == PROCESS_RECTIFIER) {
    int enabledCount = (ssr4Enabled ? 1 : 0) +
                       (ssr5Enabled ? 1 : 0);
    perSSR = enabledCount > 0 ? rectifierPower / enabledCount : 0.0f;

    analogWriteCompat(RECTIFIER_PWM1, (ssr4Enabled ? perSSR : 0.0f) / 100.0f);
    analogWriteCompat(RECTIFIER_PWM2, (ssr5Enabled ? perSSR : 0.0f) / 100.0f);

    analogWriteCompat(DISTILLER_PWM1, 0);
    analogWriteCompat(DISTILLER_PWM2, 0);
    analogWriteCompat(DISTILLER_PWM3, 0);
  }
  else { // PROCESS_OFF or unknown
    analogWriteCompat(DISTILLER_PWM1, 0);
    analogWriteCompat(DISTILLER_PWM2, 0);
    analogWriteCompat(DISTILLER_PWM3, 0);
    analogWriteCompat(RECTIFIER_PWM1, 0);
    analogWriteCompat(RECTIFIER_PWM2, 0);
  }
}

void pidUpdate(float inputTemp) {
  float error = setpointValue - inputTemp;
  pidISum += error;
  if (pidISum > 50.0f)  pidISum = 50.0f;
  if (pidISum < -50.0f) pidISum = -50.0f;

  float dErr = error - pidLastErr;
  pidOutput = Kp * error + Ki * pidISum + Kd * dErr;
  if (pidOutput > 100.0f) pidOutput = 100.0f;
  if (pidOutput < 0.0f)   pidOutput = 0.0f;
  pidLastErr = error;
}

void updateControlLoop() {
  static unsigned long lastControl = 0;
  if (millis() - lastControl > 100) {
    if (isRunning) {
      if (processMode == PROCESS_DISTILLER) {
        if (controlMode == CONTROL_POWER) {
          distillerPower = setpointValue;
        } else { // CONTROL_TEMP
          pidUpdate(tankTemp);
          distillerPower = pidOutput;
        }
        rectifierPower = 0.0f;
      }
      else if (processMode == PROCESS_RECTIFIER) {
        if (controlMode == CONTROL_POWER) {
          rectifierPower = setpointValue;
        } else { // CONTROL_TEMP
          pidUpdate(colTemp);
          rectifierPower = pidOutput;
        }
        distillerPower = 0.0f;
      }
    } else {
      distillerPower = 0.0f;
      rectifierPower = 0.0f;
    }

    updateOutputs();
    lastControl = millis();
  }
}
