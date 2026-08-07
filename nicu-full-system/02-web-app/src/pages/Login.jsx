import { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { supabase } from '../supabaseClient';

const ROLES = [
  { value: 'parent', label: 'Parent' },
  { value: 'nurse', label: 'Nurse' },
  { value: 'doctor', label: 'Doctor' },
];

export default function Login() {
  const navigate = useNavigate();
  const [mode, setMode] = useState('signin'); // 'signin' | 'signup'
  const [fullName, setFullName] = useState('');
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [role, setRole] = useState('parent');
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);

  async function handleSubmit(e) {
    e.preventDefault();
    setError('');
    setLoading(true);

    if (mode === 'signup') {
      const { error } = await supabase.auth.signUp({
        email,
        password,
        options: { data: { full_name: fullName, role } },
      });
      if (error) setError(error.message);
      else navigate('/dashboard');
    } else {
      const { error } = await supabase.auth.signInWithPassword({ email, password });
      if (error) setError(error.message);
      else navigate('/dashboard');
    }
    setLoading(false);
  }

  return (
    <div className="min-h-screen flex items-center justify-center px-6">
      <div className="w-full max-w-sm">
        <div className="text-center mb-8">
          <h1 className="font-display text-2xl font-semibold text-ink">NICU Care</h1>
          <p className="text-muted text-sm mt-1">Baby vitals, right in your pocket</p>
        </div>

        <div className="card">
          <div className="flex bg-sage-100 rounded-full p-1 mb-6">
            <button
              className={`flex-1 pill ${mode === 'signin' ? 'pill-active' : 'pill-inactive'}`}
              onClick={() => setMode('signin')}
              type="button"
            >
              Sign In
            </button>
            <button
              className={`flex-1 pill ${mode === 'signup' ? 'pill-active' : 'pill-inactive'}`}
              onClick={() => setMode('signup')}
              type="button"
            >
              Sign Up
            </button>
          </div>

          <form onSubmit={handleSubmit} className="flex flex-col gap-3">
            {mode === 'signup' && (
              <>
                <input
                  type="text"
                  placeholder="Full name"
                  value={fullName}
                  onChange={(e) => setFullName(e.target.value)}
                  required
                  className="rounded-2xl border border-sage-300 px-4 py-3 text-sm focus:outline-none focus:ring-2 focus:ring-lime-dark"
                />
                <div className="flex gap-2">
                  {ROLES.map((r) => (
                    <button
                      type="button"
                      key={r.value}
                      onClick={() => setRole(r.value)}
                      className={`flex-1 pill ${role === r.value ? 'pill-active' : 'pill-inactive'}`}
                    >
                      {r.label}
                    </button>
                  ))}
                </div>
              </>
            )}
            <input
              type="email"
              placeholder="Email"
              value={email}
              onChange={(e) => setEmail(e.target.value)}
              required
              className="rounded-2xl border border-sage-300 px-4 py-3 text-sm focus:outline-none focus:ring-2 focus:ring-lime-dark"
            />
            <input
              type="password"
              placeholder="Password"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              required
              minLength={6}
              className="rounded-2xl border border-sage-300 px-4 py-3 text-sm focus:outline-none focus:ring-2 focus:ring-lime-dark"
            />

            {error && <p className="text-alert text-sm">{error}</p>}

            <button type="submit" disabled={loading} className="btn-primary mt-2">
              {loading ? 'Please wait…' : mode === 'signup' ? 'Create account' : 'Sign in'}
            </button>
          </form>
        </div>
      </div>
    </div>
  );
}
