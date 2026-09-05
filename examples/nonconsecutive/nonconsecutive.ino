// A -> GPIO2, B -> GPIO10, common GND, 3.3V-compatible signals.
// Explicit nonconsecutive mode: 2 SMs per encoder, up to 2 encoders per PIO.
// GPIO numbers are not the physical header-pin numbers.
#include <Pico_PIO_Encoder.h>
Pico_PIO_Encoder encoder;
using Pull = Pico_PIO_Encoder::Pull;
void setup() {
  Serial.begin(115200);
  // Omitting the third argument also selects Pull::Up.
  // Select Pull::None for external bias, or Pull::Down if required by the circuit.
  const int result=encoder.beginNonConsecutive(2,10,Pull::Up);
  if(result!=0) {
    Serial.println(Pico_PIO_Encoder::beginErrorMessage(result));
    while(true)delay(1000);
  }
  encoder.setSpeedUpdateIntervalUs(10000);
}
void loop() {
  const auto s=encoder.read();
  if(!s.positionValid())Serial.println("Encoder acquisition unavailable");
  else Serial.printf("position=%lld step speed=%.3f step/s status=0x%lx\n",
    s.position,s.speed,(unsigned long)s.status);
  delay(10);
}
