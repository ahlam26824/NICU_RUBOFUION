import { createClient } from '@supabase/supabase-js';

// Copy these two values from Supabase Project Settings > API into your .env file.
// Use the `anon public` key here — never the service_role key, which would be
// readable by anyone who opens the built bundle.
// VITE_SUPABASE_URL=https://xxxxx.supabase.co
// VITE_SUPABASE_ANON_KEY=eyJhbGciOi...

const supabaseUrl = import.meta.env.VITE_SUPABASE_URL;
const supabaseAnonKey = import.meta.env.VITE_SUPABASE_ANON_KEY;

export const supabase = createClient(supabaseUrl, supabaseAnonKey);
