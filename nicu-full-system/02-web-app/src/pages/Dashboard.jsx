import { useEffect, useState, useCallback, useRef, useMemo } from 'react';
import { Bell, Plus, RefreshCw } from 'lucide-react';
import { supabase } from '../supabaseClient';
import { useAuth } from '../context/AuthContext';
import { vitalStatus, STATUS } from '../lib/vitalStatus';
import { DEMO_MODE, DEMO_DRIFT_MS, DEMO_DRIFT_LABEL, fillWithDemoVitals } from '../lib/demoVitals';
import CircularProgress from '../components/CircularProgress';
import BabyCard from '../components/BabyCard';
import BottomNav from '../components/BottomNav';

// How often to re-evaluate staleness while the page sits idle. Without this
// a baby whose device goes quiet would keep showing its last known status
// until some other event forced a re-render.
const STALENESS_TICK_MS = 30 * 1000;

// One timer drives both staleness and demo drift. It has to fire at least as
// often as the demo bucket rolls over, otherwise fresh demo values would sit
// computed-but-unrendered until something else caused a re-render.
const TICK_MS = DEMO_MODE
  ? Math.min(STALENESS_TICK_MS, DEMO_DRIFT_MS)
  : STALENESS_TICK_MS;

export default function Dashboard() {
  const { profile } = useAuth();
  const [babies, setBabies] = useState([]);
  const [latestVitals, setLatestVitals] = useState({});
  const [activeAlertCount, setActiveAlertCount] = useState(0);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  // Bumped on a timer to force staleness recalculation. The value is read by
  // the demo-fill memo below, which is how demo vitals drift on their own.
  const [tick, setTick] = useState(0);

  // Bumped by the Refresh button to re-roll demo values on demand.
  const [demoNonce, setDemoNonce] = useState(0);

  // Lets the realtime handler check membership without re-subscribing every
  // time the vitals map changes.
  const babyIdsRef = useRef(new Set());

  const refreshAlertCount = useCallback(async (ids) => {
    if (!ids?.length) {
      setActiveAlertCount(0);
      return;
    }
    const { count, error: alertErr } = await supabase
      .from('alerts')
      .select('*', { count: 'exact', head: true })
      .in('baby_id', ids)
      .eq('status', 'active');

    if (alertErr) {
      setError(alertErr.message);
      return;
    }
    setActiveAlertCount(count || 0);
  }, []);

  const loadData = useCallback(async () => {
    setLoading(true);
    setError('');

    // Which babies this user is allowed to see depends on their role.
    const link =
      profile.role === 'parent'
        ? { table: 'baby_parents', column: 'parent_id' }
        : { table: 'baby_care_team', column: 'staff_id' };

    const { data: linkRows, error: linkErr } = await supabase
      .from(link.table)
      .select('babies(*)')
      .eq(link.column, profile.id);

    if (linkErr) {
      setError(linkErr.message);
      setLoading(false);
      return;
    }

    const babyRows = (linkRows || []).map((r) => r.babies).filter(Boolean);
    setBabies(babyRows);
    babyIdsRef.current = new Set(babyRows.map((b) => b.id));

    const babyIds = babyRows.map((b) => b.id);
    if (!babyIds.length) {
      setLatestVitals({});
      setActiveAlertCount(0);
      setLoading(false);
      return;
    }

    // One round trip for every baby's latest reading. This used to be a
    // separate query per baby inside a loop.
    const { data: vitalRows, error: vitalsErr } = await supabase.rpc(
      'latest_vitals_for_babies',
      { p_baby_ids: babyIds }
    );

    if (vitalsErr) {
      setError(vitalsErr.message);
    } else {
      const vitalsMap = {};
      for (const row of vitalRows || []) vitalsMap[row.baby_id] = row;
      setLatestVitals(vitalsMap);
    }

    await refreshAlertCount(babyIds);
    setLoading(false);
  }, [profile, refreshAlertCount]);

  useEffect(() => {
    if (profile) loadData();
  }, [profile, loadData]);

  // Staleness ticker.
  useEffect(() => {
    const id = setInterval(() => setTick((t) => t + 1), TICK_MS);
    return () => clearInterval(id);
  }, []);

  // Live updates. Keyed on the baby id list so the channel is rebuilt only
  // when the set of babies actually changes, not on every vitals update.
  const babyIdKey = babies.map((b) => b.id).sort().join(',');

  useEffect(() => {
    if (!babyIdKey) return;
    const ids = babyIdKey.split(',');

    const channel = supabase
      .channel('dashboard-live')
      .on(
        'postgres_changes',
        { event: 'INSERT', schema: 'public', table: 'vitals' },
        ({ new: row }) => {
          // RLS already limits what reaches us, but a user may be on several
          // babies' care teams while this page shows a subset.
          if (!row || !babyIdsRef.current.has(row.baby_id)) return;

          setLatestVitals((prev) => {
            const current = prev[row.baby_id];
            // Realtime does not guarantee ordering; never go backwards.
            if (
              current &&
              new Date(current.recorded_at).getTime() >=
                new Date(row.recorded_at).getTime()
            ) {
              return prev;
            }
            return { ...prev, [row.baby_id]: row };
          });
        }
      )
      .on(
        'postgres_changes',
        { event: '*', schema: 'public', table: 'alerts' },
        () => refreshAlertCount(ids)
      )
      .subscribe();

    return () => supabase.removeChannel(channel);
  }, [babyIdKey, refreshAlertCount]);

  // ---- Demo fill ----
  // Substitutes plausible values only where a baby is genuinely unmonitored.
  // Live data always wins, so this cannot mask a real device going quiet.
  const displayVitals = useMemo(
    () =>
      fillWithDemoVitals(
        babies,
        latestVitals,
        demoNonce,
        (v) => vitalStatus(v) === STATUS.UNMONITORED
      ),
    // tick is a dependency on purpose: it re-runs this on the staleness timer,
    // which is what lets demo values drift on their own every DEMO_DRIFT_MS.
    [babies, latestVitals, demoNonce, tick]
  );

  const demoCount = babies.filter((b) => displayVitals[b.id]?.is_demo).length;

  // ---- Derived counts ----
  // Everything reads the demo-filled map, so the ring and the stat cards match
  // what the baby cards show. When DEMO_MODE is off, fillWithDemoVitals returns
  // the live map untouched, which restores the honest behaviour on its own: an
  // offline ward reads "—" rather than 100%.
  const statuses = babies.map((b) => vitalStatus(displayVitals[b.id]));
  const stableCount = statuses.filter((s) => s === STATUS.STABLE).length;
  const abnormalCount = statuses.filter((s) => s === STATUS.ABNORMAL).length;
  const unmonitoredCount = statuses.filter((s) => s === STATUS.UNMONITORED).length;

  // Only babies we are actually receiving data for can count toward a health
  // score. Scoring over all babies is what made an offline ward read 100%.
  const monitoredCount = stableCount + abnormalCount;
  const healthScore = monitoredCount
    ? Math.round((stableCount / monitoredCount) * 100)
    : null;

  const ringColor =
    monitoredCount === 0 ? '#CBD8AC' : abnormalCount > 0 ? '#E8604C' : '#B9D93A';

  return (
    <div className="max-w-md mx-auto pb-24 px-5 pt-6">
      {/* Header */}
      <div className="flex items-center justify-between mb-6">
        <div className="flex items-center gap-3">
          <div className="w-11 h-11 rounded-full bg-sage-200 flex items-center justify-center font-semibold text-ink">
            {profile?.full_name?.[0] || '?'}
          </div>
          <div>
            <p className="text-sm text-muted">Hi,</p>
            <p className="font-display font-semibold text-ink">{profile?.full_name}</p>
          </div>
        </div>
        <div className="flex items-center gap-2">
          <button
            onClick={() => {
              setDemoNonce((n) => n + 1);
              loadData();
            }}
            aria-label="Refresh vitals"
            title="Refresh vitals"
            className="w-10 h-10 rounded-full bg-card shadow-soft flex items-center justify-center active:scale-95 transition-transform"
          >
            <RefreshCw size={17} className={loading ? 'animate-spin' : ''} />
          </button>
          <button className="relative w-10 h-10 rounded-full bg-card shadow-soft flex items-center justify-center">
            <Bell size={18} />
            {activeAlertCount > 0 && (
              <span className="absolute -top-1 -right-1 bg-alert text-white text-[10px] rounded-full w-4 h-4 flex items-center justify-center">
                {activeAlertCount}
              </span>
            )}
          </button>
        </div>
      </div>

      {/* Fabricated numbers on a patient monitor have to be labelled. This
          banner is the thing that keeps the demo honest. */}
      {DEMO_MODE && demoCount > 0 && (
        <div className="card bg-warn/10 mb-4 py-3 px-4">
          <p className="text-xs font-semibold text-ink">
            Demo data · {demoCount} {demoCount === 1 ? 'baby' : 'babies'}
          </p>
          <p className="text-[11px] text-muted mt-0.5 leading-relaxed">
            Simulated values, not measurements — no device is reporting. Drifts
            every {DEMO_DRIFT_LABEL}, or tap refresh. Live readings replace them
            automatically.
          </p>
        </div>
      )}

      <h1 className="font-display text-2xl font-semibold text-ink mb-4">
        {profile?.role === 'parent' ? 'Your Baby' : 'Today'}
      </h1>

      {error && (
        <div className="card bg-alert/10 mb-4">
          <p className="text-sm text-alert font-medium">Could not load data</p>
          <p className="text-xs text-muted mt-1">{error}</p>
        </div>
      )}

      {/* Overall ring */}
      <div className="card flex flex-col items-center mb-4">
        <CircularProgress
          value={healthScore ?? 0}
          centerLabel={healthScore === null ? '—' : `${healthScore}%`}
          centerSubLabel="Overall health"
          color={ringColor}
        />
        <p className="text-sm text-muted mt-3 text-center">
          {monitoredCount === 0
            ? 'No devices reporting'
            : `${stableCount} of ${monitoredCount} monitored stable`}
        </p>
        {unmonitoredCount > 0 && (
          <p className="text-xs text-warn font-medium mt-1">
            {unmonitoredCount} not reporting
          </p>
        )}
      </div>

      {/* Quick stat cards */}
      <div className="grid grid-cols-2 gap-3 mb-6">
        <div className="card bg-sage-100">
          <p className="text-xs text-muted mb-1">Active alerts</p>
          <p className="font-display text-2xl font-semibold text-ink">{activeAlertCount}</p>
        </div>
        <div className="card bg-tan">
          <p className="text-xs text-muted mb-1">Babies reporting</p>
          <p className="font-display text-2xl font-semibold text-ink">
            {monitoredCount}
            <span className="text-base text-muted">/{babies.length}</span>
          </p>
        </div>
      </div>

      {/* Baby list */}
      <div className="flex items-center justify-between mb-3">
        <h2 className="font-display font-semibold text-ink">
          {profile?.role === 'parent' ? 'Vitals' : 'Babies'}
        </h2>
        {profile?.role !== 'parent' && (
          <button className="text-sm text-muted flex items-center gap-1">
            <Plus size={14} /> Add
          </button>
        )}
      </div>

      {loading ? (
        <p className="text-muted text-sm">Loading…</p>
      ) : babies.length === 0 ? (
        <p className="text-muted text-sm">
          No babies assigned yet.{' '}
          {profile?.role !== 'parent' &&
            'Add an entry to baby_care_team from the Supabase dashboard.'}
        </p>
      ) : (
        <div className="flex flex-col gap-3">
          {babies.map((baby, i) => (
            <BabyCard
              key={baby.id}
              baby={baby}
              latestVital={displayVitals[baby.id]}
              status={statuses[i]}
            />
          ))}
        </div>
      )}

      <BottomNav />
    </div>
  );
}
