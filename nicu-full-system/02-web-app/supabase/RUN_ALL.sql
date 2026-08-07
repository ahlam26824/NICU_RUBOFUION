-- =====================================================================
--  NICU system - complete database setup, single paste
--  Generated from schema.sql + device_auth.sql + web_app_fixes.sql,
--  concatenated in the order they must run. All three are idempotent,
--  so re-running this whole file is safe.
-- =====================================================================

-- ==================== schema.sql ====================

-- =========================================================
-- NICU Monitoring App — Full Supabase Schema
-- =========================================================
-- Paste this entire file into Supabase Dashboard > SQL Editor and press "Run".
-- That creates every table, policy, function and trigger in one go.
-- Run the whole file at once — the top-to-bottom order matters.
-- =========================================================

-- ---------------------------------------------------------
-- 0. Extensions
-- ---------------------------------------------------------
create extension if not exists "uuid-ossp";

-- ---------------------------------------------------------
-- 1. PROFILES — role and details for every logged-in user
-- ---------------------------------------------------------
create table public.profiles (
  id uuid primary key references auth.users(id) on delete cascade,
  full_name text not null,
  role text not null check (role in ('nurse', 'doctor', 'parent')),
  phone text,
  avatar_url text,
  specialty text,              -- doctors only (e.g. "Neonatologist")
  created_at timestamptz not null default now()
);

alter table public.profiles enable row level security;

-- A profile is created automatically on signup. The role arrives as user metadata
-- from the signup form.
create function public.handle_new_user()
returns trigger
language plpgsql
security definer set search_path = public
as $$
begin
  insert into public.profiles (id, full_name, role)
  values (
    new.id,
    coalesce(new.raw_user_meta_data->>'full_name', 'Unnamed'),
    coalesce(new.raw_user_meta_data->>'role', 'parent')
  );
  return new;
end;
$$;

create trigger on_auth_user_created
  after insert on auth.users
  for each row execute procedure public.handle_new_user();

-- ---------------------------------------------------------
-- 2. BABIES — one profile row per NICU baby
-- ---------------------------------------------------------
create table public.babies (
  id uuid primary key default uuid_generate_v4(),
  baby_code text not null unique,        -- matches BABY_CODE in the firmware (e.g. "baby_01")
  display_name text not null,             -- e.g. "Baby of Rahim"
  date_of_birth date,
  gestational_age_weeks numeric,
  birth_weight_grams numeric,
  bed_number text,
  room_number text,
  is_active boolean not null default true,
  created_at timestamptz not null default now()
);

alter table public.babies enable row level security;

-- ---------------------------------------------------------
-- 3. CARE TEAM — which nurse/doctor may see which baby
-- ---------------------------------------------------------
create table public.baby_care_team (
  id uuid primary key default uuid_generate_v4(),
  baby_id uuid not null references public.babies(id) on delete cascade,
  staff_id uuid not null references public.profiles(id) on delete cascade,
  assigned_at timestamptz not null default now(),
  unique (baby_id, staff_id)
);

alter table public.baby_care_team enable row level security;

-- ---------------------------------------------------------
-- 4. PARENT LINK — which parent is linked to which baby
-- ---------------------------------------------------------
create table public.baby_parents (
  id uuid primary key default uuid_generate_v4(),
  baby_id uuid not null references public.babies(id) on delete cascade,
  parent_id uuid not null references public.profiles(id) on delete cascade,
  relation text default 'parent',        -- 'mother' / 'father' / 'guardian'
  linked_at timestamptz not null default now(),
  unique (baby_id, parent_id)
);

alter table public.baby_parents enable row level security;

-- ---------------------------------------------------------
-- 5. VITALS — every reading posted by a device
-- ---------------------------------------------------------
create table public.vitals (
  id uuid primary key default uuid_generate_v4(),
  baby_id uuid not null references public.babies(id) on delete cascade,
  heart_rate int,
  spo2 int,
  temperature numeric,
  motion numeric,
  sensor_ok boolean default true,
  is_abnormal boolean default false,
  recorded_at timestamptz not null default now()
);

alter table public.vitals enable row level security;
create index idx_vitals_baby_time on public.vitals (baby_id, recorded_at desc);

-- ---------------------------------------------------------
-- 6. ALERTS — raised when a reading crosses a critical threshold
-- ---------------------------------------------------------
create table public.alerts (
  id uuid primary key default uuid_generate_v4(),
  baby_id uuid not null references public.babies(id) on delete cascade,
  reason text not null,
  severity text not null default 'critical' check (severity in ('warning', 'critical')),
  status text not null default 'active' check (status in ('active', 'acknowledged', 'resolved')),
  acknowledged_by uuid references public.profiles(id),
  acknowledged_at timestamptz,
  created_at timestamptz not null default now()
);

alter table public.alerts enable row level security;
create index idx_alerts_baby_time on public.alerts (baby_id, created_at desc);

-- ---------------------------------------------------------
-- 7. CONSULTATIONS — a parent booking time with a doctor
-- ---------------------------------------------------------
create table public.consultations (
  id uuid primary key default uuid_generate_v4(),
  baby_id uuid not null references public.babies(id) on delete cascade,
  doctor_id uuid not null references public.profiles(id),
  parent_id uuid not null references public.profiles(id),
  mode text not null default 'video' check (mode in ('video', 'clinic')),
  scheduled_at timestamptz not null,
  status text not null default 'pending' check (status in ('pending', 'confirmed', 'completed', 'cancelled')),
  notes text,
  created_at timestamptz not null default now()
);

alter table public.consultations enable row level security;

-- ---------------------------------------------------------
-- 8. NOTIFICATION SETTINGS — backs the Settings page
-- ---------------------------------------------------------
create table public.notification_settings (
  user_id uuid primary key references public.profiles(id) on delete cascade,
  push_enabled boolean not null default true,
  email_enabled boolean not null default true,
  sms_enabled boolean not null default false,
  critical_only boolean not null default false,
  updated_at timestamptz not null default now()
);

alter table public.notification_settings enable row level security;

-- =========================================================
-- HELPER FUNCTIONS (keep the RLS policies readable)
-- security definer is what avoids infinite recursion inside a policy: without it,
-- a policy on a table would re-trigger the same policy while checking access.
-- =========================================================

create function public.is_staff_for_baby(target_baby_id uuid)
returns boolean
language sql
security definer set search_path = public
stable
as $$
  select exists (
    select 1 from public.baby_care_team
    where baby_id = target_baby_id and staff_id = auth.uid()
  );
$$;

create function public.is_parent_of_baby(target_baby_id uuid)
returns boolean
language sql
security definer set search_path = public
stable
as $$
  select exists (
    select 1 from public.baby_parents
    where baby_id = target_baby_id and parent_id = auth.uid()
  );
$$;

create function public.current_role()
returns text
language sql
security definer set search_path = public
stable
as $$
  select role from public.profiles where id = auth.uid();
$$;

create function public.has_baby_access(target_baby_id uuid)
returns boolean
language sql
security definer set search_path = public
stable
as $$
  select public.is_staff_for_baby(target_baby_id) or public.is_parent_of_baby(target_baby_id);
$$;

-- =========================================================
-- RLS POLICIES
-- =========================================================

-- ---------- PROFILES ----------
create policy "profiles_select_own"
  on public.profiles for select
  using (id = auth.uid());

create policy "profiles_select_staff_all"
  on public.profiles for select
  using (public.current_role() in ('nurse', 'doctor'));

create policy "profiles_update_own"
  on public.profiles for update
  using (id = auth.uid());

-- ---------- BABIES ----------
create policy "babies_select_staff"
  on public.babies for select
  using (public.is_staff_for_baby(id));

create policy "babies_select_parent"
  on public.babies for select
  using (public.is_parent_of_baby(id));

create policy "babies_insert_staff"
  on public.babies for insert
  with check (public.current_role() in ('nurse', 'doctor'));

create policy "babies_update_staff"
  on public.babies for update
  using (public.is_staff_for_baby(id));

-- ---------- CARE TEAM ----------
create policy "care_team_select_own"
  on public.baby_care_team for select
  using (staff_id = auth.uid() or public.is_staff_for_baby(baby_id));

create policy "care_team_insert_staff"
  on public.baby_care_team for insert
  with check (public.current_role() in ('nurse', 'doctor'));

-- ---------- BABY PARENTS ----------
create policy "baby_parents_select_own"
  on public.baby_parents for select
  using (parent_id = auth.uid() or public.is_staff_for_baby(baby_id));

create policy "baby_parents_insert_staff"
  on public.baby_parents for insert
  with check (public.current_role() in ('nurse', 'doctor'));

-- ---------- VITALS ----------
create policy "vitals_select_access"
  on public.vitals for select
  using (public.has_baby_access(baby_id));

-- Devices do NOT insert through this policy. They call device_push_vitals() in
-- device_auth.sql, which is security definer and authenticates a per-device secret,
-- so the firmware only ever needs the public anon key. The staff policy below exists
-- for inserting test data by hand from the Supabase dashboard.
create policy "vitals_insert_staff"
  on public.vitals for insert
  with check (public.is_staff_for_baby(baby_id));

-- ---------- ALERTS ----------
create policy "alerts_select_access"
  on public.alerts for select
  using (public.has_baby_access(baby_id));

create policy "alerts_insert_staff"
  on public.alerts for insert
  with check (public.is_staff_for_baby(baby_id));

create policy "alerts_update_staff"
  on public.alerts for update
  using (public.is_staff_for_baby(baby_id));

-- ---------- CONSULTATIONS ----------
create policy "consultations_select_related"
  on public.consultations for select
  using (parent_id = auth.uid() or doctor_id = auth.uid());

create policy "consultations_insert_parent"
  on public.consultations for insert
  with check (parent_id = auth.uid() and public.is_parent_of_baby(baby_id));

create policy "consultations_update_related"
  on public.consultations for update
  using (parent_id = auth.uid() or doctor_id = auth.uid());

-- ---------- NOTIFICATION SETTINGS ----------
create policy "notif_settings_select_own"
  on public.notification_settings for select
  using (user_id = auth.uid());

create policy "notif_settings_upsert_own"
  on public.notification_settings for insert
  with check (user_id = auth.uid());

create policy "notif_settings_update_own"
  on public.notification_settings for update
  using (user_id = auth.uid());

-- =========================================================
-- REALTIME — enable live updates for vitals and alerts
-- =========================================================
alter publication supabase_realtime add table public.vitals;
alter publication supabase_realtime add table public.alerts;

-- =========================================================
-- SEED DATA (optional — one sample baby for testing)
-- Sign up first, then uncomment, paste your own user id, and run.
-- =========================================================
-- insert into public.babies (baby_code, display_name, gestational_age_weeks, birth_weight_grams, bed_number, room_number)
-- values ('baby_01', 'Baby of Karim', 34, 2100, 'B-3', 'NICU-1');


-- ==================== device_auth.sql ====================

-- =========================================================
-- NICU Monitoring — Device Auth & Ingest
-- =========================================================
-- Run this in Supabase Dashboard > SQL Editor *after* schema.sql.
--
-- What it does:
--   Lets the ESP32-WROOM-32 push vitals straight into Supabase without
--   carrying a service_role key. The device holds only the public anon
--   key plus its own secret. If that secret leaks, the attacker can
--   insert fake vitals for that one baby and nothing else — no read
--   access to any patient data.
-- =========================================================

-- ---------------------------------------------------------
-- 0. Extension — needed to hash device secrets (bcrypt)
-- ---------------------------------------------------------
-- Supabase normally ships pgcrypto in the "extensions" schema already.
-- Creating the schema first keeps this working on a plain Postgres too.
create schema if not exists extensions;
create extension if not exists pgcrypto with schema extensions;

-- ---------------------------------------------------------
-- 1. DEVICES — one row per physical device, with its secret
-- ---------------------------------------------------------
create table if not exists public.devices (
  id            uuid primary key default uuid_generate_v4(),
  baby_code     text not null references public.babies(baby_code) on delete cascade,
  label         text,                     -- e.g. "ESP32 foot unit #1"
  secret_hash   text not null,            -- bcrypt hash, never plaintext
  is_active     boolean not null default true,
  last_seen_at  timestamptz,              -- bumped on every successful push
  created_at    timestamptz not null default now()
);

create index if not exists idx_devices_baby_code on public.devices (baby_code);

-- RLS on, with no policies — deliberately.
-- That means anon and authenticated clients cannot read or write this
-- table at all; only the security definer function below can touch it.
alter table public.devices enable row level security;

-- =========================================================
-- 2. device_push_vitals() — the device's only endpoint
-- =========================================================
-- One call does everything: verify the secret, resolve baby_code to a
-- uuid, insert the vitals row, raise an alert if asked, stamp last_seen_at.
--
-- Keeping "extensions" on the search_path matters — without it crypt()
-- will not resolve and every call fails.
-- =========================================================
create or replace function public.device_push_vitals(
  p_baby_code      text,
  p_device_secret  text,
  p_hr             int,
  p_spo2           int,
  p_temp           numeric,
  p_motion         numeric,
  p_sensor_ok      boolean,
  p_is_abnormal    boolean default false,
  p_alert_reason   text    default null,
  p_alert_severity text    default null
)
returns jsonb
language plpgsql
security definer
set search_path = public, extensions
as $$
declare
  v_device   public.devices%rowtype;
  v_baby_id  uuid;
  v_alert_id uuid;
  v_severity text;
begin
  -- ---- Find the device and verify its secret in one query ----
  -- "no such device" and "wrong secret" return the same error on purpose,
  -- so an outsider cannot probe which baby_codes are registered.
  select * into v_device
  from public.devices
  where baby_code = p_baby_code
    and is_active
    and secret_hash = crypt(p_device_secret, secret_hash)
  limit 1;

  if not found then
    raise exception 'device authentication failed' using errcode = '28000';
  end if;

  -- ---- baby_code (text) -> babies.id (uuid) ----
  -- This mapping is what the old gateway had no way to do; it pushed the
  -- text code straight into a uuid column, which could never have worked.
  select id into v_baby_id
  from public.babies
  where baby_code = p_baby_code and is_active;

  if v_baby_id is null then
    raise exception 'no active baby for code %', p_baby_code using errcode = '23503';
  end if;

  -- ---- Insert the vitals row ----
  -- Firmware sends -1 for an invalid reading (and DS18B20 reports -127 when
  -- it fails). Storing those as NULL lets the UI's `?? '--'` fallback do its
  -- job instead of rendering nonsense like "-1 bpm".
  insert into public.vitals (
    baby_id, heart_rate, spo2, temperature, motion, sensor_ok, is_abnormal
  )
  values (
    v_baby_id,
    nullif(p_hr, -1),
    nullif(p_spo2, -1),
    case when p_temp <= -100 then null else p_temp end,
    p_motion,
    coalesce(p_sensor_ok, false),
    coalesce(p_is_abnormal, false)
  );

  -- ---- Raise an alert, if the firmware asked for one ----
  if p_alert_reason is not null and length(trim(p_alert_reason)) > 0 then
    v_severity := case
                    when p_alert_severity in ('warning', 'critical') then p_alert_severity
                    else 'critical'
                  end;

    -- Dedupe: skip if an active alert with this same reason was raised in
    -- the last 5 minutes. Without this a sustained desaturation would stack
    -- up a new alert every few seconds.
    insert into public.alerts (baby_id, reason, severity)
    select v_baby_id, p_alert_reason, v_severity
    where not exists (
      select 1 from public.alerts
      where baby_id = v_baby_id
        and status = 'active'
        and reason = p_alert_reason
        and created_at > now() - interval '5 minutes'
    )
    returning id into v_alert_id;
  end if;

  -- ---- Heartbeat ----
  update public.devices set last_seen_at = now() where id = v_device.id;

  return jsonb_build_object(
    'ok', true,
    'baby_id', v_baby_id,
    'alert_created', v_alert_id is not null
  );
end;
$$;

-- ---------------------------------------------------------
-- 3. Permissions
-- ---------------------------------------------------------
-- The anon key is enough to *call* this function, but without the device
-- secret it does nothing. bcrypt is slow by design, which is the natural
-- brake on brute-forcing the secret.
revoke all on function public.device_push_vitals(
  text, text, int, int, numeric, numeric, boolean, boolean, text, text
) from public;

grant execute on function public.device_push_vitals(
  text, text, int, int, numeric, numeric, boolean, boolean, text, text
) to anon, authenticated;

-- =========================================================
-- 4. Registering a device (run this yourself in the SQL Editor)
-- =========================================================
-- There is deliberately no callable function for this — otherwise anyone
-- holding the anon key could enrol their own device.
--
-- The baby must already exist in babies with this baby_code. Then:
--
--   insert into public.devices (baby_code, label, secret_hash)
--   values (
--     'baby_01',
--     'ESP32 foot unit #1',
--     crypt('choose-a-long-random-secret', gen_salt('bf'))
--   );
--
-- Whatever plaintext secret you use here goes into DEVICE_SECRET in the
-- firmware. Only the hash is stored, so the plaintext exists nowhere else —
-- if you lose it, set a new one rather than trying to recover it.
--
-- To rotate a secret:
--   update public.devices
--      set secret_hash = crypt('new-secret', gen_salt('bf'))
--    where baby_code = 'baby_01';
--
-- To disable a device:
--   update public.devices set is_active = false where baby_code = 'baby_01';
-- =========================================================


-- ==================== web_app_fixes.sql ====================

-- =========================================================
-- NICU Monitoring — Web App Fixes
-- =========================================================
-- Run this in Supabase Dashboard > SQL Editor *after* schema.sql.
-- Order: schema.sql -> device_auth.sql -> web_app_fixes.sql
--
-- Safe to re-run: everything here is create-or-replace.
--
-- Fixes two things the web app could not do with table access alone:
--   1. A parent could never see any doctor on the consulting page.
--   2. The dashboard issued one query per baby to find latest vitals.
-- =========================================================


-- =========================================================
-- 1. list_doctors() — the bookable doctor directory
-- =========================================================
-- The consulting page used to run:
--     select * from profiles where role = 'doctor'
--
-- RLS on profiles only allows a user to read their own row, or — for
-- nurses and doctors — every row. A parent matched neither, so the
-- doctor list was silently empty for exactly the role that needs it.
--
-- The obvious fix is a policy like `using (role = 'doctor')`, but RLS is
-- row-level: that would hand every parent the doctors' phone numbers too.
-- A security definer function lets us return only the columns a booking
-- screen actually needs.
create or replace function public.list_doctors()
returns table (
  id         uuid,
  full_name  text,
  specialty  text,
  avatar_url text
)
language sql
security definer
set search_path = public
stable
as $$
  select p.id, p.full_name, p.specialty, p.avatar_url
  from public.profiles p
  where p.role = 'doctor'
  order by p.full_name;
$$;

-- Signed-in users only. Not anon: the directory is not public.
revoke all on function public.list_doctors() from public;
grant execute on function public.list_doctors() to authenticated;


-- =========================================================
-- 2. latest_vitals_for_babies() — one query instead of N
-- =========================================================
-- The dashboard looped over its babies and ran a separate
-- `select ... limit 1` for each one. DISTINCT ON does the whole thing in
-- a single round trip, and rides the existing
-- idx_vitals_baby_time (baby_id, recorded_at desc) index.
--
-- SECURITY NOTE: security definer bypasses RLS, so access is re-checked
-- explicitly with has_baby_access(). Without that line this function
-- would leak every baby's vitals to any signed-in user. auth.uid() still
-- resolves correctly inside a definer function — it reads the request's
-- JWT claims, not the executing role.
create or replace function public.latest_vitals_for_babies(p_baby_ids uuid[])
returns setof public.vitals
language sql
security definer
set search_path = public
stable
as $$
  select distinct on (v.baby_id) v.*
  from public.vitals v
  where v.baby_id = any(p_baby_ids)
    and public.has_baby_access(v.baby_id)
  order by v.baby_id, v.recorded_at desc;
$$;

revoke all on function public.latest_vitals_for_babies(uuid[]) from public;
grant execute on function public.latest_vitals_for_babies(uuid[]) to authenticated;


-- =========================================================
-- 3. Realtime — confirm alerts is published
-- =========================================================
-- schema.sql already adds vitals and alerts to the supabase_realtime
-- publication, so the Alerts page subscription works with no change here.
-- These statements are only a safety net for a database where schema.sql
-- was run before those lines existed. `add table` errors if the table is
-- already a member, so each one is wrapped.
do $$
begin
  alter publication supabase_realtime add table public.vitals;
exception
  when duplicate_object then null;
end;
$$;

do $$
begin
  alter publication supabase_realtime add table public.alerts;
exception
  when duplicate_object then null;
end;
$$;


-- =========================================================
-- Verification
-- =========================================================
-- As a signed-in parent (from the app, not the SQL editor — the SQL
-- editor runs as postgres and bypasses RLS, so it cannot prove this):
--
--   select * from public.list_doctors();
--     -> should list doctors, with no phone column at all
--
--   select * from public.latest_vitals_for_babies(
--     array['<your-baby-uuid>']::uuid[]
--   );
--     -> should return exactly one row per baby you have access to,
--        and nothing for a baby you do not
-- =========================================================

