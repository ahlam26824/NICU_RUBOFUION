# NICU Care — Web App (React + Supabase)

Role-aware NICU monitoring web app. **Nurse**, **Doctor** and **Parent** each get their own
dashboard view, enforced by row level security rather than by the UI.

## 1. Supabase setup

1. Create a new project at [supabase.com](https://supabase.com)
2. Once it is ready, open the **SQL Editor**
3. Run these files in order, pasting the full contents of each:

   | Order | File | What it creates |
   |---|---|---|
   | 1 | `supabase/RUN_ALL.sql` | Everything: tables, RLS policies, triggers, realtime publication, `device_push_vitals()` for the firmware, and the `list_doctors()` / `latest_vitals_for_babies()` RPCs the app calls |
   | 2 | `supabase/seed_demo_accounts.sql` | Three fixed demo logins, a demo baby, and a device row |

   Both are idempotent, so re-running either is safe.

   `supabase/fix_existing_users.sql` is a third, optional file. It only matters if you signed
   up in the app *before* running `RUN_ALL.sql` — the profile-creating trigger did not exist
   yet, so that account has no profile row and loads with `profile = null`. It backfills the
   gap. Not needed if you're using the demo logins.

4. From **Project Settings > API**, copy the `Project URL` and the `anon public` key

## 2. Local setup

```bash
npm install
cp .env.example .env
```

Put your Supabase URL and anon key in `.env`:

```
VITE_SUPABASE_URL=https://xxxxx.supabase.co
VITE_SUPABASE_ANON_KEY=eyJhbGciOi...
```

```bash
npm run dev
```

Only ever put the **anon** key here. Anything in `.env` ends up in the built bundle and is
readable by every visitor.

## 3. Creating the first user

Open the app and **Sign Up**, choosing Nurse, Doctor or Parent as the role. A matching row in
`profiles` is created automatically by the `on_auth_user_created` trigger.

For a parent to see any doctor on `/consulting`, at least one user must have signed up with
the Doctor role.

## 4. Adding and assigning a baby (manual for now)

From Supabase Dashboard > Table Editor:

1. Add a row to `babies` — `baby_code` must match `BABY_CODE` in the firmware
2. Add a row to `baby_care_team` with a nurse/doctor user id + the baby id
3. Add a row to `baby_parents` with a parent user id + the baby id

Until step 2 or 3 exists for your account, the dashboard is empty by design — RLS returns
no babies to a user who is on nobody's care team.

## 5. How device data arrives

The ESP32-C6 posts directly to Supabase. It calls the `device_push_vitals()` RPC defined
in `supabase/RUN_ALL.sql`, authenticating with a per-device secret stored as a bcrypt hash.

That means the firmware carries only the public **anon** key plus its own secret. There is no
middle backend, and the `service_role` key is not used anywhere in this project — a stolen
device can insert fake vitals for its one baby and can read nothing.

See `01-device-firmware/esp32_wroom_standalone/` and the root `README.md` for wiring and flashing.

## Pages

| Route | Who | What it shows |
|---|---|---|
| `/login` | Everyone | Sign in / sign up, with role selection |
| `/dashboard` | Everyone | Overall health ring + baby list, filtered by role |
| `/baby/:id` | Anyone with access to that baby | Live vitals for one baby (realtime) |
| `/alerts` | Everyone | Active / acknowledged / resolved alert history (realtime) |
| `/consulting` | Mainly parents | Doctor directory, video or clinic booking |
| `/settings` | Everyone | Profile edit, notification toggles |

## How a baby's status is decided

`src/lib/vitalStatus.js` classifies each baby three ways, and every screen uses it:

| Status | Meaning | Shown as |
|---|---|---|
| `stable` | Recent reading, within thresholds | Green dot |
| `abnormal` | Recent reading, `is_abnormal` set by the firmware | Pulsing red dot |
| `unmonitored` | No reading ever, none in the last 2 minutes, or `sensor_ok = false` | Grey dot + "No signal" |

The distinction matters: an unmonitored baby is **excluded from the health score** rather than
counted as healthy. A ward whose devices are all offline reads `—`, not `100%`. Likewise a baby
with no SpO2 reading shows a grey `--` ring, not a red `0%` one that would look like a
desaturation. Missing data must never be displayed as good news.

Staleness is re-evaluated on a 30 second timer, so a device that goes quiet turns grey on its
own without anyone touching the page.

## Design system

- Background: soft mint `#EAF0DA`
- Cards: white, `rounded-3xl`, soft shadow
- Accent: lime green `#D7F24E` for buttons and active pills
- Status colours: `good #6FAE5C`, `warn #F0B94D`, `alert #E8604C`, `sage-300 #CBD8AC`

All of it is centralized in `tailwind.config.js` — change the theme there, not in components.

## Not built yet

- [ ] "Add Baby" form — currently done from the Supabase dashboard
- [ ] Push notifications for doctors and nurses (Firebase Cloud Messaging)
- [ ] Vitals history line chart on the BabyDetail page
- [ ] Per-baby thresholds based on gestational age — the firmware uses static ones
