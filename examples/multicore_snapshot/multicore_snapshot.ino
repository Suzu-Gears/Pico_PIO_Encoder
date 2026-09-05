// arduino-pico: core 0 acquires; core 1 reads saved data only. No motor output.
#include <Pico_PIO_Encoder.h>
#include <cstdio>

Pico_PIO_Encoder encoder;
uint32_t next_read_us = 0;

void setup() {
  Serial.begin(115200);
  encoder.setSpeedUpdateIntervalUs(10000);
  encoder.setMaxSampleAgeUs(3000);
  if (encoder.beginConsecutive(2, 3) != 0) {
    while (true) delay(1000);
  }
  next_read_us = micros();
}

void loop() {
  const uint32_t now = micros();
  if (int32_t(now - next_read_us) < 0) return;
  next_read_us += (uint32_t(now - next_read_us) / 1000 + 1) * 1000;
  encoder.refresh();
  const auto sample = encoder.latest();
  if (sample.positionValid() && sample.speedValid()) {
    // Compute control from this single sample here.
  }
}

void setup1() {}
void loop1() {
  const auto sample = encoder.latest();
  char line[180];
  const int length = snprintf(line, sizeof(line), "%llu,%lu,%lld,%.5f,0x%lx\n",
    (unsigned long long)sample.timestamp_us64, (unsigned long)sample.speed_timestamp_us,
    (long long)sample.position, (double)sample.speed, (unsigned long)sample.status);
  if (Serial && length > 0 && length < int(sizeof(line)) && Serial.availableForWrite() >= length)
    Serial.write(line, length);
  delay(20);
}
