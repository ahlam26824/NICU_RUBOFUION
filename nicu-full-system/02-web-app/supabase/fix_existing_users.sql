-- =====================================================================
--  Repair accounts created BEFORE RUN_ALL.sql was applied
-- =====================================================================
--  Run this only AFTER RUN_ALL.sql has completed successfully.
--
--  Why this is needed:
--    RUN_ALL.sql installs an on_auth_user_created trigger that inserts a
--    public.profiles row whenever someone signs up. A user who signed up
--    before that trigger existed has an auth.users row and no profile, so
--    the app loads with profile = null and most pages break.
--
--    Triggers do not fire retroactively, so the gap has to be backfilled
--    by hand. This does that, and also clears the unconfirmed-email state
--    that blocks sign-in.
--
--  Safe to re-run. Both statements skip rows that are already correct.
-- =====================================================================


-- ---------------------------------------------------------------------
-- 1. Mark existing signups as confirmed
-- ---------------------------------------------------------------------
-- Fixes "Email not confirmed" for accounts already created. Turning off
-- "Confirm email" in the dashboard only affects NEW signups - it does not
-- retroactively confirm anyone, which is why this is still needed.
update auth.users
   set email_confirmed_at = coalesce(email_confirmed_at, now())
 where email_confirmed_at is null;


-- ---------------------------------------------------------------------
-- 2. Backfill the missing profile rows
-- ---------------------------------------------------------------------
-- Reads the same user metadata the trigger would have read, with the same
-- fallbacks, so a backfilled profile is indistinguishable from a normal one.
--
-- Note the role fallback: 'parent' is the least-privileged of the three, so
-- a user whose signup metadata is missing lands with the narrowest access
-- rather than accidentally becoming staff. Fix any wrong roles in step 3.
insert into public.profiles (id, full_name, role)
select u.id,
       coalesce(u.raw_user_meta_data->>'full_name', 'Unnamed'),
       coalesce(u.raw_user_meta_data->>'role', 'parent')
  from auth.users u
  left join public.profiles p on p.id = u.id
 where p.id is null
   and coalesce(u.raw_user_meta_data->>'role', 'parent') in ('nurse','doctor','parent');


-- ---------------------------------------------------------------------
-- 3. Check what you ended up with
-- ---------------------------------------------------------------------
select u.email,
       p.full_name,
       p.role,
       (u.email_confirmed_at is not null) as confirmed,
       (p.id is not null)                 as has_profile
  from auth.users u
  left join public.profiles p on p.id = u.id
 order by u.created_at;

-- Every row should read confirmed = true and has_profile = true.
--
-- To correct a role that came through wrong:
--   update public.profiles set role = 'nurse'
--    where id = (select id from auth.users where email = 'you@example.com');
