// Shared vitals classification.
//
// The rule this file exists to enforce: missing data must never render as
// healthy. The dashboard used to compute `!vital?.is_abnormal`, which is
// `true` for a baby that has never reported at all — so a ward full of
// offline devices showed as 100% stable.

// How long a reading stays trustworthy.
// The firmware posts every ~2s on USB and every ~30s in battery mode, so
// two minutes of silence means something is actually wrong: device off,
// WiFi dropped, or battery flat.
export const STALE_AFTER_MS = 2 * 60 * 1000;

export const STATUS = {
  STABLE: 'stable',
  ABNORMAL: 'abnormal',
  UNMONITORED: 'unmonitored',
};

// Returns 'stable' | 'abnormal' | 'unmonitored'.
//
// 'unmonitored' deliberately covers three different failures — never
// reported, stopped reporting, and probe detached. They differ in cause but
// not in consequence: nobody is watching this baby, and none of them is
// evidence of health.
export function vitalStatus(vital) {
  if (!vital || !vital.recorded_at) return STATUS.UNMONITORED;

  const age = Date.now() - new Date(vital.recorded_at).getTime();
  if (!Number.isFinite(age) || age > STALE_AFTER_MS) return STATUS.UNMONITORED;

  // sensor_ok === false is the probe-detached case the firmware raises its
  // "Sensor signal lost" warning on. The numbers in that row are noise.
  if (vital.sensor_ok === false) return STATUS.UNMONITORED;

  return vital.is_abnormal ? STATUS.ABNORMAL : STATUS.STABLE;
}

// Short human-readable age, for the "last reading 3m ago" captions.
export function formatAge(timestamp) {
  if (!timestamp) return 'never';

  const seconds = Math.floor((Date.now() - new Date(timestamp).getTime()) / 1000);
  if (!Number.isFinite(seconds)) return 'unknown';
  if (seconds < 10) return 'just now';
  if (seconds < 60) return `${seconds}s ago`;

  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes}m ago`;

  const hours = Math.floor(minutes / 60);
  if (hours < 24) return `${hours}h ago`;

  return `${Math.floor(hours / 24)}d ago`;
}
