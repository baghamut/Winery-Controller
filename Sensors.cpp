#include <OneWire.h>
#include <DallasTemperature.h>
#include "Config.h"
#include "State.h"

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Your addresses
DeviceAddress tankAddr = {0xF9,0x00,0x00,0x00,0x59,0x1C,0xC2,0x28};
DeviceAddress roomAddr = {0x59,0x00,0x00,0x00,0x55,0xEF,0x57,0x28};
DeviceAddress colAddr  = {0xFA,0x00,0x00,0x00,0xBC,0xD3,0x53,0x28};

void initSensors() {
  sensors.begin();
  sensors.setResolution(12);
  sensors.setWaitForConversion(false);
}

void updateTemperatures() {
  static unsigned long lastTemp = 0;
  if (millis() - lastTemp > 750) {
    sensors.requestTemperatures();
    tankTemp = sensors.getTempC(tankAddr);
    roomTemp = sensors.getTempC(roomAddr);
    colTemp  = sensors.getTempC(colAddr);
    lastTemp = millis();
  }
}
