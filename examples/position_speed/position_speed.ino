// SubstepEncoder basic example: print position and speed.
//
// Connect the two encoder phases to two consecutive GPIOs (here: 2 and 3)
// and pass the lower-numbered one to begin().
//
// There is no update() to call: the PIO tracks the encoder in hardware and
// the getters refresh themselves. Read as fast or as slow as you like.

#include <SubstepEncoder.h>

SubstepEncoder encoder;

// substeps per revolution for a 100 PPR encoder counted 4x: 100 * 4 * 64
const float SUBSTEPS_PER_REV = 100 * 4 * 64.0f;

void setup() {
  Serial.begin(115200);

  if (encoder.begin(2) != 0) {
    Serial.println("No free PIO block for the encoder");
    while (1);
  }
}

void loop() {
  // position and speed from one consistent reading
  SubstepEncoder::Snapshot s = encoder.read();

  Serial.print("Position [rev]: ");
  Serial.print(s.position / SUBSTEPS_PER_REV, 4);
  Serial.print(" | Speed [rev/s]: ");
  Serial.print(s.speed / SUBSTEPS_PER_REV, 4);
  Serial.print(" | stopped: ");
  Serial.println(encoder.stopped());

  delay(10);
}
