// Pico_PIO_Encoder basic example: print position and speed.
//
// Connect the two encoder phases to two consecutive GPIOs (here: 2 and 3)
// and explicitly select the consecutive mode (up to 4 encoders per PIO).
//
// There is no update() to call: the PIO tracks the encoder in hardware and
// getters acquire on demand. See multirate/ for independently scheduled speed.

#include <Pico_PIO_Encoder.h>

Pico_PIO_Encoder encoder;

void setup() {
  Serial.begin(115200);

  const int result = encoder.beginConsecutive(2, 3);
  if (result != 0) {
    Serial.println(Pico_PIO_Encoder::beginErrorMessage(result));
    while (1) delay(1000);
  }

  // quadrature steps per revolution = PPR x 4 (100 PPR encoder -> 400).
  // This enables the unit helpers used below; the measured count API
  // (position() / speed()) works without it
  encoder.setStepsPerRev(400);
}

void loop() {
  // consistent reading in quadrature steps (4x counting; use
  // positionSubsteps()/speedSubsteps() for 1/64-step resolution)
  Pico_PIO_Encoder::Snapshot s = encoder.read();

  Serial.print("Position [steps]: ");
  Serial.printf("%lld", (long long)s.position);
  // The unit helpers may acquire again. Use one saved Snapshot when values
  // must correspond to the same observation (see README).
  Serial.print(" | turns: ");
  Serial.printf("%lld", (long long)encoder.turns());
  Serial.print(" | angle [deg]: ");
  Serial.print(encoder.angleInRevDeg(), 2);
  Serial.print(" | Speed [rpm]: ");
  Serial.print(encoder.rpm(), 2);
  Serial.print(" | stopped: ");
  Serial.println(encoder.stopped());

  delay(10);
}
