// Generic read-only encoder diagnostics; does not control a motor.
// For the Nidec/INA219 test-fixture wiring see docs/HARDWARE_VALIDATION_JA.md.
#include <Pico_PIO_Encoder.h>
#include <cstdio>

constexpr uint kEncoderA = 2;
constexpr int kIndexPin = -1;  // set to Z GPIO if present; -1 disables it
constexpr uint32_t kPositionPeriodUs = 1000;

Pico_PIO_Encoder encoder;
uint32_t nextReadUs, lastReadUs, nextLogUs;
uint32_t samples = 0, missed = 0, maxReadUs = 0, maxGapUs = 0, droppedLogs = 0;

void setup() {
  Serial.begin(115200);
  if (encoder.beginConsecutive(kEncoderA, kEncoderA + 1) != 0) {
    while (true) { Serial.println("encoder.begin failed"); delay(1000); }
  }
  encoder.setMinRefreshIntervalUs(0); // every read() captures new hardware data
  encoder.setSpeedUpdateIntervalUs(10000); // 100Hz speed, 1kHz position
  encoder.setIdleTimeoutUs(50000);
  if (kIndexPin >= 0 && encoder.attachIndex(uint(kIndexPin)) != 0) {
    while (true) { Serial.println("attachIndex failed"); delay(1000); }
  }
  nextReadUs = nextLogUs = micros();
  Serial.println("t_us,speed_t_us,count,substeps,speed,stopped,samples,missed,max_read_us,max_gap_us,index_count,index_spacing,dropped_logs");
}

void loop() {
  if (Serial.available()) {
    const char c = Serial.read();
    if (c == '1') encoder.setSpeedUpdateIntervalUs(10000);
    if (c == '2') encoder.setSpeedUpdateIntervalUs(20000);
    if (c == '3') encoder.setSpeedUpdateIntervalUs(5000);
    if (c == 'r') encoder.resetPosition();
    if (c == 'z' && kIndexPin >= 0) encoder.zeroOnNextIndex();
  }
  const uint32_t now = micros();
  if (int32_t(now - nextReadUs) < 0) return;
  const uint32_t skipped = (now - nextReadUs) / kPositionPeriodUs;
  missed += skipped;
  nextReadUs += (skipped + 1) * kPositionPeriodUs;
  if (samples && now - lastReadUs > maxGapUs) maxGapUs = now - lastReadUs;
  lastReadUs = now;
  const auto s = encoder.read();
  const uint32_t elapsed = micros() - now;
  if (elapsed > maxReadUs) maxReadUs = elapsed;
  ++samples;

  // Print at 100Hz; do not block the 1kHz acquisition on USB backpressure.
  if (int32_t(now - nextLogUs) >= 0) {
    nextLogUs = now + 10000;
    // Temporarily permit cached getters for the index fields in this line.
    encoder.setMinRefreshIntervalUs(1000);
    const uint32_t indexCount = encoder.indexCount();
    const int64_t spacing = encoder.lastIndexSpacing();
    encoder.setMinRefreshIntervalUs(0);
    char line[220];
    const int n = snprintf(line, sizeof(line), "%lu,%lu,%lld,%lld,%.5f,%d,%lu,%lu,%lu,%lu,%lu,%lld,%lu\n",
        (unsigned long)s.timestamp_us, (unsigned long)s.speed_timestamp_us,
        (long long)s.position, (long long)s.position_substeps, (double)s.speed, int(s.stopped),
        (unsigned long)samples, (unsigned long)missed, (unsigned long)maxReadUs,
        (unsigned long)maxGapUs, (unsigned long)indexCount, (long long)spacing,
        (unsigned long)droppedLogs);
    if (n > 0 && n < int(sizeof(line)) && Serial.availableForWrite() >= n) Serial.write(line, n);
    else ++droppedLogs;
  }
}
