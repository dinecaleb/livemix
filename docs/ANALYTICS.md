# Analytics, stability reports and milestones

What DLIVE sends, why, and what it never sends. Every event below exists to answer a question
in the last column; **an event that answers no question here is not added**, and a new one
is added to this file in the same commit as the code that sends it.

- Code: `app/native/Telemetry.{h,cpp}` (the one service), `app/native/UsageIds.h` (the fixed
  words for instruments), `MixController::onUsage` (the mix's events, no JUCE), wired in
  `app/Main.cpp`. Tests: `app/Tests/TelemetryTests.cpp`, and the last test in `AppTests.cpp`.
- Database: the Supabase project **DineAudio** (`sagthwycbpdcwdwzpiia`, ca-central-1).
  `supabase/migrations/20260929120000_dlive_analytics.sql` holds one `events` table and the
  `analytics.*` views that answer the questions. `20260929130000_dlive_analytics_ingest.sql`
  adds `public.ingest_events(rows)`, which is the only way in for the anon key. Both are
  applied.

## The rules

1. **Never on the audio thread, never in its way.** `track()` appends a row under a short lock.
   The files and the network belong to a background worker thread that sleeps between batches.
   Device figures (dropouts, CPU, sample rate) are read from counters once a second on the
   message thread. Nothing new runs in `process()`, `processBlock()` or the CoreAudio callback.
2. **Fails silently.** No URL or key configured, no network, a 5xx, sharing switched off: to
   the rest of DLIVE all of these look the same, which is that nothing happens. Unsent rows wait
   in `telemetry-queue.jsonl`, capped at 2,000 with the oldest dropped first, and go out on a
   later launch. Retries back off from 30 s to 15 min. A 4xx other than 408/429 drops that batch
   and counts it, so one bad row can't block every later one.
3. **Nothing personal.** No audio, recordings, mix settings, channel names, session or file
   names, chat text, typed sentences or exception messages. The fields are fixed words and
   numbers. Any string field that contains a `/`, `\` or `~` is replaced with `redacted` before
   it is queued, and strings are cut to 80 printable characters. A device model is sent only
   for hardware (`interface`, `builtin`). An aggregate device or a pair of headphones is often
   named after its owner, so for those only the kind is sent.
4. **Anonymous.** `install_id` is a random UUID made on first launch and stored in
   `~/Library/DLIVE/telemetry.json`. It is kept out of sessions and out of `~/Music`.
   `session_id` is one launch of the app (a "run"), not a DLIVE session document.
5. **The user can switch it off.** Help > *Share anonymous usage data* (shown only when a
   project is configured). Off also empties the queue. The milestones keep working, because
   they are the user's and live on the Mac.
6. **Milestones never wait for the network.** They are decided in `Telemetry::learn()` from the
   local state file and shown as a toast the moment they are earned. `milestone_unlocked` is
   only the record of it.

## Configuration

| Where | What |
| --- | --- |
| CMake cache `DLIVE_SUPABASE_URL`, `DLIVE_SUPABASE_ANON_KEY` | Baked into a release build (`-DDLIVE_SUPABASE_URL=https://<ref>.supabase.co`). |
| Environment variables of the same names | Override the build: a developer pointing a run at a test project. |
| Neither | Off. Nothing is queued or sent; milestones still work. |

The anon key is public by design. It can call `POST /rest/v1/rpc/ingest_events` with
`{"rows": [...]}` (at most 100 rows) and nothing else: it can't read, insert into, change or
delete from `events`. RLS is on with no policy, so the table is closed to the API. The
function inserts the batch and skips an `event_id` that has already landed. Supabase's
advisor flags both of these (`rls_enabled_no_policy`, `anon_security_definer_function_executable`);
both are intended. A direct insert with ignore-duplicates would have needed `SELECT` for the
anon key, which is why the function exists.

Tested on 2026-09-29: the call returns 204, a repeated `event_id` is stored once, and a
read or a direct insert with the anon key returns 401.

## Local files (`~/Library/DLIVE/`)

| File | What | Lifetime |
| --- | --- | --- |
| `telemetry.json` | install ID, first seen, launches, sharing, total mixing seconds, milestones earned, features used, failed-send and dropped-row counts | for good |
| `telemetry-queue.jsonl` | rows not yet sent | until sent |
| `telemetry-run.json` | this run's session ID, version, uptime, activity, device, sample rate, buffer and channel count, rewritten about twice a minute | deleted on a clean quit; one found at launch = the last run ended badly |
| `telemetry-crash.txt` | written from the signal / `std::terminate` handler: the signal, the exception *type*, the raw backtrace | read and deleted at the next launch |

## Every row

| Column | Meaning |
| --- | --- |
| `event_id` | UUID made on the Mac; the retry key (`ingest_events` skips one that has already landed) |
| `install_id`, `session_id` | as above |
| `event` | the name, from the tables below |
| `ts` | the Mac's clock, ISO 8601 with offset. `received_at` is the server's |
| `app_version`, `os`, `os_version`, `arch` | e.g. `0.1.0`, `macOS`, `26.4`, `arm64` / `x86_64` / `x86_64-rosetta` |
| `props` | the event's own fields, below. At most 8 KB |

## Events

### Adoption, retention and time in the product

| Event | Props | Question it answers |
| --- | --- | --- |
| `app_started` | `first_run`, `launch` (nth on this install), `days_since_install`, `previous_run_crashed`, `mac_model` (e.g. `Mac17,8`), `cpu_cores`, `ram_gb` | DAU/WAU/MAU; new vs returning; sessions per user; retention (day N of `days_since_install`); the hardware DLIVE runs on |
| `app_ended` | `uptime_s`, `mixing_s`, `xruns`, `cpu_peak` (%), `rss_mb` | session duration; time spent mixing; a clean end (its absence plus `app_crash` = a crash) |
| `session_heartbeat` | every 5 min: `uptime_s`, `mixing_s`, `xruns`, `cpu_peak`, `rss_mb`, `inputs`, `tracks` | duration of a run that never ended cleanly; typical channel and track count while mixing |
| `feature_first_use` | `feature`, `days_since_install`, `launch` | which features are discovered, how soon, and which are never touched. Features: `tune_channel`, `tune_group`, `tune_channels`, `tune_mix`, `tune_live`, `tune_mix_buddy`, `keep_some`, `autopilot`, `sample_replacement`, `preset_scene`, `preset_favourite`, `preset_input_map`, `preset_recall`, `mix_buddy`, `recording`, `live_view`, `live_safe`, `speech_priority` |
| `milestone_unlocked` | `milestone`, `mixing_hours` | how far people get (see Milestones) |

"Mixing time" is wall-clock time with the audio device running and at least one input
assigned. A gap of more than 5 s between ticks (the Mac asleep) counts as nothing.

### Sessions

| Event | Props | Question |
| --- | --- | --- |
| `session_created` | - | new sessions vs reopened ones |
| `session_opened` | `source` (`user` / `launch` / `recovery`), `inputs`, `tracks`, `takes_repaired`, `device_fallback` | typical channel count; how often a session opens without its device |
| `recovery_offered` | `after_crash` | how often the autosave holds work the document doesn't |
| `session_recovery` | `choice` (`recover` / `keep_both` / `open_saved`), `ok`, `after_crash` | the recovery success rate |

### Tune

| Event | Props | Question |
| --- | --- | --- |
| `tune_result` | `scope` (`channel` / `group` / `channels` / `mix` / `live` / `mix_buddy`), `outcome` (`proposal` / `no_change` / `nothing_heard` / `no_signal` / `too_short` / `failed` / `cancelled`), `family` + `kind` (channel scope, from `UsageIds.h`), `group` (group scope: `DRUMS`, `BASS`, ...), `channels`, `inputs`, `changes`, `heard` | Tune use by scope; the most-tuned channel types; how often a listen fails and why (Tune failures) |
| `tune_decision` | `decision` (`kept` / `kept_some` / `reverted`), same scope fields, `changes` | Tune acceptance rate, full and partial |

A proposal left on preview when a new listen starts is kept by DLIVE itself, and counts as `kept`.

### Features

| Event | Props | Question |
| --- | --- | --- |
| `autopilot` | `state` (`on` / `off` / `refused`), `reason` when refused (`no_mix` / `silent`) | Autopilot adoption and its refusals |
| `sample_replacement_on` | `instrument` (`kick` / `snare` / `tom` ...), `by` (`hand` / `tune`) | sample replacement adoption, by instrument |
| `preset_saved` | `kind` (`scene` / `favourite` / `input_map`), `inputs` for a map | saved mixes and patches. DLIVE has no channel presets, so scenes, favourites and input maps stand in for them |
| `preset_applied` | `kind` | whether saved mixes get used again |
| `mix_buddy_used` | - | Mix Buddy use. The request's words are never sent |
| `live_view_opened` | `audio_running` | the Live workspace, which is the closest thing to a broadcast mode. There is no "go live" action |
| `live_safe` | `on` | LIVE SAFE use |
| `speech_priority` | `on` | speech priority use |

### Recording and the device

| Event | Props | Question |
| --- | --- | --- |
| `device_opened` | `device_kind` (`interface` / `builtin` / `bluetooth` / `display` / `virtual` / `aggregate` / `unknown`), `device_model` (hardware only), `sample_rate`, `buffer`, `device_inputs`, `device_outputs`, `inputs` | the interfaces, rates and buffers in use; the context for failures |
| `device_returned` | `down_s`, `device_kind` | the device came back and the audio engine restarted by itself |
| `recording_started` | `armed`, `inputs`, `sample_rate` | recording adoption; tracks per take |
| `recording_completed` | `seconds`, `armed`, `ok` | take lengths; recording failure rate (`ok = false`) |
| `audio_dropouts` | at most once a minute: `count`, `window_s`, `sample_rate`, `buffer`, `cpu`, `inputs`, `activity` | where dropouts happen (buffer too small, too many channels) |
| `cpu_high` | at most once every 10 min, after 5 s at 85 % or more: `cpu`, `sample_rate`, `buffer`, `inputs`, `activity` | machines that run out of headroom |
| `memory_high` | once a run, over 3 GB resident: `rss_mb`, `inputs`, `tracks` | memory growth |

### Stability

| Event | Props | Question |
| --- | --- | --- |
| `app_crash` | Sent on the next launch, under **the session ID and version of the run that died**. `detected` (`signal`, or `no_goodbye` for a kill, a power cut or a hang ended by force quit), `signal`, `exception_type` (the C++ type, never its message), `stack` (up to 48 frames of frame, module and demangled symbol, with no addresses and no folders), plus the run's last context: `uptime_s`, `mixing_s`, `xruns`, `activity`, `device_kind`, `device_model`, `sample_rate`, `buffer`, `inputs`, `recording` | crash-free runs and installs; crashes by version, OS, device and activity |
| `error` | `area`, `code`, `recovered`, plus the moment's context: `activity`, `audio_running`, `device_kind`, `device_model`, `sample_rate`, `buffer`, `inputs`, `recording`, and the fields listed below | the most common failure areas, and how often DLIVE carries on by itself |
| `telemetry_health` | `failed_sends`, `dropped_rows`, `last_status` | the telemetry's own failures, kept apart from the product's |

Error codes, all of them:

| `area` | `code` | `recovered` | Extra props |
| --- | --- | --- | --- |
| `app` | `unhandled_exception` (the message loop caught it and carried on) | true | `type`, `where` (file name and line only) |
| `audio_device` | `device_lost` | false (see `device_returned`) | `was_recording` |
| `recording` | `buffer_too_large`, `disk_too_slow`, `device_lost` | false | `seconds` |
| `session` | `load_failed`, `restore_failed` (the last session didn't reopen at launch), `autosave_unreadable`, `save_failed` | per code | `save_as` |
| `samples` | `import_failed` | true | `instrument` |

Autopilot's refusals are `autopilot` / `refused`, and Tune's failures are `tune_result`
outcomes. They are product answers rather than faults, so they are not duplicated as `error`.

## Milestones

Decided on the Mac, shown as a toast ("Milestone: Drums sounding fire. Your first drum tune."),
earned once per install.

| ID | Title | Earned by |
| --- | --- | --- |
| `tuned_in` | You're tuned in | the first channel tune that heard its channel |
| `drums_fire` | Drums sounding fire | the first drum-channel tune, or TUNE DRUMS |
| `bass_locked` | Bass locked in | the first bass-channel tune, or TUNE BASS |
| `your_sound` | That's your sound | the first scene or favourite kept |
| `one_hour` | One hour live | an hour of mixing, over any number of runs |
| `ten_hours` | Ten hours behind the mix | ten hours |

## Metrics: where each one comes from

| Metric | From |
| --- | --- |
| DAU / WAU / MAU | `analytics.daily_active`; distinct `install_id` of `app_started` over 7 / 30 days |
| New vs returning | `app_started.first_run` |
| Retention | cohort by the `first_run` day; returning = any `app_started` N days later |
| Sessions per user, session duration | `analytics.runs` (`seconds`) |
| Total hours mixed | `sum (mixing_seconds)` over `analytics.runs` |
| Feature use, discovery, never touched | `feature_first_use` (`analytics.feature_adoption`), and each feature's own event |
| Most tuned channel types | `analytics.channel_tunes_by_instrument` |
| Tune adoption / acceptance | `tune_result` installs over active installs; `analytics.tune_acceptance` |
| Autopilot and sample-replacement adoption | `feature_first_use` for `autopilot` / `sample_replacement` |
| Typical channel count | `session_opened.inputs`, `session_heartbeat.inputs` |
| Milestone progression | `analytics.milestones` |
| Feature use by version | any of the above grouped by `app_version` |
| Where people stop | the last `feature_first_use` before an install's last `app_started` |
| Crash-free runs / installs, crashes by version | `analytics.stability_by_version` |
| Crashes by OS, device, interface | `app_crash` grouped by `os_version`, `props.device_kind`, `props.device_model` |
| Failure areas, audio-engine and recording failure rates | `analytics.errors`; `recording_completed.ok` |
| Recovery success | `session_recovery.ok` |

Retention, for example:

```sql
with cohort as (
  select install_id, min (ts)::date as day0 from public.events
  where event = 'app_started' and (props ->> 'first_run')::boolean group by 1)
select day0, count (*) as installs,
       count (*) filter (where exists (select 1 from public.events e where e.install_id = c.install_id
                         and e.event = 'app_started' and e.ts::date between day0 + 7 and day0 + 13)) as week_2
from cohort c group by 1 order by 1;
```

## What is not covered, and why

- **Audio engine failures other than a lost device.** DLIVE's engine doesn't fail on its own. A
  device that stops is `device_lost`, and a restart is `device_returned`. Dropouts are the
  device's own xrun count; there is no CoreAudio overload listener.
- **A hang.** No watchdog. A hang ended by Force Quit shows up as `app_crash` with
  `detected = no_goodbye`.
- **Symbolication.** A release build's stack has exported symbol names only. Deeper frames
  need the dSYM of that version.
