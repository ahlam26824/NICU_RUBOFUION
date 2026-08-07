-- =====================================================================
--  Fixed demo logins for the prototype (nurse / doctor / parent)
-- =====================================================================
--  Run this AFTER RUN_ALL.sql.
--
--      nurse@nicu.com     12345678     role = nurse
--      doctor@nicu.com    12345678     role = doctor
--      parent@nicu.com    12345678     role = parent
--
--  Safe to re-run. Every run forces those three passwords back to
--  12345678 and re-confirms the emails, so the credentials cannot drift
--  or expire - that is what makes them "always work".
--
--  It also creates one demo baby and links all three accounts to it.
--  That part is not optional in practice: row-level security hides every
--  baby a user is not linked to, so without the links these accounts
--  sign in successfully and then see empty pages.
--
--  PROTOTYPE ONLY. Three well-known accounts sharing an 8-digit password
--  must not go anywhere near real patient data.
-- =====================================================================

create extension if not exists pgcrypto with schema extensions;


-- ---------------------------------------------------------------------
-- Helper - create or repair one confirmed account
-- ---------------------------------------------------------------------
-- Normally a user is created by the signup API, which hashes the
-- password and writes auth.users, auth.identities and (via the
-- on_auth_user_created trigger) public.profiles. There is no way to call
-- that API from SQL, so this does the same three writes by hand.
--
-- Deliberately NOT security definer: it only ever runs as the SQL editor
-- role, which already has the rights it needs. It is also dropped at the
-- bottom of this file.
create or replace function public.seed_demo_user(
  p_email     text,
  p_password  text,
  p_full_name text,
  p_role      text,
  p_specialty text default null
)
returns uuid
language plpgsql
set search_path = public, extensions
as $fn$
declare
  v_id   uuid;
  v_meta jsonb;
begin
  select id into v_id from auth.users where email = p_email;

  if v_id is null then
    v_id := gen_random_uuid();

    -- The four empty strings at the end are load-bearing. Several GoTrue
    -- builds read those columns into Go strings and fail on NULL with
    -- "converting NULL to string is unsupported", which reaches the
    -- browser as a 500 on an otherwise valid sign-in.
    insert into auth.users (
      instance_id, id, aud, role, email, encrypted_password,
      email_confirmed_at, raw_app_meta_data, raw_user_meta_data,
      created_at, updated_at,
      confirmation_token, recovery_token, email_change, email_change_token_new
    ) values (
      '00000000-0000-0000-0000-000000000000', v_id,
      'authenticated', 'authenticated', p_email,
      crypt(p_password, gen_salt('bf')),
      now(),
      '{"provider":"email","providers":["email"]}'::jsonb,
      jsonb_build_object('full_name', p_full_name, 'role', p_role),
      now(), now(),
      '', '', '', ''
    );
  else
    -- Already present: reset the password to the known one and clear the
    -- unconfirmed state, so re-running always restores the login.
    update auth.users
       set encrypted_password = crypt(p_password, gen_salt('bf')),
           email_confirmed_at = coalesce(email_confirmed_at, now()),
           raw_user_meta_data = jsonb_build_object('full_name', p_full_name,
                                                   'role', p_role),
           updated_at         = now()
     where id = v_id;
  end if;

  -- An email identity has to exist alongside the user. Without it
  -- password sign-in can be rejected even though auth.users looks right.
  if not exists (
    select 1 from auth.identities where user_id = v_id and provider = 'email'
  ) then
    v_meta := jsonb_build_object(
      'sub',            v_id::text,
      'email',          p_email,
      'email_verified', true,
      'phone_verified', false
    );

    -- provider_id was added to auth.identities in a later GoTrue release.
    -- Branching on the column keeps this working on either shape instead
    -- of failing on a project that has not been upgraded.
    if exists (
      select 1 from information_schema.columns
       where table_schema = 'auth'
         and table_name   = 'identities'
         and column_name  = 'provider_id'
    ) then
      execute 'insert into auth.identities
                 (user_id, provider_id, provider, identity_data,
                  last_sign_in_at, created_at, updated_at)
               values ($1, $2, ''email'', $3, now(), now(), now())'
        using v_id, v_id::text, v_meta;
    else
      execute 'insert into auth.identities
                 (id, user_id, provider, identity_data,
                  last_sign_in_at, created_at, updated_at)
               values ($1, $2, ''email'', $3, now(), now(), now())'
        using v_id::text, v_id, v_meta;
    end if;
  end if;

  -- On a fresh insert the on_auth_user_created trigger has already
  -- written this row. The upsert is what makes a re-run able to repair a
  -- name, role or specialty that came through wrong.
  insert into public.profiles (id, full_name, role, specialty)
  values (v_id, p_full_name, p_role, p_specialty)
  on conflict (id) do update
    set full_name = excluded.full_name,
        role      = excluded.role,
        specialty = excluded.specialty;

  -- Settings page reads this row and shows nothing without it.
  insert into public.notification_settings (user_id)
  values (v_id)
  on conflict (user_id) do nothing;

  return v_id;
end;
$fn$;

-- ---------------------------------------------------------------------
-- Create the three accounts, the demo baby, and the links between them
-- ---------------------------------------------------------------------
do $seed$
declare
  v_nurse  uuid;
  v_doctor uuid;
  v_parent uuid;
  v_baby   uuid;
begin
  v_nurse  := public.seed_demo_user('nurse@nicu.com',  '12345678',
                                    'Demo Nurse',  'nurse');
  v_doctor := public.seed_demo_user('doctor@nicu.com', '12345678',
                                    'Demo Doctor', 'doctor', 'Neonatologist');
  v_parent := public.seed_demo_user('parent@nicu.com', '12345678',
                                    'Demo Parent', 'parent');

  -- baby_code has to match the one entered in the firmware setup portal.
  insert into public.babies (baby_code, display_name, gestational_age_weeks,
                             birth_weight_grams, bed_number, room_number)
  values ('baby_01', 'Demo Baby', 32, 1650, 'B-01', 'NICU-1')
  on conflict (baby_code) do nothing;

  select id into v_baby from public.babies where baby_code = 'baby_01';

  -- Staff see a baby only through baby_care_team, a parent only through
  -- baby_parents. Skip these and every page returns zero rows.
  insert into public.baby_care_team (baby_id, staff_id)
  values (v_baby, v_nurse), (v_baby, v_doctor)
  on conflict (baby_id, staff_id) do nothing;

  insert into public.baby_parents (baby_id, parent_id, relation)
  values (v_baby, v_parent, 'mother')
  on conflict (baby_id, parent_id) do nothing;

  -- Somewhere for the ESP32 to post. Only the bcrypt hash is stored, so
  -- keep the plaintext below - it is what goes into the setup portal.
  insert into public.devices (baby_code, label, secret_hash)
  select 'baby_01', 'ESP32 foot unit #1',
         extensions.crypt('demo-device-secret-change-me',
                          extensions.gen_salt('bf'))
   where not exists (
     select 1 from public.devices where baby_code = 'baby_01'
   );
end;
$seed$;


-- ---------------------------------------------------------------------
-- Drop the helper
-- ---------------------------------------------------------------------
-- PostgREST publishes every function in the public schema as an RPC
-- endpoint, and this one creates pre-confirmed users at any role. Leaving
-- it in place would let anyone holding the anon key mint themselves a
-- doctor account, so it does not outlive the script that needs it.
drop function public.seed_demo_user(text, text, text, text, text);


-- ---------------------------------------------------------------------
-- Verify
-- ---------------------------------------------------------------------
-- All three rows should read true, true, true.
select u.email,
       p.role,
       p.full_name,
       (u.email_confirmed_at is not null)               as confirmed,
       exists (select 1 from auth.identities i
                where i.user_id = u.id
                  and i.provider = 'email')            as has_identity,
       (bct.id is not null or bp.id is not null)        as linked_to_baby
  from auth.users u
  left join public.profiles p         on p.id         = u.id
  left join public.baby_care_team bct on bct.staff_id = u.id
  left join public.baby_parents  bp   on bp.parent_id = u.id
 where u.email in ('nurse@nicu.com', 'doctor@nicu.com', 'parent@nicu.com')
 order by p.role;
