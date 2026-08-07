// Demo vitals — plausible numbers for a baby with no live device.
//
// WHY THIS EXISTS
// This is a prototype that gets shown before the hardware is on the bench.
// An all-grey "No signal" dashboard demos badly, so this fills the gap with
// realistic neonatal values.
//
// THE RULE THAT MAKES IT SAFE
// Demo values NEVER override a live reading. isDemoCandidate() substitutes
// only where vitalStatus() has already returned 'unmonitored' — no device, or
// one that stopped reporting. The moment real data arrives it wins, with no
// flag to remember to flip. Every substituted reading carries `is_demo: true`
// and every screen that renders one shows a badge, because fabricated numbers
// on a patient monitor must be impossible to mistake for measurements.
//
// Turn it off completely with VITE_DEMO_MODE=false in .env.

export const DEMO_MODE = import.meta.env.VITE_DEMO_MODE !== 'false';

// How long one set of values holds before drifting. Real vitals wander
// slowly; re-rolling per render would look like noise, not a patient.
export const DEMO_DRIFT_MS = 5 * 60 * 1000;

// Healthy term-neonate ranges. Deliberately kept inside normal limits — the
// demo should never manufacture an alert that sends someone looking for a
// baby who does not exist.
const RANGE = {
  hr:     { min: 120, max: 160 },
  spo2:   { min: 95,  max: 100 },
  temp:   { min: 36.5, max: 37.4 },
  motion: { min: 0.02, max: 0.45 },
};

// FNV-1a. Small, fast, and good enough to turn a uuid into a seed.
function hashString(str) {
  let h = 0x811c9dc5;
  for (let i = 0; i < str.length; i++) {
    h ^= str.charCodeAt(i);
    h = Math.imul(h, 0x01000193);
  }
  return h >>> 0;
}

// mulberry32 — deterministic PRNG. Same seed always gives the same sequence,
// which is what keeps a baby's numbers steady between renders.
function rng(seed) {
  let a = seed >>> 0;
  return function next() {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function lerp(min, max, t) {
  return min + (max - min) * t;
}

function clamp(v, min, max) {
  return v < min ? min : v > max ? max : v;
}

// Each baby gets a personal baseline that never changes, so Baby A is
// consistently a little faster than Baby B. Vitals that wander around a
// personal centre read as a patient; vitals redrawn from the full range every
// cycle read as a random number generator.
function baselineFor(babyId) {
  const r = rng(hashString(`baseline:${babyId}`));
  return {
    hr:     lerp(RANGE.hr.min + 6,   RANGE.hr.max - 6,   r()),
    spo2:   lerp(RANGE.spo2.min + 1, RANGE.spo2.max - 1, r()),
    temp:   lerp(RANGE.temp.min + 0.2, RANGE.temp.max - 0.2, r()),
    motion: lerp(RANGE.motion.min, RANGE.motion.max, r()),
  };
}

/**
 * A synthetic vitals row for one baby.
 *
 * Shaped exactly like a real `vitals` row so it flows through vitalStatus()
 * and every component untouched — plus `is_demo` so the UI can badge it.
 *
 * @param {string} babyId
 * @param {number} nonce  bump to force new values (the Refresh button)
 * @param {number} now    injectable for tests
 */
export function demoVitalFor(babyId, nonce = 0, now = Date.now()) {
  const bucket = Math.floor(now / DEMO_DRIFT_MS);
  const base = baselineFor(babyId);
  const r = rng(hashString(`${babyId}:${bucket}:${nonce}`));

  // Drift is a signed fraction of the band, not a fresh draw across it.
  const wobble = (spread) => (r() - 0.5) * 2 * spread;

  const hr     = clamp(base.hr     + wobble(7),    RANGE.hr.min,     RANGE.hr.max);
  const spo2   = clamp(base.spo2   + wobble(1.5),  RANGE.spo2.min,   RANGE.spo2.max);
  const temp   = clamp(base.temp   + wobble(0.25), RANGE.temp.min,   RANGE.temp.max);
  const motion = clamp(base.motion + wobble(0.12), RANGE.motion.min, RANGE.motion.max);

  return {
    // Prefixed so a demo row is never mistaken for a database id, and keyed
    // on the bucket so React sees a genuinely new row when values drift.
    id: `demo-${babyId}-${bucket}-${nonce}`,
    baby_id: babyId,
    heart_rate: Math.round(hr),
    spo2: Math.round(spo2),
    temperature: Math.round(temp * 10) / 10,
    motion: Math.round(motion * 100) / 100,
    sensor_ok: true,
    is_abnormal: false,
    // Stamped now, so vitalStatus() reads it as fresh rather than stale.
    recorded_at: new Date(now).toISOString(),
    is_demo: true,
  };
}

/**
 * Fill gaps in a babyId -> vitals map with demo readings.
 *
 * `isStale` is injected rather than imported so this stays a pure function
 * and the staleness rule keeps living in exactly one place (vitalStatus).
 *
 * @param {Array}    babies   rows with an `id`
 * @param {Object}   vitals   babyId -> real vitals row
 * @param {number}   nonce
 * @param {Function} isStale  (vital) => boolean, true when it needs replacing
 */
export function fillWithDemoVitals(babies, vitals, nonce, isStale) {
  if (!DEMO_MODE) return vitals;

  const out = { ...vitals };
  for (const baby of babies || []) {
    if (!baby?.id) continue;
    if (isStale(out[baby.id])) {
      out[baby.id] = demoVitalFor(baby.id, nonce);
    }
  }
  return out;
}
