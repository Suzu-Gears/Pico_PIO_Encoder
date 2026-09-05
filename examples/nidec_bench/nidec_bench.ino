// RP2040-Zero / Nidec current_feedback wiring; no Z phase.
// Serial commands (newline terminated):
// P output duration_ms : signed PWM command, +/-32767, at most 10 seconds
// I mA duration_ms     : signed current target, +/-1200mA, at most 10 seconds
// R output ramp_ms hold_ms : ramp up, hold, ramp down; total <=10 seconds
// V interval_us       : velocity update interval, 0..1000000
// T timeout_us        : idle timeout, 1000..1000000
// G pause_ms          : pause encoder acquisition only, 0..200ms
// Z                   : zero measured/interpolated position
// S                   : stop and apply brake
// D                   : print bus voltage and GPIO levels (use while stopped)
// Every run expires locally even if the host disconnects. No automatic run.
#include <Pico_PIO_Encoder.h>
#include <Wire.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

constexpr uint kDir = 26, kPwm = 15, kBrake = 14;
Pico_PIO_Encoder encoder;
Pico_PIO_Encoder::Snapshot sample{};
char mode = 'S';
int demand = 0, output = 0, fault = 0;
float currentMa = 0, filteredMa = 0, integral = 0;
uint32_t nextTick, nextLog, runUntil = 0, pauseUntil = 0, previousTick = 0;
uint32_t samples = 0, missed = 0, maxWorkUs = 0, maxReadUs = 0, i2cErrors = 0;
uint32_t rampStart = 0, rampMs = 0, holdMs = 0;

bool readRegister(uint8_t reg, int16_t &value) {
  Wire.beginTransmission(0x40);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(uint8_t(0x40), uint8_t(2)) != 2) return false;
  const uint16_t raw = uint16_t(Wire.read()) << 8 | uint16_t(Wire.read());
  value = int16_t(raw);
  return true;
}

void stopMotor() {
  analogWrite(kPwm, 32767); // negative-logic PWM: full HIGH is zero drive
  digitalWrite(kBrake, LOW);
  mode = 'S';
  output = demand = 0;
  integral = 0;
}

void applyOutput(int value) {
  value = std::clamp(value, -32767, 32767);
  // Match the existing driver's A28/B27 convention: drive_sign=-1,
  // raw polarity on this physical setup is negative for positive command.
  digitalWrite(kDir, value <= 0 ? HIGH : LOW);
  analogWrite(kPwm, 32767 - std::abs(value));
  digitalWrite(kBrake, HIGH);
  output = value;
}

void command(const char *line) {
  char op = 0;
  long value = 0, duration = 0, hold = 0;
  const int fields = sscanf(line, " %c %ld %ld %ld", &op, &value, &duration, &hold);
  if (op == 'D') {
    int16_t bus = 0;
    const bool ok = readRegister(2, bus);
    Serial.printf("diag,bus_ok=%d,bus_mv=%u,A=%d,B=%d,brake=%d,dir=%d,pwm_level=%d\n",
                  int(ok), unsigned((uint16_t(bus) >> 3) * 4), digitalRead(28), digitalRead(27),
                  digitalRead(kBrake), digitalRead(kDir), digitalRead(kPwm));
    return;
  }
  if (op == 'S') { stopMotor(); return; }
  if (op == 'Z') { encoder.resetPosition(); sample = encoder.read(); return; }
  if (op == 'V' && fields >= 2 && value >= 0 && value <= 1000000)
    encoder.setSpeedUpdateIntervalUs(uint32_t(value));
  if (op == 'T' && fields >= 2 && value >= 1000 && value <= 1000000)
    encoder.setIdleTimeoutUs(uint32_t(value));
  if (op == 'G' && fields >= 2 && value >= 0 && value <= 200)
    pauseUntil = micros() + uint32_t(value) * 1000;
  if (op == 'R' && fields == 4 && value >= -32767 && value <= 32767 &&
      duration >= 100 && duration <= 4000 && hold >= 0 && hold <= 8000 &&
      2 * duration + hold <= 10000 && !fault) {
    stopMotor();
    mode = 'R';
    demand = int(value);
    rampMs = uint32_t(duration);
    holdMs = uint32_t(hold);
    rampStart = millis();
    runUntil = rampStart + 2 * rampMs + holdMs;
  }
  if ((op == 'P' || op == 'I') && fields == 3 && duration > 0 && duration <= 10000) {
    if (fault || std::abs(value) > (op == 'I' ? 1200 : 32767)) return;
    stopMotor();
    mode = op;
    demand = int(value);
    filteredMa = 0;
    runUntil = millis() + uint32_t(duration);
  }
}

void setup() {
  digitalWrite(kBrake, LOW); pinMode(kBrake, OUTPUT);
  digitalWrite(kPwm, HIGH); pinMode(kPwm, OUTPUT);
  digitalWrite(kDir, LOW); pinMode(kDir, OUTPUT);
  analogWriteFreq(30000);
  analogWriteResolution(15);
  stopMotor();
  Serial.begin(115200);
  Wire.setSDA(4); Wire.setSCL(5); Wire.setClock(400000); Wire.begin();
  Wire.setTimeout(2); // arduino-pico Wire timeout in milliseconds
  Wire.beginTransmission(0x40);
  Wire.write(0); Wire.write(0x39); Wire.write(0x9F); // original INA219 config
  if (Wire.endTransmission() != 0) fault = 1;
  if (encoder.beginConsecutive(27, 28) != 0) fault = 2;
  encoder.setMinRefreshIntervalUs(0);
  encoder.setSpeedUpdateIntervalUs(10000);
  nextTick = nextLog = previousTick = micros();
}

void loop() {
  static char line[48];
  static size_t length = 0;
  // Bound parsing work so incoming text cannot monopolize the control loop.
  for (int i = 0; i < 48 && Serial.available(); ++i) {
    const char ch = char(Serial.read());
    if (ch == '\n') { line[length] = 0; command(line); length = 0; }
    else if (ch != '\r' && length < sizeof(line) - 1) line[length++] = ch;
  }
  const uint32_t now = micros();
  if (mode != 'S' && int32_t(millis() - runUntil) >= 0) stopMotor();
  if (int32_t(now - nextTick) < 0) return;
  const uint32_t skipped = (now - nextTick) / 1000;
  missed += skipped;
  nextTick += (skipped + 1) * 1000;
  const float dt = float(now - previousTick) / 1000000.0f;
  previousTick = now;
  if (int32_t(now - pauseUntil) >= 0) {
    const uint32_t start = micros();
    sample = encoder.read();
    maxReadUs = std::max(maxReadUs, micros() - start);
    ++samples;
  }
  int16_t shunt = 0;
  if (!readRegister(1, shunt)) {
    ++i2cErrors;
    fault = 3;
    stopMotor();
  } else {
    const float magnitude = std::abs(int(shunt)) * 0.1f; // original 0.1ohm shunt scale
    currentMa = output < 0 ? -magnitude : magnitude;
    filteredMa += 0.1f * (currentMa - filteredMa);
    if (magnitude > 1600) { fault = 4; stopMotor(); }
    if (!fault && mode == 'P') applyOutput(demand);
    if (!fault && mode == 'R') {
      const uint32_t elapsed = millis() - rampStart;
      if (elapsed < rampMs) applyOutput(int(int64_t(demand) * elapsed / rampMs));
      else if (elapsed < rampMs + holdMs) applyOutput(demand);
      else if (elapsed < 2 * rampMs + holdMs)
        applyOutput(int(int64_t(demand) * (2 * rampMs + holdMs - elapsed) / rampMs));
      else stopMotor();
    }
    if (!fault && mode == 'I') {
      const float error = demand - filteredMa;
      integral = std::clamp(integral + error * dt, -30.0f, 30.0f);
      // Same P/I coefficients as current_feedback, with measured dt.
      const float control = 100.0f * error + 1000.0f * integral;
      applyOutput(int(std::clamp(control, -32767.0f, 32767.0f)));
    }
  }
  maxWorkUs = std::max(maxWorkUs, micros() - now);
  if (int32_t(now - nextLog) < 0) return;
  nextLog = now + 10000;
  char row[240];
  const int n = snprintf(row, sizeof(row),
      "%lu,%lu,%lu,%lld,%lld,%.4f,%d,%c,%d,%d,%.2f,%.2f,%lu,%lu,%lu,%lu,%lu,%d\n",
      (unsigned long)now, (unsigned long)sample.timestamp_us,
      (unsigned long)sample.speed_timestamp_us, (long long)sample.position,
      (long long)sample.position_substeps, (double)sample.speed, int(sample.stopped),
      mode, demand, output, (double)currentMa, (double)filteredMa, (unsigned long)samples,
      (unsigned long)missed, (unsigned long)maxReadUs, (unsigned long)maxWorkUs,
      (unsigned long)i2cErrors, fault);
  if (n > 0 && n < int(sizeof(row)) && Serial.availableForWrite() >= n) Serial.write(row, n);
}
