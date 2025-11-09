// Glitch Clocks
//
// Ville Alatalo (https://github.com/alatalo), 2025-11-09
// Vibe coded using ChatGPT 5.
//
// Acts as a master clock with support up to 4x different slave clock syncs.
// Not usable as a real master clock, as there is no attempt to keep accurate time.
//
// This is an art project / installation piece.
// Read more (in Finnish): https://medium.com/@ville.alatalo/kello-joka-tarvitsee-kellon-pysyakseen-ajassa-4ecf6e186a1d
//
// Supports 2x L298N or similar H-bridge drivers to drive
// multiple clock sync lanes for slave clocks. Each lane runs
// at a base tempo with slight random variations. Periodical random
// glithes are applied accross the lanes, such as long pauses,
// sync-locks, accelarations etc.
//
// For ESP32 NTP master clock, see https://gist.github.com/alatalo/cdd7e4c48bdc5a3fbb875d33d4a6d590
//

#include <Arduino.h>
#include <math.h>

// ---------------- Pins for 2x L298N's ----------------
struct LanePins {
  int IN1, IN2, EN;
};
const LanePins LANE_A_PINS = { 33, 14, 13 };
const LanePins LANE_B_PINS = { 27, 26, 25 };
const LanePins LANE_C_PINS = { 19, 21, 18 };
const LanePins LANE_D_PINS = { 22, 23, 32 };

// ---------------- Base phasing tempos (Hz) ----------------
float BASE_HZ_A = 1.00f;
float BASE_HZ_B = 0.98f;
float BASE_HZ_C = 1.02f;
float BASE_HZ_D = 1.01f;

// ---------------- Pulse safety ----------------
uint16_t PULSE_ON_MS = 120;  // 90–150 ms typical
uint16_t MIN_OFF_MS = 120;   // 120–180 ms recommended
int16_t JITTER_MS = 10;      // ± per-pulse jitter

// ---------------- Glitch cadence & weights ----------------
uint32_t GLITCH_CHECK_MS = 4000;  // consider new effect ~every xxxx ms
float GLITCH_PROB = 0.4f;         // glitch probability 0...1

// Effect weights (sum ≈ 0.90; remainder = subtle per-lane glitches)
float W_PAUSE = 0.08f;    // long pause (“did it end?”)
float W_STAGGER = 0.08f;  // staggered blackout
float W_SYNC = 0.10f;     // sync-lock
float W_BEAT = 0.03f;     // dembow/rock
float W_ACCEL = 0.10f;    // accelerando
float W_DECEL = 0.10f;    // decelerando
float W_COUNTER = 0.08f;  // counterbalance (one up, one down)
float W_STORM = 0.08f;    // email storm window
float W_SNAP = 0.06f;     // phase snap (one follows another)
float W_SCOPE = 0.06f;    // scope creep (+0.01 Hz)
float W_REORG = 0.06f;    // reorg swap (rotate tempos)

// Subtle per-lane glitch params
float NUDGE_MAX_PCT = 0.03f;
uint32_t NUDGE_MIN_MS = 2000;
uint32_t NUDGE_MAX_MS = 8000;
uint32_t FREEZE_MIN_MS = 2000;
uint32_t FREEZE_MAX_MS = 7000;
uint8_t BURST_MIN_PULSES = 3;
uint8_t BURST_MAX_PULSES = 8;
uint16_t BURST_TARGET_MS = 150;

// --- Themed specials config ---
uint32_t LONG_PAUSE_MIN_MS = 10000;
uint32_t LONG_PAUSE_MAX_MS = 14000;
uint32_t LONG_PAUSE_COOLDOWN = 40000;

uint32_t STAGGER_STOP_STEP_MS = 700;
uint32_t STAGGER_SILENCE_MS = 3500;

uint16_t SYNC_BPM = 120;
uint32_t SYNC_HOLD_MS = 8000;
uint32_t SYNC_COOLDOWN_MS = 10000;

uint32_t ACCEL_DUR_MS = 10000;
float ACCEL_TO_MUL_MIN = 1.25f;
float ACCEL_TO_MUL_MAX = 1.40f;

uint32_t DECEL_DUR_MS = 10000;
float DECEL_TO_MUL_MIN = 0.70f;
float DECEL_TO_MUL_MAX = 0.85f;

uint32_t COUNTERBAL_DUR_MS = 8000;
float COUNTERBAL_DELTA_MIN = 0.20f;
float COUNTERBAL_DELTA_MAX = 0.35f;

uint32_t STORM_DUR_MS = 3500;
uint32_t STORM_TICK_MIN_MS = 180;
uint32_t STORM_TICK_MAX_MS = 350;

uint32_t SCOPE_DUR_MS = 60000;
float SCOPE_DH = 0.01f;

uint32_t lastAnyPulseAt = 0;
const uint32_t LIVENESS_WARN_MS = 20000;

// ---------------- Beat patterns ----------------
struct BeatPattern {
  const char* name;
  uint16_t bpm;
  uint8_t steps;
  const uint8_t* A;
  uint8_t nA;
  const uint8_t* B;
  uint8_t nB;
  const uint8_t* C;
  uint8_t nC;
  uint8_t bars;
};
const uint8_t DEMBOW_A[] = { 0, 6, 8, 14 };
const uint8_t DEMBOW_B[] = { 4, 12 };
const uint8_t DEMBOW_C[] = { 2, 6, 10, 14 };
const BeatPattern DEMBOW = { "dembow", 95, 16,
                             DEMBOW_A, (uint8_t)sizeof(DEMBOW_A), DEMBOW_B, (uint8_t)sizeof(DEMBOW_B), DEMBOW_C, (uint8_t)sizeof(DEMBOW_C), 4 };

const uint8_t ROCK_A[] = { 0, 8 };
const uint8_t ROCK_B[] = { 4, 12 };
const uint8_t ROCK_C[] = { 2, 6, 10, 14 };
const BeatPattern ROCK = { "rock", 104, 16,
                           ROCK_A, (uint8_t)sizeof(ROCK_A), ROCK_B, (uint8_t)sizeof(ROCK_B), ROCK_C, (uint8_t)sizeof(ROCK_C), 4 };

// ---------------- Lane engine ----------------
struct Lane {
  LanePins pins;
  float baseHz = 1.0f;
  float nudgeMul = 1.0f;
  uint32_t nudgeUntil = 0;

  uint32_t nextDue = 0;
  uint32_t nextFree = 0;
  uint32_t coilOffAt = 0;
  bool running = true;

  bool pol = false;
  uint8_t minute = 0;

  uint32_t freezeUntil = 0;
  uint8_t burstRemain = 0;

  float savedHz = 1.0f;
};

// ---------------- Define Lanes ----------------
Lane A{ LANE_A_PINS, BASE_HZ_A };
Lane B{ LANE_B_PINS, BASE_HZ_B };
Lane C{ LANE_C_PINS, BASE_HZ_C };
Lane D{ LANE_D_PINS, BASE_HZ_D };

// Define the lanes that are in use. Also update setup() if you're modifying this
//Lane* LANES[] = { &A, &B, &C };
Lane* LANES[] = { &A, &B, &C, &D };

const size_t NLANES = sizeof(LANES) / sizeof(LANES[0]);

int laneIndex(Lane* L) {
  for (size_t i = 0; i < NLANES; i++)
    if (LANES[i] == L) return (int)i;
  return 0;
}
char laneName(Lane* L) {
  return (char)('A' + laneIndex(L));
}

float avgEffectiveHz() {
  float sum = 0;
  for (size_t i = 0; i < NLANES; i++) sum += LANES[i]->baseHz * LANES[i]->nudgeMul;
  return sum / (float)NLANES;
}
bool pickTwoDistinct(int& i, int& j) {
  if (NLANES < 2) return false;
  i = (int)random(0, (long)NLANES);
  j = (i + 1 + (int)random(0, (long)(NLANES - 1))) % (int)NLANES;
  return true;
}

static inline uint32_t periodMs(const Lane& L) {
  float effHz = max(0.001f, L.baseHz * L.nudgeMul);
  return (uint32_t)roundf(1000.0f / effHz);
}
static inline int32_t rng(int32_t lo, int32_t hi) {
  return lo + (esp_random() % (uint32_t)(hi - lo + 1));
}

void coilOn(Lane& L) {
  uint32_t now = millis();
  if ((int32_t)(now - L.nextFree) < 0) return;  // gated

  digitalWrite(L.pins.IN1, L.pol ? HIGH : LOW);
  digitalWrite(L.pins.IN2, L.pol ? LOW : HIGH);
  digitalWrite(L.pins.EN, HIGH);

  L.coilOffAt = now + PULSE_ON_MS;
  L.nextFree = now + PULSE_ON_MS + MIN_OFF_MS;

  L.pol = !L.pol;
  L.minute = (L.minute + 1) % 60;
  lastAnyPulseAt = now;
}
void maybeCoilOff(Lane& L, uint32_t now) {
  if (L.coilOffAt && (int32_t)(now - L.coilOffAt) >= 0) {
    digitalWrite(L.pins.EN, LOW);
    L.coilOffAt = 0;
  }
}
void scheduleNext(Lane& L, uint32_t now) {
  uint32_t base = periodMs(L);
  int32_t j = (JITTER_MS > 0) ? rng(-JITTER_MS, JITTER_MS) : 0;
  if ((int32_t)(now - L.nextDue) > (int32_t)(base * 4)) L.nextDue = now + base + j;
  else L.nextDue += base + j;
}
void setupLane(Lane& L) {
  pinMode(L.pins.IN1, OUTPUT);
  pinMode(L.pins.IN2, OUTPUT);
  pinMode(L.pins.EN, OUTPUT);
  digitalWrite(L.pins.IN1, LOW);
  digitalWrite(L.pins.IN2, LOW);
  digitalWrite(L.pins.EN, LOW);
  uint32_t now = millis();
  L.nextDue = now + rng(0, 500);
  L.nextFree = now;
}

// ---------------- MODES ----------------
enum Mode {
  MODE_NORMAL,
  MODE_BEAT,
  MODE_SYNC_LOCK,
  MODE_LONG_PAUSE,
  MODE_STAGGER_BLACKOUT,
  MODE_ACCEL,
  MODE_DECEL,
  MODE_SCOPE_CREEP,
  MODE_PHASE_SNAP,
  MODE_COUNTERBAL,
  MODE_EMAIL_STORM
};
Mode mode = MODE_NORMAL;

// ---------------- STATES ----------------
struct BeatState {
  const BeatPattern* pat = nullptr;
  uint32_t stepPeriodMs = 0, nextStepAt = 0;
  uint8_t step = 0, barsLeft = 0;
} beat;
void clearTransientsAll() {
  for (Lane* L : LANES) {
    L->burstRemain = 0;
    L->freezeUntil = 0;
    L->nudgeMul = 1.0f;
    L->nudgeUntil = 0;
  }
}
bool hasStep(const uint8_t* arr, uint8_t n, uint8_t step) {
  for (uint8_t i = 0; i < n; i++)
    if (arr[i] == step) return true;
  return false;
}
void startBeat(const BeatPattern& p) {
  clearTransientsAll();
  for (Lane* L : LANES) L->running = false;
  beat.pat = &p;
  beat.stepPeriodMs = (uint32_t)roundf((60000.0f / p.bpm) / 4.0f);
  beat.step = 0;
  beat.barsLeft = p.bars;
  beat.nextStepAt = millis() + 20;
  mode = MODE_BEAT;
  Serial.printf("[beat] start '%s' @ %u BPM for %u bars\n", p.name, p.bpm, p.bars);
}
void endBeat() {
  for (Lane* L : LANES) {
    L->running = true;
    L->nextDue = millis() + periodMs(*L);
  }
  mode = MODE_NORMAL;
  beat.pat = nullptr;
  Serial.println("[beat] end");
}
void handleBeat(uint32_t now) {
  if (!beat.pat || (int32_t)(now - beat.nextStepAt) < 0) return;
  if (hasStep(beat.pat->A, beat.pat->nA, beat.step)) coilOn(A);
  if (hasStep(beat.pat->B, beat.pat->nB, beat.step)) coilOn(B);
  if (hasStep(beat.pat->C, beat.pat->nC, beat.step)) coilOn(C);
  int j = (JITTER_MS > 0) ? rng(-JITTER_MS / 2, JITTER_MS / 2) : 0;
  beat.nextStepAt += beat.stepPeriodMs + j;
  beat.step = (beat.step + 1) % beat.pat->steps;
  if (beat.step == 0 && beat.barsLeft && --beat.barsLeft == 0) endBeat();
}

// Sync-lock
uint32_t lastSyncEnd = 0, modeEndsAt = 0;
void startSyncLock() {
  if ((int32_t)(millis() - lastSyncEnd) < (int32_t)SYNC_COOLDOWN_MS) return;
  clearTransientsAll();
  uint32_t now = millis();
  for (Lane* L : LANES) {
    L->savedHz = L->baseHz;
    L->baseHz = (float)SYNC_BPM / 60.0f;
    L->running = true;
    L->nextDue = now + 50;
    L->pol = false;
  }
  mode = MODE_SYNC_LOCK;
  modeEndsAt = now + SYNC_HOLD_MS;
  Serial.printf("[sync] lock @ %u BPM for %ums\n", SYNC_BPM, SYNC_HOLD_MS);
}
void endSyncLock() {
  for (Lane* L : LANES) {
    L->baseHz = L->savedHz;
    L->nudgeMul = 1.0f;
    L->nudgeUntil = 0;
    L->nextDue = millis() + periodMs(*L) + rng(0, 120);
  }
  mode = MODE_NORMAL;
  lastSyncEnd = millis();
  Serial.println("[sync] release");
}

// Long pause / Stagger blackout
uint32_t lastLongPauseEnd = 0;
void startLongPause() {
  if ((int32_t)(millis() - lastLongPauseEnd) < (int32_t)LONG_PAUSE_COOLDOWN) return;
  clearTransientsAll();
  uint32_t dur = rng(LONG_PAUSE_MIN_MS, LONG_PAUSE_MAX_MS), now = millis();
  for (Lane* L : LANES) {
    L->freezeUntil = now + dur;
    L->running = false;
  }
  mode = MODE_LONG_PAUSE;
  modeEndsAt = now + dur;
  Serial.printf("[pause] long silence %u ms\n", dur);
}
void endLongPause() {
  for (Lane* L : LANES) {
    L->running = true;
    L->nextDue = millis() + periodMs(*L) + rng(0, 120);
  }
  mode = MODE_NORMAL;
  lastLongPauseEnd = millis();
  Serial.println("[pause] resume");
}
void startStaggerBlackout() {
  clearTransientsAll();
  uint32_t now = millis();
  for (size_t i = 0; i < NLANES; i++) {
    size_t stepsFromEnd = (NLANES - 1) - i;
    LANES[i]->freezeUntil = now + STAGGER_SILENCE_MS + STAGGER_STOP_STEP_MS * stepsFromEnd;
    LANES[i]->running = false;
  }
  mode = MODE_STAGGER_BLACKOUT;
  modeEndsAt = now + STAGGER_SILENCE_MS + STAGGER_STOP_STEP_MS * (NLANES - 1);
  Serial.printf("[stagger] window %u ms, step %u ms\n", STAGGER_SILENCE_MS, STAGGER_STOP_STEP_MS);
}

void endStaggerBlackout() {
  for (size_t i = 0; i < NLANES; i++) {
    LANES[i]->running = true;
    LANES[i]->nextDue = millis() + periodMs(*LANES[i]) + rng(0, 100 + (int)(20 * i));
  }
  mode = MODE_NORMAL;
  Serial.println("[stagger] resume");
}

// Accelerando / Decelerando (all lanes)
uint32_t rampStart = 0, rampEnd = 0;
float rampTo = 1.0f;
void startAccelerando() {
  clearTransientsAll();
  rampStart = millis();
  rampEnd = rampStart + ACCEL_DUR_MS;
  rampTo = (float)rng((int)(ACCEL_TO_MUL_MIN * 100), (int)(ACCEL_TO_MUL_MAX * 100)) / 100.0f;
  for (Lane* L : LANES) {
    L->nudgeMul = 1.0f;
    L->nudgeUntil = 0;
  }
  mode = MODE_ACCEL;
  Serial.printf("[accel] to x%.2f over %ums\n", rampTo, ACCEL_DUR_MS);
}
void handleAccelerando(uint32_t now) {
  float t = constrain((float)(now - rampStart) / (float)(rampEnd - rampStart), 0.0f, 1.0f);
  float mul = 1.0f + (rampTo - 1.0f) * t;
  for (Lane* L : LANES) L->nudgeMul = mul;
  if ((int32_t)(now - rampEnd) >= 0) {
    for (Lane* L : LANES) L->nudgeMul = 1.0f;
    mode = MODE_NORMAL;
    Serial.println("[accel] done");
  }
}
void startDecelerando() {
  clearTransientsAll();
  rampStart = millis();
  rampEnd = rampStart + DECEL_DUR_MS;
  rampTo = (float)rng((int)(DECEL_TO_MUL_MIN * 100), (int)(DECEL_TO_MUL_MAX * 100)) / 100.0f;
  for (Lane* L : LANES) {
    L->nudgeMul = 1.0f;
    L->nudgeUntil = 0;
  }
  mode = MODE_DECEL;
  Serial.printf("[decel] to x%.2f over %ums\n", rampTo, DECEL_DUR_MS);
}
void handleDecelerando(uint32_t now) {
  float t = constrain((float)(now - rampStart) / (float)(rampEnd - rampStart), 0.0f, 1.0f);
  float mul = 1.0f + (rampTo - 1.0f) * t;
  for (Lane* L : LANES) L->nudgeMul = mul;
  if ((int32_t)(now - rampEnd) >= 0) {
    for (Lane* L : LANES) L->nudgeMul = 1.0f;
    mode = MODE_NORMAL;
    Serial.println("[decel] done");
  }
}

// Counterbalance (one up, one down)
int cbUp = -1, cbDown = -1;
uint32_t cbStart = 0, cbEnd = 0;
float cbDelta = 0.2f;
void startCounterbalance() {
  cbUp = rng(0, (int)NLANES - 1);
  do { cbDown = rng(0, (int)NLANES - 1); } while (cbDown == cbUp);
  cbDelta = (float)rng((int)(COUNTERBAL_DELTA_MIN * 100), (int)(COUNTERBAL_DELTA_MAX * 100)) / 100.0f;
  cbStart = millis();
  cbEnd = cbStart + COUNTERBAL_DUR_MS;
  for (size_t i = 0; i < NLANES; i++) LANES[i]->nudgeMul = 1.0f;
  mode = MODE_COUNTERBAL;
  Serial.printf("[counter] %c up +%.0f%%, %c down −%.0f%% for %ums\n",
                'A' + cbUp, cbDelta * 100.0f, 'A' + cbDown, cbDelta * 100.0f, COUNTERBAL_DUR_MS);
}
void handleCounterbalance(uint32_t now) {
  float t = constrain((float)(now - cbStart) / (float)(cbEnd - cbStart), 0.0f, 1.0f);
  float upMul = 1.0f + cbDelta * t;
  float downMul = 1.0f - cbDelta * t;
  for (size_t i = 0; i < NLANES; i++) {
    if (i == cbUp) LANES[i]->nudgeMul = upMul;
    else if (i == cbDown) LANES[i]->nudgeMul = downMul;
    else LANES[i]->nudgeMul = 1.0f;
  }
  if ((int32_t)(now - cbEnd) >= 0) {
    for (Lane* L : LANES) L->nudgeMul = 1.0f;
    mode = MODE_NORMAL;
    Serial.println("[counter] done");
  }
}

// Email storm (microburst window)
uint32_t stormEnd = 0, nextStormTick = 0;
void startEmailStorm() {
  clearTransientsAll();
  stormEnd = millis() + STORM_DUR_MS;
  nextStormTick = millis();
  mode = MODE_EMAIL_STORM;
  Serial.printf("[storm] %ums window\n", STORM_DUR_MS);
}
void handleEmailStorm(uint32_t now) {
  if ((int32_t)(now - stormEnd) >= 0) {
    mode = MODE_NORMAL;
    Serial.println("[storm] end");
    return;
  }
  if ((int32_t)(now - nextStormTick) >= 0) {
    int i, j;
    if (pickTwoDistinct(i, j)) {
      LANES[i]->burstRemain = rng(BURST_MIN_PULSES, BURST_MAX_PULSES);
      LANES[j]->burstRemain = rng(BURST_MIN_PULSES, BURST_MAX_PULSES);
    } else {
      LANES[0]->burstRemain = rng(BURST_MIN_PULSES, BURST_MAX_PULSES);
    }
    nextStormTick = now + rng(STORM_TICK_MIN_MS, STORM_TICK_MAX_MS);
  }
}

// Reorg swap (rotate base tempos)
void reorgSwap() {
  if (NLANES < 2) return;
  float first = LANES[0]->baseHz;
  for (size_t i = 0; i < NLANES - 1; i++) LANES[i]->baseHz = LANES[i + 1]->baseHz;
  LANES[NLANES - 1]->baseHz = first;
  for (Lane* L : LANES) L->nextDue = millis() + periodMs(*L);
  Serial.print("[reorg] tempos now ");
  for (size_t i = 0; i < NLANES; i++) { Serial.printf("%c=%.2f ", 'A' + (int)i, LANES[i]->baseHz); }
  Serial.println();
}

/// Scope creep
float scopeStartHz[NLANES];
uint32_t scopeStart = 0, scopeEnd = 0;
void startScopeCreep() {
  scopeStart = millis();
  scopeEnd = scopeStart + SCOPE_DUR_MS;
  for (size_t i = 0; i < NLANES; i++) scopeStartHz[i] = LANES[i]->baseHz;
  mode = MODE_SCOPE_CREEP;
  Serial.printf("[scope] +%.3f Hz over %ums\n", SCOPE_DH, SCOPE_DUR_MS);
}
void handleScopeCreep(uint32_t now) {
  float t = constrain((float)(now - scopeStart) / (float)(scopeEnd - scopeStart), 0.0f, 1.0f);
  for (size_t i = 0; i < NLANES; i++) LANES[i]->baseHz = scopeStartHz[i] + SCOPE_DH * t;
  if ((int32_t)(now - scopeEnd) >= 0) {
    for (size_t i = 0; i < NLANES; i++) LANES[i]->baseHz = scopeStartHz[i];
    mode = MODE_NORMAL;
    Serial.println("[scope] done");
  }
}

// Phase snap (temporarily align one to another)
int snapRef = -1, snapFol = -1;
uint32_t snapEnd = 0;
void startPhaseSnap() {
  snapRef = rng(0, (int)NLANES - 1);
  do { snapFol = rng(0, (int)NLANES - 1); } while (snapFol == snapRef);
  LANES[snapFol]->savedHz = LANES[snapFol]->baseHz;
  LANES[snapFol]->baseHz = LANES[snapRef]->baseHz;
  LANES[snapFol]->nextDue = LANES[snapRef]->nextDue;
  snapEnd = millis() + 4000;
  mode = MODE_PHASE_SNAP;
  Serial.printf("[snap] %c follows %c\n", 'A' + snapFol, 'A' + snapRef);
}

void handlePhaseSnap(uint32_t now) {
  LANES[snapFol]->baseHz = LANES[snapRef]->baseHz;
  LANES[snapFol]->nextDue = LANES[snapRef]->nextDue;
  if ((int32_t)(now - snapEnd) >= 0) {
    LANES[snapFol]->baseHz = LANES[snapFol]->savedHz;
    mode = MODE_NORMAL;
    Serial.println("[snap] release");
  }
}

// ---------------- Random glitch engine ----------------

// Pointers to effect weights, so changes to W_* are reflected automatically.
float* const WEIGHT_PTRS[] = {
  &W_PAUSE,
  &W_STAGGER,
  &W_SYNC,
  &W_BEAT,
  &W_ACCEL,
  &W_DECEL,
  &W_COUNTER,
  &W_STORM,
  &W_SNAP,
  &W_SCOPE,
  &W_REORG
};

const int NEFF = sizeof(WEIGHT_PTRS) / sizeof(WEIGHT_PTRS[0]);

// cumW[i] = cumulative probability boundary for effect i
// cumW[NEFF] is capped at 1.0f
float cumW[NEFF + 1];

void buildWeights() {
  float sum = 0.0f;
  for (int i = 0; i < NEFF; i++) {
    sum += *WEIGHT_PTRS[i];
    cumW[i] = sum;
  }

  // Cap the last bucket so a random value in [0,1) always maps safely.
  cumW[NEFF] = 1.0f;
}

void pickAndStartEffect() {
  float r = (float)random(0, 10000) / 10000.0f;
  if (r < cumW[0]) {
    startLongPause();
    return;
  } else if (r < cumW[1]) {
    startStaggerBlackout();
    return;
  } else if (r < cumW[2]) {
    startSyncLock();
    return;
  } else if (r < cumW[3]) {
    if (random(0, 2)) startBeat(DEMBOW);
    else startBeat(ROCK);
    return;
  } else if (r < cumW[4]) {
    startAccelerando();
    return;
  } else if (r < cumW[5]) {
    startDecelerando();
    return;
  } else if (r < cumW[6]) {
    startCounterbalance();
    return;
  } else if (r < cumW[7]) {
    startEmailStorm();
    return;
  } else if (r < cumW[8]) {
    startPhaseSnap();
    return;
  } else if (r < cumW[9]) {
    startScopeCreep();
    return;
  } else if (r < cumW[10]) {
    reorgSwap();
    return;
  }

  // fallback: subtle per-lane glitch
  Lane* L = LANES[random(0, (long)NLANES)];
  int g = random(0, 100);
  if (g < 40) {
    float sign = random(0, 2) ? +1.0f : -1.0f;
    float mul = 1.0f + sign * (NUDGE_MAX_PCT * (random(30, 100) / 100.0f));
    uint32_t dur = rng(NUDGE_MIN_MS, NUDGE_MAX_MS);
    L->nudgeMul = mul;
    L->nudgeUntil = millis() + dur;
    Serial.printf("[glitch] %c nudge x%.3f for %ums\n", laneName(L), mul, dur);
  } else if (g < 65) {
    uint32_t dur = rng(FREEZE_MIN_MS, FREEZE_MAX_MS);
    L->freezeUntil = millis() + dur;
    Serial.printf("[glitch] %c freeze %ums\n", laneName(L), dur);
  } else {
    L->burstRemain = rng(BURST_MIN_PULSES, BURST_MAX_PULSES);
    Serial.printf("[glitch] %c burst %u\n", laneName(L), L->burstRemain);
  }
}

// ---------------- Setup/Loop ----------------
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("\nGlitch Clocks");
  randomSeed(esp_random());

  // Comment lanes out if less than 4 lanes in use
  setupLane(A);
  setupLane(B);
  setupLane(C);
  setupLane(D);

  buildWeights();
  lastAnyPulseAt = millis();  // liveness baseline
}

void loop() {
  uint32_t now = millis();

  // consider a new effect while idle
  static uint32_t lastTry = 0;
  if (mode == MODE_NORMAL && (int32_t)(now - lastTry) >= 0) {
    lastTry = now + GLITCH_CHECK_MS + rng(-500, 500);
    if (random(0, 1000) < (int)(GLITCH_PROB * 1000)) pickAndStartEffect();
  }

  // handle modes
  switch (mode) {
    case MODE_BEAT: handleBeat(now); break;
    case MODE_SYNC_LOCK:
      if ((int32_t)(now - modeEndsAt) >= 0) endSyncLock();
      break;
    case MODE_LONG_PAUSE:
      if ((int32_t)(now - modeEndsAt) >= 0) endLongPause();
      break;
    case MODE_STAGGER_BLACKOUT:
      if ((int32_t)(now - modeEndsAt) >= 0) endStaggerBlackout();
      break;
    case MODE_ACCEL: handleAccelerando(now); break;
    case MODE_DECEL: handleDecelerando(now); break;
    case MODE_EMAIL_STORM: handleEmailStorm(now); break;
    case MODE_SCOPE_CREEP: handleScopeCreep(now); break;
    case MODE_PHASE_SNAP: handlePhaseSnap(now); break;
    case MODE_COUNTERBAL: handleCounterbalance(now); break;
    default: break;
  }

  // Liveness check (engine watchdog-lite)
  static uint32_t lastLivenessLog = 0;
  if ((int32_t)(now - lastAnyPulseAt) > (int32_t)LIVENESS_WARN_MS
      && mode == MODE_NORMAL) {
    // Nudge schedules forward politely
    for (Lane* L : LANES) {
      if (!L->running) continue;
      L->nextDue = now + periodMs(*L) + rng(0, 100);
    }
    if ((int32_t)(now - lastLivenessLog) > 5000) {
      Serial.println("[watch] no pulses for a while; re-armed schedules");
      lastLivenessLog = now;
    }
  }

  // lane scheduler
  for (Lane* L : LANES) {
    maybeCoilOff(*L, now);

    if (L->nudgeUntil && (int32_t)(now - L->nudgeUntil) >= 0) {
      L->nudgeMul = 1.0f;
      L->nudgeUntil = 0;
    }

    if (L->freezeUntil && (int32_t)(now - L->freezeUntil) < 0) {
      L->nextDue = now + 10;
      continue;
    } else if (L->freezeUntil && (int32_t)(now - L->freezeUntil) >= 0) {
      L->freezeUntil = 0;
    }

    if (L->burstRemain > 0) {
      if ((int32_t)(now - L->nextFree) >= 0) {
        coilOn(*L);
        L->burstRemain--;
        L->nextDue = now + BURST_TARGET_MS;
        continue;
      }
    }

    if (!L->running) continue;
    while ((int32_t)(now - L->nextDue) >= 0) {
      if ((int32_t)(now - L->nextFree) >= 0) {
        coilOn(*L);
      } else {
        L->nextDue = L->nextFree;
        break;
      }
      scheduleNext(*L, now);
      if ((int32_t)(now - L->nextDue) < 0) break;
    }
  }
}
