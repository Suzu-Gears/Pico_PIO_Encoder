// Pico_PIO_Encoder phase calibration example.
//
// Real encoders have slightly unequal phase sizes, which adds ripple to the
// speed estimate. With auto calibration enabled, the library learns the
// phase sizes from normal readings. Spin the encoder at a steady speed
// (each step must be faster than 20ms) until calibration is ready, then
// save the result and hardcode it with setPhases() to skip the procedure
// on the next boot.

#include <Pico_PIO_Encoder.h>

Pico_PIO_Encoder encoder;

void setup() {
  Serial.begin(115200);

  if (encoder.beginConsecutive(2, 3) != 0) {
    Serial.println("No free PIO block for the encoder");
    while (1);
  }

  // if you already calibrated once, restore the result instead:
  // encoder.setPhases(0x404040);

  encoder.enableAutoCalibration();
  Serial.println("Spin the encoder at a steady speed...");
}

void loop() {
  static bool reported = false;

  // reading also feeds the calibration while it is enabled
  int32_t speed = encoder.speed();

  if (!reported && encoder.calibrationReady()) {
    reported = true;
    Serial.print("Calibration done. Save this value: encoder.setPhases(0x");
    Serial.print(encoder.getPhases(), HEX);
    Serial.println(");");
  }

  Serial.print("Speed [substeps/s]: ");
  Serial.println(speed);
  delay(10);
}
