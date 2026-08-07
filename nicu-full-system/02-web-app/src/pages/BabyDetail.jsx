import { useEffect, useState, useCallback, useMemo } from 'react';
import { useParams, useNavigate } from 'react-router-dom';
import { ArrowLeft, Heart, Wind, Thermometer, Activity, WifiOff, RefreshCw } from 'lucide-react';
import { supabase } from '../supabaseClient';
import { vitalStatus, STATUS, formatAge } from '../lib/vitalStatus';
import { DEMO_MODE, DEMO_DRIFT_MS, demoVitalFor } from '../lib/demoVitals';
import CircularProgress from '../components/CircularProgress';
import BottomNav from '../components/BottomNav';

const STALENESS_TICK_MS = 30 * 1000;

export default function BabyDetail() {
  const { id } = useParams();
  const navigate = useNavigate();
  const [baby, setBaby] = useState(null);
  const [liveVital, setLiveVital] = useState(null);
  const [recentAlerts, setRecentAlerts] = useState([]);
  const [error, setError] = useState('');
  const [tick, setTick] = useState(0);
  const [demoNonce, setDemoNonce] = useState(0);

  const loadBaby = useCallback(async () => {
    const { data, error: err } = await supabase
      .from('babies')
      .select('*')
      .eq('id', id)
      .maybeSingle();
    if (err) setError(err.message);
    else setBaby(data);
  }, [id]);

  const loadLatestVital = useCallback(async () => {
    // maybeSingle, not single: a baby with no readings yet is a normal state,
    // and single() would return a PGRST116 error for it.
    const { data, error: err } = await supabase
      .from('vitals')
      .select('*')
      .eq('baby_id', id)
      .order('recorded_at', { ascending: false })
      .limit(1)
      .maybeSingle();
    if (err) setError(err.message);
    else setLiveVital(data);
  }, [id]);

  const loadAlerts = useCallback(async () => {
    const { data, error: err } = await supabase
      .from('alerts')
      .select('*')
      .eq('baby_id', id)
      .order('created_at', { ascending: false })
      .limit(5);
    if (err) setError(err.message);
    else setRecentAlerts(data || []);
  }, [id]);

  useEffect(() => {
    loadBaby();
    loadLatestVital();
    loadAlerts();

    const channel = supabase
      .channel(`baby-${id}`)
      .on(
        'postgres_changes',
        { event: 'INSERT', schema: 'public', table: 'vitals', filter: `baby_id=eq.${id}` },
        ({ new: row }) =>
          setLiveVital((prev) =>
            prev &&
            new Date(prev.recorded_at).getTime() >= new Date(row.recorded_at).getTime()
              ? prev
              : row
          )
      )
      .on(
        'postgres_changes',
        { event: '*', schema: 'public', table: 'alerts', filter: `baby_id=eq.${id}` },
        () => loadAlerts()
      )
      .subscribe();

    return () => supabase.removeChannel(channel);
  }, [id, loadBaby, loadLatestVital, loadAlerts]);

  // Keeps the "last reading Xm ago" caption and the stale banner honest while
  // the page is open and idle.
  useEffect(() => {
    const t = setInterval(() => setTick((n) => n + 1), STALENESS_TICK_MS);
    return () => clearInterval(t);
  }, []);

  // Demo fill, same rule as the dashboard: substitute only when there is
  // genuinely nothing live. A real reading always wins.
  const vital = useMemo(() => {
    if (!DEMO_MODE || !id) return liveVital;
    if (vitalStatus(liveVital) !== STATUS.UNMONITORED) return liveVital;
    return demoVitalFor(id, demoNonce);
    // tick re-runs this on the staleness timer, which is what makes the
    // values drift on their own every DEMO_DRIFT_MS.
  }, [liveVital, id, demoNonce, tick]);

  const status = vitalStatus(vital);
  const unmonitored = status === STATUS.UNMONITORED;
  const isDemo = vital?.is_demo === true;

  // null rather than 0 — a missing reading must not paint a 0% ring, which
  // looks identical to a catastrophic desaturation.
  const spo2 = vital?.spo2 ?? null;
  const showSpo2 = spo2 !== null && !unmonitored;

  return (
    <div className="max-w-md mx-auto pb-24 px-5 pt-6">
      <button onClick={() => navigate(-1)} className="mb-4 text-ink">
        <ArrowLeft size={22} />
      </button>

      <div className="flex items-start justify-between gap-3 mb-1">
        <h1 className="font-display text-2xl font-semibold text-ink">
          {baby?.display_name || 'Loading…'}
        </h1>
        <button
          onClick={() => {
            setDemoNonce((n) => n + 1);
            loadLatestVital();
            loadAlerts();
          }}
          aria-label="Refresh vitals"
          title="Refresh vitals"
          className="w-10 h-10 rounded-full bg-card shadow-soft flex items-center justify-center flex-shrink-0 active:scale-95 transition-transform"
        >
          <RefreshCw size={17} />
        </button>
      </div>
      <p className="text-muted text-sm mb-6">
        Bed {baby?.bed_number || '—'} · Room {baby?.room_number || '—'}
      </p>

      {/* Simulated numbers are labelled before they are shown, not after. */}
      {isDemo && (
        <div className="card bg-warn/10 mb-4 py-3 px-4">
          <p className="text-xs font-semibold text-ink">Demo data</p>
          <p className="text-[11px] text-muted mt-0.5 leading-relaxed">
            Simulated values, not measurements — no device is reporting for this
            baby. Drifts every {Math.round(DEMO_DRIFT_MS / 60000)} min, or tap
            refresh. A live reading replaces these automatically.
          </p>
        </div>
      )}

      {error && (
        <div className="card bg-alert/10 mb-4">
          <p className="text-sm text-alert font-medium">Could not load data</p>
          <p className="text-xs text-muted mt-1">{error}</p>
        </div>
      )}

      {unmonitored && (
        <div className="card bg-warn/10 mb-4 flex items-start gap-3">
          <WifiOff size={18} className="text-warn flex-shrink-0 mt-0.5" />
          <div>
            <p className="text-sm font-medium text-ink">
              {!vital
                ? 'No readings yet'
                : vital.sensor_ok === false
                  ? 'Sensor signal lost'
                  : 'Device not reporting'}
            </p>
            <p className="text-xs text-muted mt-0.5">
              {!vital
                ? 'This baby has no recorded vitals. Check that the device is powered and configured.'
                : vital.sensor_ok === false
                  ? `Probe may be detached. Last reading ${formatAge(vital.recorded_at)}.`
                  : `Last reading ${formatAge(vital.recorded_at)}. Values below are not current.`}
            </p>
          </div>
        </div>
      )}

      <div className="card flex flex-col items-center mb-4">
        <CircularProgress
          value={showSpo2 ? spo2 : 0}
          centerLabel={showSpo2 ? `${spo2}%` : '--'}
          centerSubLabel="SpO2"
          color={!showSpo2 ? '#CBD8AC' : spo2 < 90 ? '#E8604C' : '#B9D93A'}
        />
        {vital?.is_abnormal && !unmonitored && (
          <p className="text-alert text-sm font-medium mt-3">⚠ Vitals out of normal range</p>
        )}
        {vital && !unmonitored && (
          <p className="text-xs text-muted mt-2">Updated {formatAge(vital.recorded_at)}</p>
        )}
      </div>

      <div className={`grid grid-cols-2 gap-3 mb-4 ${unmonitored ? 'opacity-40' : ''}`}>
        <VitalStat icon={Heart} label="Heart Rate" value={`${vital?.heart_rate ?? '--'} bpm`} color="text-alert" />
        <VitalStat icon={Wind} label="SpO2" value={`${vital?.spo2 ?? '--'}%`} color="text-good" />
        <VitalStat icon={Thermometer} label="Temperature" value={`${vital?.temperature ?? '--'}°C`} color="text-warn" />
        <VitalStat icon={Activity} label="Motion" value={vital?.motion?.toFixed?.(1) ?? '--'} color="text-ink" />
      </div>

      <h2 className="font-display font-semibold text-ink mb-3">Recent Alerts</h2>
      {recentAlerts.length === 0 ? (
        <p className="text-muted text-sm">No alerts — everything normal.</p>
      ) : (
        <div className="flex flex-col gap-2">
          {recentAlerts.map((a) => (
            <div key={a.id} className="card py-3 px-4">
              <p className="text-sm font-medium text-ink">{a.reason}</p>
              <p className="text-xs text-muted mt-1">
                {new Date(a.created_at).toLocaleString()} · {a.status}
              </p>
            </div>
          ))}
        </div>
      )}

      <BottomNav />
    </div>
  );
}

function VitalStat({ icon: Icon, label, value, color }) {
  return (
    <div className="card">
      <Icon size={18} className={color} />
      <p className="text-xs text-muted mt-2">{label}</p>
      <p className="font-display font-semibold text-ink">{value}</p>
    </div>
  );
}
