import { useEffect, useState, useCallback } from 'react';
import { supabase } from '../supabaseClient';
import { useAuth } from '../context/AuthContext';
import BottomNav from '../components/BottomNav';

const TABS = ['active', 'acknowledged', 'resolved'];

export default function Alerts() {
  const { profile } = useAuth();
  const [tab, setTab] = useState('active');
  const [alerts, setAlerts] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  const loadAlerts = useCallback(async () => {
    setError('');
    // RLS limits this to babies the signed-in user has access to.
    const { data, error: err } = await supabase
      .from('alerts')
      .select('*, babies(display_name, bed_number)')
      .eq('status', tab)
      .order('created_at', { ascending: false });

    if (err) setError(err.message);
    else setAlerts(data || []);
    setLoading(false);
  }, [tab]);

  // Load and subscribe together. Keyed on `tab` so the subscription callback
  // always closes over the tab currently being viewed.
  useEffect(() => {
    setLoading(true);
    loadAlerts();

    const channel = supabase
      .channel(`alerts-${tab}`)
      .on(
        'postgres_changes',
        { event: '*', schema: 'public', table: 'alerts' },
        () => loadAlerts()
      )
      .subscribe();

    return () => supabase.removeChannel(channel);
  }, [tab, loadAlerts]);

  async function acknowledge(alertId) {
    const { error: err } = await supabase
      .from('alerts')
      .update({
        status: 'acknowledged',
        acknowledged_by: profile.id,
        acknowledged_at: new Date().toISOString(),
      })
      .eq('id', alertId);

    if (err) setError(err.message);
    else loadAlerts();
  }

  async function resolve(alertId) {
    const { error: err } = await supabase
      .from('alerts')
      .update({ status: 'resolved' })
      .eq('id', alertId);

    if (err) setError(err.message);
    else loadAlerts();
  }

  return (
    <div className="max-w-md mx-auto pb-24 px-5 pt-6">
      <h1 className="font-display text-2xl font-semibold text-ink mb-4">Alerts</h1>

      <div className="flex bg-sage-100 rounded-full p-1 mb-5">
        {TABS.map((t) => (
          <button
            key={t}
            onClick={() => setTab(t)}
            className={`flex-1 pill capitalize ${tab === t ? 'pill-active' : 'pill-inactive'}`}
          >
            {t}
          </button>
        ))}
      </div>

      {error && (
        <div className="card bg-alert/10 mb-4">
          <p className="text-sm text-alert font-medium">Something went wrong</p>
          <p className="text-xs text-muted mt-1">{error}</p>
        </div>
      )}

      {loading ? (
        <p className="text-muted text-sm">Loading…</p>
      ) : alerts.length === 0 ? (
        <p className="text-muted text-sm">No alerts in this category.</p>
      ) : (
        <div className="flex flex-col gap-3">
          {alerts.map((a) => (
            <div key={a.id} className="card">
              <div className="flex items-center justify-between mb-2">
                <p className="font-medium text-ink">
                  {a.babies?.display_name || 'Unknown baby'}
                </p>
                <span
                  className={`text-xs px-2 py-1 rounded-full ${
                    a.severity === 'critical' ? 'bg-alert/10 text-alert' : 'bg-warn/10 text-warn'
                  }`}
                >
                  {a.severity}
                </span>
              </div>
              <p className="text-sm text-ink mb-1">{a.reason}</p>
              <p className="text-xs text-muted mb-3">{new Date(a.created_at).toLocaleString()}</p>

              {profile?.role !== 'parent' && a.status !== 'resolved' && (
                <div className="flex gap-2">
                  {a.status === 'active' && (
                    <button
                      onClick={() => acknowledge(a.id)}
                      className="text-xs px-3 py-1.5 rounded-full bg-sage-100 text-ink font-medium"
                    >
                      Acknowledge
                    </button>
                  )}
                  <button
                    onClick={() => resolve(a.id)}
                    className="text-xs px-3 py-1.5 rounded-full bg-lime text-ink font-medium"
                  >
                    Mark Resolved
                  </button>
                </div>
              )}
            </div>
          ))}
        </div>
      )}

      <BottomNav />
    </div>
  );
}
