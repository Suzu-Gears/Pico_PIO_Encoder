// SubstepEncoder index input example: homing with a Z phase or limit switch.
//
// The index input latches the encoder position at the moment of an edge on
// any GPIO. Typical uses:
//  - encoder Z/index phase  -> absolute position within one revolution
//  - limit switch on a linear axis -> homing, re-checked on every hit
//
// Wiring in this example: encoder A/B on GPIO2+3, index signal on GPIO4.

#include <SubstepEncoder.h>

SubstepEncoder encoder;

void setup() {
  Serial.begin(115200);

  if (encoder.begin(2) != 0) {
    Serial.println("No free PIO block for the encoder");
    while (1);
  }

  // Z phase, rising edge, clean signal (no debounce):
  encoder.attachIndex(4);

  // For a mechanical limit switch to GND instead, use falling edge with
  // pull-up and a debounce time:
  // encoder.attachIndex(4, false, true, 10000);

  // home: zero the position at the first index event
  encoder.zeroOnNextIndex();
  Serial.println("Waiting for the first index event...");
}

void loop() {
  SubstepEncoder::Snapshot s = encoder.read();

  Serial.print("Position: ");
  Serial.print((long)s.position);
  Serial.print(" | homed: ");
  Serial.print(encoder.zeroPending() ? "no " : "yes");
  Serial.print(" | index count: ");
  Serial.print(encoder.indexCount());
  Serial.print(" | last index at: ");
  // after homing this stays near a multiple of one revolution for a Z
  // phase, or near 0 for a limit switch -- drift here means lost steps
  Serial.println((long)encoder.lastIndexPosition());

  delay(50);
}
