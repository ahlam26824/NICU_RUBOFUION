-- =========================================================
-- DIAGNOSE_AND_FIX.sql — "no data from the ESP32" checklist
-- =========================================================
-- Run this whole file in the Supabase SQL Editor. It is read-only apart
-- from PART 4 (linkage repair) and PART 5 (a fake reading you ask for
-- explicitly), and it is safe to re-run as often as you like.
--
-- Read the NOTICE output at the bottom of the results pane, not the
-- table grid — every check prints a line there.
--
-- What this can and cannot tell you:
--   It proves whether the DATABASE half works. If every check passes and
--   your dashboard is still empty, the device never reached Supabase, and
--   the answer is on the serial monitor — not in here. PART 6 says how to
--   tell those two apart for certain.
-- =========================================================

-- ---------------------------------------------------------
-- PART 1 — Is the schema actually installed?
-- ---------------------------------------------------------
do $$
declare
  v_missing text := '';
  v_t       text;
begin
  raise notice '';
  raise notice '=== PART 1: schema ===';

  foreach v_t in array array['profiles','babies','baby_care_team','baby_parents',
                             'vitals','alerts','devices','consultations',
                             'notification_settings']
  loop
    if not exists (select 1 from information_schema.tables
                   where table_schema = 'public' and table_name = v_t) then
      v_missing := v_missing || v_t || ' ';
    end if;
  end loop;

  if v_missing = '' then
    raise notice 'OK    all 9 tables present';
  else
    raise notice 'FAIL  missing tables: %', v_missing;
    raise notice '      -> run RUN_ALL.sql first. Nothing else here will work.';
  end if;

  -- The three functions the device and the app call.
  if exists (select 1 from pg_proc p join pg_namespace n on n.oid = p.pronamespace
             where n.nspname = 'public' and p.proname = 'device_push_vitals') then
    raise notice 'OK    device_push_vitals() exists  (the device''s only endpoint)';
  else
    raise notice 'FAIL  device_push_vitals() MISSING -> every POST returns 404';
  end if;

  if exists (select 1 from pg_proc p join pg_namespace n on n.oid = p.pronamespace
             where n.nspname = 'public' and p.proname = 'latest_vitals_for_babies') then
    raise notice 'OK    latest_vitals_for_babies() exists  (dashboard needs it)';
  else
    raise notice 'FAIL  latest_vitals_for_babies() MISSING -> dashboard shows no vitals';
  end if;

  -- pgcrypto: without it crypt() fails and EVERY device POST 500s.
  if exists (select 1 from pg_extension where extname = 'pgcrypto') then
    raise notice 'OK    pgcrypto installed  (needed to check the device secret)';
  else
    raise notice 'FAIL  pgcrypto MISSING -> every device POST fails on crypt()';
  end if;
end $$;

-- ---------------------------------------------------------
-- PART 2 — Realtime. Without this the page needs a manual refresh.
-- ---------------------------------------------------------
do $$
begin
  raise notice '';
  raise notice '=== PART 2: realtime ===';

  if exists (select 1 from pg_publication_tables
             where pubname = 'supabase_realtime'
               and schemaname = 'public' and tablename = 'vitals') then
    raise notice 'OK    vitals is published -> live updates work';
  else
    raise notice 'WARN  vitals NOT published -> rows land in the table but the';
    raise notice '      page will not move until you reload it. Fix:';
    raise notice '      alter publication supabase_realtime add table public.vitals;';
  end if;

  if exists (select 1 from pg_publication_tables
             where pubname = 'supabase_realtime'
               and schemaname = 'public' and tablename = 'alerts') then
    raise notice 'OK    alerts is published';
  else
    raise notice 'WARN  alerts NOT published -> new alerts need a reload';
  end if;
end $$;

-- ---------------------------------------------------------
-- PART 3 — Babies, devices, and whether anything ever arrived
-- ---------------------------------------------------------
do $$
declare
  v_babies  int;
  v_devices int;
  v_vitals  int;
  v_recent  int;
  v_last    timestamptz;
  v_seen    timestamptz;
  v_code    text;
begin
  raise notice '';
  raise notice '=== PART 3: data ===';

  select count(*) into v_babies  from public.babies;
  select count(*) into v_devices from public.devices;
  select count(*) into v_vitals  from public.vitals;

  raise notice 'babies=%   devices=%   vitals rows=%', v_babies, v_devices, v_vitals;

  if v_babies = 0 then
    raise notice 'FAIL  no babies at all -> run seed_demo_accounts.sql';
    return;
  end if;

  -- A device whose baby_code has no matching baby can never post: the
  -- function authenticates it, then fails to resolve the code to a uuid.
  for v_code in
    select d.baby_code from public.devices d
    where not exists (select 1 from public.babies b
                      where b.baby_code = d.baby_code and b.is_active)
  loop
    raise notice 'FAIL  device baby_code "%" has no ACTIVE baby -> POST gets 23503', v_code;
  end loop;

  -- A baby with no device means nothing is ever going to push for it.
  for v_code in
    select b.baby_code from public.babies b
    where not exists (select 1 from public.devices d
                      where d.baby_code = b.baby_code and d.is_active)
  loop
    raise notice 'WARN  baby "%" has no active device registered', v_code;
  end loop;

  select max(recorded_at) into v_last from public.vitals;
  select max(last_seen_at) into v_seen from public.devices;

  if v_vitals = 0 then
    raise notice 'FAIL  the vitals table is EMPTY - no device has EVER posted.';
    raise notice '      The database is not your problem. See PART 5 and PART 6.';
  else
    select count(*) into v_recent from public.vitals
    where recorded_at > now() - interval '2 minutes';

    raise notice 'last reading: %  (% ago)', v_last, age(now(), v_last);
    raise notice 'last device contact: %', coalesce(v_seen::text, 'never');

    if v_recent > 0 then
      raise notice 'OK    % reading(s) in the last 2 min -> device is LIVE right now', v_recent;
    else
      raise notice 'WARN  nothing in the last 2 min -> dashboard will show grey';
      raise notice '      "No signal". Readings exist, so the device worked once';
      raise notice '      and has since stopped or lost WiFi.';
    end if;
  end if;
end $$;

-- ---------------------------------------------------------
-- PART 4 — Linkage repair (the usual reason a dashboard is empty)
-- ---------------------------------------------------------
-- RLS returns no babies to a user who is on nobody's care team. A brand
-- new signup is therefore correct-but-empty. This links EVERY existing
-- account to EVERY active baby, which is what you want for a prototype
-- and is not what you want in a real ward.
do $$
declare
  v_staff  int := 0;
  v_parent int := 0;
  v_orphan int;
begin
  raise notice '';
  raise notice '=== PART 4: linking accounts to babies ===';

  -- An auth user with no profile row loads as profile=null and most pages
  -- break. That happens to anyone who signed up before RUN_ALL.sql created
  -- the trigger.
  select count(*) into v_orphan
  from auth.users u
  where not exists (select 1 from public.profiles p where p.id = u.id);

  if v_orphan > 0 then
    raise notice 'WARN  % auth user(s) have no profile row -> run fix_existing_users.sql', v_orphan;
  end if;

  insert into public.baby_care_team (baby_id, staff_id)
  select b.id, p.id
  from public.babies b
  cross join public.profiles p
  where b.is_active and p.role in ('nurse','doctor')
  on conflict (baby_id, staff_id) do nothing;
  get diagnostics v_staff = row_count;

  insert into public.baby_parents (baby_id, parent_id, relation)
  select b.id, p.id, 'parent'
  from public.babies b
  cross join public.profiles p
  where b.is_active and p.role = 'parent'
  on conflict (baby_id, parent_id) do nothing;
  get diagnostics v_parent = row_count;

  raise notice 'linked % staff and % parent assignment(s)', v_staff, v_parent;

  if v_staff = 0 and v_parent = 0 then
    raise notice 'OK    everyone was already linked - this was not the problem';
  else
    raise notice 'OK    fixed. Reload the dashboard.';
  end if;
end $$;

-- who can now see what
select
  p.role,
  coalesce(u.email, '(no auth user)') as email,
  p.full_name,
  (select count(*) from public.baby_care_team t where t.staff_id  = p.id) as as_staff,
  (select count(*) from public.baby_parents  x where x.parent_id = p.id) as as_parent
from public.profiles p
left join auth.users u on u.id = p.id
order by p.role, u.email;

-- ---------------------------------------------------------
-- PART 5 — Prove the pipeline with NO hardware
-- ---------------------------------------------------------
-- This is the important one. It calls the exact function the firmware
-- calls, with the demo secret, so it tests auth + insert + alert + the
-- realtime push in one go.
--
-- Leave the dashboard open on a second screen while you run it. A row
-- should appear within a second.
--
-- UNCOMMENT the block below to fire it. Change 'baby_01' and the secret
-- if you registered your own device.

-- do $$
-- declare
--   v_result jsonb;
-- begin
--   v_result := public.device_push_vitals(
--     p_baby_code     => 'baby_01',
--     p_device_secret => 'demo-device-secret-change-me',
--     p_hr            => 148,
--     p_spo2          => 96,
--     p_temp          => 36.9,
--     p_motion        => 0.15,
--     p_sensor_ok     => true,
--     p_is_abnormal   => false
--   );
--   raise notice '';
--   raise notice '=== PART 5: fake device POST ===';
--   raise notice 'result: %', v_result;
--   raise notice 'If you see ok=true, the ENTIRE database path works and the';
--   raise notice 'problem is on the device. Go to PART 6.';
-- exception
--   when sqlstate '28000' then
--     raise notice 'FAILED auth - the secret does not match the devices row.';
--     raise notice 'This is the SAME 403 your ESP32 is getting. Fix: PART 7.';
--   when others then
--     raise notice 'FAILED: % (%)', sqlerrm, sqlstate;
-- end $$;

-- ---------------------------------------------------------
-- PART 6 — Reading the result
-- ---------------------------------------------------------
--  PART 5 ok=true, dashboard updates  -> database fine, device never
--                                        arrived. Check serial monitor.
--  PART 5 ok=true, dashboard EMPTY    -> a read/RLS problem. Re-run
--                                        PART 4 and log in again.
--  PART 5 says FAILED auth            -> baby code or secret is wrong.
--                                        PART 7 resets it.
--
-- On the device side, at 115200 baud, the three things worth knowing:
--   "POST ok"              working
--   "POST failed (401/403)" baby code / secret mismatch -> PART 7
--   blank serial monitor    USB CDC On Boot is Disabled. The board is
--                           almost certainly fine - see WIRING.md sec 8
--
-- If the setup page reopens on every boot, the config has never produced
-- one accepted POST, so the firmware treats it as wrong. That is nearly
-- always a mistyped Supabase URL, anon key, or device secret.

-- ---------------------------------------------------------
-- PART 7 — Reset the device secret
-- ---------------------------------------------------------
-- Only the bcrypt hash is stored, so a lost secret is replaced, never
-- recovered. Uncomment, pick your own long random string, and type the
-- SAME string into the device's setup page.

-- update public.devices
--    set secret_hash = extensions.crypt('demo-device-secret-change-me',
--                                       extensions.gen_salt('bf')),
--        is_active   = true
--  where baby_code = 'baby_01';

-- Registering a device for a baby that has none:
-- insert into public.devices (baby_code, label, secret_hash)
-- values ('baby_01', 'ESP32 foot unit #1',
--         extensions.crypt('choose-a-long-random-secret',
--                          extensions.gen_salt('bf')));

-- ---------------------------------------------------------
-- PART 8 — Clearing test data
-- ---------------------------------------------------------
-- The fake readings from PART 5 are real rows and will sit in the history
-- and skew the dashboard. Remove them once the real device is posting:

-- delete from public.vitals where recorded_at < now() - interval '1 hour';
-- delete from public.alerts where status = 'active';
