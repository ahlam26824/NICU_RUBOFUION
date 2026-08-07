import { useEffect, useState } from 'react';
import { supabase } from '../supabaseClient';
import { useAuth } from '../context/AuthContext';
import BottomNav from '../components/BottomNav';

export default function Settings() {
  const { profile, signOut } = useAuth();
  const [fullName, setFullName] = useState('');
  const [phone, setPhone] = useState('');
  const [notif, setNotif] = useState({
    push_enabled: true,
    email_enabled: true,
    sms_enabled: false,
    critical_only: false,
  });
  const [saved, setSaved] = useState(false);

  useEffect(() => {
    if (profile) {
      setFullName(profile.full_name || '');
      setPhone(profile.phone || '');
      loadNotifSettings();
    }
  }, [profile]);

  async function loadNotifSettings() {
    const { data } = await supabase
      .from('notification_settings')
      .select('*')
      .eq('user_id', profile.id)
      .single();
    if (data) setNotif(data);
  }

  async function saveProfile() {
    await supabase.from('profiles').update({ full_name: fullName, phone }).eq('id', profile.id);
    await supabase.from('notification_settings').upsert({ user_id: profile.id, ...notif });
    setSaved(true);
    setTimeout(() => setSaved(false), 1500);
  }

  function toggle(key) {
    setNotif((n) => ({ ...n, [key]: !n[key] }));
  }

  return (
    <div className="max-w-md mx-auto pb-24 px-5 pt-6">
      <h1 className="font-display text-2xl font-semibold text-ink mb-6">Settings</h1>

      <div className="card mb-4">
        <p className="font-display font-semibold text-ink mb-4">Profile</p>
        <label className="text-xs text-muted">Full name</label>
        <input
          value={fullName}
          onChange={(e) => setFullName(e.target.value)}
          className="w-full rounded-2xl border border-sage-300 px-4 py-3 text-sm mt-1 mb-3"
        />
        <label className="text-xs text-muted">Phone</label>
        <input
          value={phone}
          onChange={(e) => setPhone(e.target.value)}
          className="w-full rounded-2xl border border-sage-300 px-4 py-3 text-sm mt-1"
        />
        <p className="text-xs text-muted mt-3">Role: <span className="capitalize font-medium text-ink">{profile?.role}</span></p>
      </div>

      <div className="card mb-4">
        <p className="font-display font-semibold text-ink mb-4">Notifications</p>
        <ToggleRow label="Push notifications" checked={notif.push_enabled} onChange={() => toggle('push_enabled')} />
        <ToggleRow label="Email alerts" checked={notif.email_enabled} onChange={() => toggle('email_enabled')} />
        <ToggleRow label="SMS alerts" checked={notif.sms_enabled} onChange={() => toggle('sms_enabled')} />
        <ToggleRow label="Critical alerts only" checked={notif.critical_only} onChange={() => toggle('critical_only')} />
      </div>

      <button onClick={saveProfile} className="btn-primary w-full mb-3">
        {saved ? 'Saved ✓' : 'Save changes'}
      </button>

      <button onClick={signOut} className="w-full text-alert text-sm font-medium py-2">
        Sign out
      </button>

      <BottomNav />
    </div>
  );
}

function ToggleRow({ label, checked, onChange }) {
  return (
    <div className="flex items-center justify-between py-2">
      <span className="text-sm text-ink">{label}</span>
      <button
        onClick={onChange}
        className={`w-11 h-6 rounded-full transition-colors relative ${checked ? 'bg-lime-dark' : 'bg-sage-300'}`}
      >
        <span
          className={`absolute top-0.5 w-5 h-5 rounded-full bg-white transition-transform ${
            checked ? 'translate-x-5' : 'translate-x-0.5'
          }`}
        />
      </button>
    </div>
  );
}
