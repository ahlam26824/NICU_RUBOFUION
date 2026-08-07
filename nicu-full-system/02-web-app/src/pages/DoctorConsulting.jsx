import { useEffect, useState } from 'react';
import { Search, Video, Building2, Check } from 'lucide-react';
import { supabase } from '../supabaseClient';
import { useAuth } from '../context/AuthContext';
import BottomNav from '../components/BottomNav';

export default function DoctorConsulting() {
  const { profile } = useAuth();
  const [doctors, setDoctors] = useState([]);
  const [search, setSearch] = useState('');
  const [mode, setMode] = useState('video');
  const [selectedDoctor, setSelectedDoctor] = useState(null);
  const [babies, setBabies] = useState([]);
  const [selectedBaby, setSelectedBaby] = useState(null);
  const [date, setDate] = useState('');
  const [time, setTime] = useState('');
  const [confirming, setConfirming] = useState(false);
  const [confirmed, setConfirmed] = useState(false);
  const [error, setError] = useState('');

  useEffect(() => {
    loadDoctors();
    if (profile?.role === 'parent') loadMyBabies();
  }, [profile]);

  async function loadDoctors() {
    // Via RPC, not `from('profiles')`. RLS on profiles only lets a user read
    // their own row or — for staff — every row, so a parent querying the
    // table directly always got an empty list. list_doctors() is a security
    // definer function returning just the columns a booking screen needs,
    // so no phone numbers are exposed.
    const { data, error: err } = await supabase.rpc('list_doctors');
    if (err) setError(err.message);
    else setDoctors(data || []);
  }

  async function loadMyBabies() {
    const { data, error: err } = await supabase
      .from('baby_parents')
      .select('babies(*)')
      .eq('parent_id', profile.id);

    if (err) {
      setError(err.message);
      return;
    }
    const list = (data || []).map((r) => r.babies).filter(Boolean);
    setBabies(list);
    if (list.length) setSelectedBaby(list[0].id);
  }

  const filteredDoctors = doctors.filter((d) =>
    (d.full_name || '').toLowerCase().includes(search.toLowerCase())
  );

  async function handleConfirm() {
    if (!selectedDoctor || !selectedBaby || !date || !time) return;
    setConfirming(true);
    setError('');
    const scheduledAt = new Date(`${date}T${time}`).toISOString();

    const { error: err } = await supabase.from('consultations').insert({
      baby_id: selectedBaby,
      doctor_id: selectedDoctor.id,
      parent_id: profile.id,
      mode,
      scheduled_at: scheduledAt,
    });

    setConfirming(false);
    if (err) {
      setError(err.message);
      return;
    }
    setConfirmed(true);
    setTimeout(() => {
      setConfirmed(false);
      setSelectedDoctor(null);
    }, 2000);
  }

  return (
    <div className="max-w-md mx-auto pb-24 px-5 pt-6">
      <h1 className="font-display text-2xl font-semibold text-ink mb-4">Online Consulting</h1>

      {error && (
        <div className="card bg-alert/10 mb-4">
          <p className="text-sm text-alert font-medium">Something went wrong</p>
          <p className="text-xs text-muted mt-1">{error}</p>
        </div>
      )}

      <div className="relative mb-4">
        <Search size={16} className="absolute left-4 top-1/2 -translate-y-1/2 text-muted" />
        <input
          value={search}
          onChange={(e) => setSearch(e.target.value)}
          placeholder="Search for a doctor"
          className="w-full rounded-2xl border border-sage-300 pl-11 pr-4 py-3 text-sm focus:outline-none focus:ring-2 focus:ring-lime-dark"
        />
      </div>

      <div className="flex gap-2 mb-5">
        <button
          onClick={() => setMode('video')}
          className={`flex-1 pill flex items-center justify-center gap-2 ${
            mode === 'video' ? 'pill-active' : 'pill-inactive'
          }`}
        >
          <Video size={15} /> Video
        </button>
        <button
          onClick={() => setMode('clinic')}
          className={`flex-1 pill flex items-center justify-center gap-2 ${
            mode === 'clinic' ? 'pill-active' : 'pill-inactive'
          }`}
        >
          <Building2 size={15} /> Clinic
        </button>
      </div>

      <h2 className="font-display font-semibold text-ink mb-3">Available doctors</h2>
      <div className="flex flex-col gap-3 mb-6">
        {filteredDoctors.map((doc) => (
          <button
            key={doc.id}
            onClick={() => setSelectedDoctor(doc)}
            className={`card flex items-center gap-3 text-left ${
              selectedDoctor?.id === doc.id ? 'ring-2 ring-lime-dark' : ''
            }`}
          >
            <div className="w-11 h-11 rounded-full bg-sage-200 flex items-center justify-center font-semibold text-ink flex-shrink-0">
              {doc.full_name?.[0] || '?'}
            </div>
            <div>
              <p className="font-medium text-ink text-sm">{doc.full_name}</p>
              <p className="text-xs text-muted">{doc.specialty || 'General'}</p>
            </div>
          </button>
        ))}
        {filteredDoctors.length === 0 && (
          <p className="text-muted text-sm">
            {search
              ? 'No doctor matches that name.'
              : 'No doctors available yet. A user must sign up with the Doctor role first.'}
          </p>
        )}
      </div>

      {selectedDoctor && (
        <div className="card">
          <p className="font-display font-semibold text-ink mb-3">
            Book with {selectedDoctor.full_name}
          </p>

          {babies.length > 1 && (
            <select
              value={selectedBaby || ''}
              onChange={(e) => setSelectedBaby(e.target.value)}
              className="w-full rounded-2xl border border-sage-300 px-4 py-3 text-sm mb-3"
            >
              {babies.map((b) => (
                <option key={b.id} value={b.id}>
                  {b.display_name}
                </option>
              ))}
            </select>
          )}

          <div className="flex gap-2 mb-3">
            <input
              type="date"
              value={date}
              onChange={(e) => setDate(e.target.value)}
              className="flex-1 rounded-2xl border border-sage-300 px-4 py-3 text-sm"
            />
            <input
              type="time"
              value={time}
              onChange={(e) => setTime(e.target.value)}
              className="flex-1 rounded-2xl border border-sage-300 px-4 py-3 text-sm"
            />
          </div>

          <button
            onClick={handleConfirm}
            disabled={confirming || !date || !time}
            className="btn-primary w-full flex items-center justify-center gap-2"
          >
            {confirmed ? (
              <>
                <Check size={16} /> Confirmed
              </>
            ) : confirming ? (
              'Booking…'
            ) : (
              'Confirm'
            )}
          </button>
        </div>
      )}

      <BottomNav />
    </div>
  );
}
