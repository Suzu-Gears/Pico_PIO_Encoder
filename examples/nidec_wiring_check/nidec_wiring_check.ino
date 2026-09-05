// Nidec_24H_BLDC_Pico/current_feedback wiring, no Z phase.
// This first-stage diagnostic keeps the motor braked with zero drive.
// It does NOT run a current controller. Integrate read() into the established
// controller only after the checks in docs/HARDWARE_VALIDATION_JA.md.
#include <Pico_PIO_Encoder.h>
#include <Wire.h>
#include <cstdio>

constexpr uint kEncoderFirstPin = 27; // B=27, A=28, ascending PIO input order
constexpr uint kDirection = 26, kPwm = 15, kBrake = 14;
Pico_PIO_Encoder encoder;
uint32_t nextTick, nextLog, samples = 0, missed = 0, maxReadUs = 0;
int ina219Status = -1;

void setup() {
  // Active-low PWM: HIGH = zero drive. LOW on BRAKE applies the brake.
  digitalWrite(kBrake, LOW);
  pinMode(kBrake, OUTPUT);
  digitalWrite(kPwm, HIGH);
  pinMode(kPwm, OUTPUT);
  digitalWrite(kDirection, LOW);
  pinMode(kDirection, OUTPUT);
  Serial.begin(115200);
  Wire.setSDA(4);
  Wire.setSCL(5);
  Wire.setClock(400000); // only probe, do not copy the example's 2.56MHz bus
  Wire.begin();
  Wire.beginTransmission(0x40);
  ina219Status = Wire.endTransmission();
  if (encoder.beginConsecutive(kEncoderFirstPin, kEncoderFirstPin + 1) != 0) {
    while (true) { Serial.println("encoder.begin failed"); delay(1000); }
  }
  encoder.setMinRefreshIntervalUs(0);
  encoder.setSpeedUpdateIntervalUs(10000);
  nextTick = nextLog = micros();
}

void loop() {
  const uint32_t now = micros();
  if (int32_t(now - nextTick) < 0) return;
  const uint32_t skipped = (now - nextTick) / 1000;
  missed += skipped;
  nextTick += (skipped + 1) * 1000;
  const auto s = encoder.read();
  const uint32_t elapsed = micros() - now;
  if (elapsed > maxReadUs) maxReadUs = elapsed;
  ++samples;
  if (int32_t(now - nextLog) < 0) return;
  nextLog = now + 100000;
  char line[200];
  const int n = snprintf(line, sizeof(line),
      "t=%lu,speed_t=%lu,count=%lld,substeps=%lld,speed=%.4f,stop=%d,n=%lu,missed=%lu,max_read_us=%lu,ina219=%d\n",
      (unsigned long)s.timestamp_us, (unsigned long)s.speed_timestamp_us,
      (long long)s.position, (long long)s.position_substeps, (double)s.speed, int(s.stopped),
      (unsigned long)samples, (unsigned long)missed, (unsigned long)maxReadUs, ina219Status);
  if (n > 0 && n < int(sizeof(line)) && Serial.availableForWrite() >= n) Serial.write(line, n);
}
