// SubstepEncoder basic example: print position and speed.
//
// Connect the two encoder phases to two consecutive GPIOs (here: 2 and 3)
// and pass the lower-numbered one to begin().
//
// There is no update() to call: the PIO tracks the encoder in hardware and
// the getters refresh themselves. Read as fast or as slow as you like.

#include <SubstepEncoder.h>

SubstepEncoder encoder;

void setup() {
  Serial.begin(115200);

  if (encoder.begin(2) != 0) {
    Serial.println("No free PIO block for the encoder");
    while (1);
  }

  // quadrature steps per revolution = PPR x 4 (100 PPR encoder -> 400).
  // This enables the unit helpers used below; the raw substep API
  // (position() / speed()) works without it
  encoder.setStepsPerRev(400);
}

void loop() {
  // raw, consistent reading in substeps (64 substeps per step)
  SubstepEncoder::Snapshot s = encoder.read();

  Serial.print("Position [substeps]: ");
  Serial.print((long)s.position);
  Serial.print(" | [rev]: ");
  Serial.print(encoder.revolutions(), 4);
  Serial.print(" | Speed [rpm]: ");
  Serial.print(encoder.rpm(), 2);
  Serial.print(" | stopped: ");
  Serial.println(encoder.stopped());

  delay(10);
}
