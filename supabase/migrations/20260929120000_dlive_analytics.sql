-- DLIVE usage and stability events. docs/ANALYTICS.md is the contract for every row.
--
-- One table. The app (app/native/Telemetry) inserts batches with the project's anon key via
-- PostgREST: POST /rest/v1/events?on_conflict=event_id with
-- "Prefer: return=minimal,resolution=ignore-duplicates", so a retried batch that had in fact
-- landed is ignored rather than counted twice. The anon key may insert and nothing else: it
-- cannot read, change or delete a single row. The questions are answered by the views in the
-- `analytics` schema, which PostgREST does not expose.

create table if not exists public.events (
    id           bigint generated always as identity primary key,
    event_id     uuid        not null unique,           -- made on the Mac; the retry key
    install_id   uuid        not null,                  -- anonymous, random, per install
    session_id   uuid        not null,                  -- one launch of the app ("a run")
    event        text        not null check (event ~ '^[a-z][a-z0-9_]{1,63}$'),
    ts           timestamptz not null,                  -- the Mac's clock
    received_at  timestamptz not null default now(),    -- the server's clock
    app_version  text        not null check (char_length (app_version) <= 32),
    os           text        not null check (char_length (os) <= 16),
    os_version   text        not null check (char_length (os_version) <= 32),
    arch         text        not null check (char_length (arch) <= 24),
    props        jsonb       not null default '{}'::jsonb
                             check (jsonb_typeof (props) = 'object' and pg_column_size (props) <= 8192)
);

comment on table public.events is 'DLIVE usage and stability events - docs/ANALYTICS.md. Insert-only for anon.';

create index if not exists events_event_ts_idx   on public.events (event, ts);
create index if not exists events_install_ts_idx on public.events (install_id, ts);
create index if not exists events_session_idx    on public.events (session_id);
create index if not exists events_ts_brin        on public.events using brin (ts);

-- ---- access: insert only ----
alter table public.events enable row level security;

revoke all on public.events from anon, authenticated;
grant insert (event_id, install_id, session_id, event, ts, app_version, os, os_version, arch, props)
    on public.events to anon;

drop policy if exists "anon inserts events" on public.events;
create policy "anon inserts events" on public.events
    for insert to anon
    with check (true);

-- ---- the questions (docs/ANALYTICS.md "Metrics") ----
-- A schema of its own, not exposed through the API. Read from the SQL editor or a dashboard
-- connected with the service role.
create schema if not exists analytics;
revoke all on schema analytics from anon, authenticated;

-- Active installs per day. WAU / MAU are the same query over a wider window.
create or replace view analytics.daily_active as
select date_trunc ('day', ts) as day, count (distinct install_id) as installs
from public.events
where event = 'app_started'
group by 1;

-- One row per run: when, how long, how much of it was mixing, and how it ended.
create or replace view analytics.runs as
select
    s.session_id,
    s.install_id,
    s.app_version,
    s.os_version,
    min (s.ts) filter (where s.event = 'app_started')                                   as started_at,
    bool_or (s.event = 'app_started' and (s.props ->> 'first_run')::boolean)            as first_run,
    max (coalesce ((s.props ->> 'uptime_s')::int, 0))
        filter (where s.event in ('app_ended', 'session_heartbeat', 'app_crash'))       as seconds,
    max (coalesce ((s.props ->> 'mixing_s')::int, 0))
        filter (where s.event in ('app_ended', 'session_heartbeat', 'app_crash'))       as mixing_seconds,
    bool_or (s.event = 'app_ended')                                                     as ended_cleanly,
    bool_or (s.event = 'app_crash')                                                     as crashed
from public.events s
group by s.session_id, s.install_id, s.app_version, s.os_version;

-- Crash-free runs and installs, by version.
create or replace view analytics.stability_by_version as
select
    app_version,
    count (*)                                                        as runs,
    count (*) filter (where crashed)                                 as crashed_runs,
    round (100.0 * count (*) filter (where not crashed) / nullif (count (*), 0), 2) as crash_free_run_pct,
    count (distinct install_id)                                      as installs,
    count (distinct install_id) filter (where crashed)               as installs_with_a_crash
from analytics.runs
group by app_version;

-- Tune: how often it is used, at what scope, and how often the result is kept.
create or replace view analytics.tune_acceptance as
select
    props ->> 'scope'                                                  as scope,
    count (*) filter (where props ->> 'decision' = 'kept')             as kept,
    count (*) filter (where props ->> 'decision' = 'kept_some')        as kept_some,
    count (*) filter (where props ->> 'decision' = 'reverted')         as reverted,
    round (100.0 * count (*) filter (where props ->> 'decision' in ('kept', 'kept_some'))
           / nullif (count (*), 0), 1)                                 as acceptance_pct
from public.events
where event = 'tune_decision'
group by 1;

-- Which instruments people tune on their own.
create or replace view analytics.channel_tunes_by_instrument as
select props ->> 'kind' as kind, props ->> 'family' as family, count (*) as tunes,
       count (distinct install_id) as installs
from public.events
where event = 'tune_result' and props ->> 'scope' = 'channel'
  and props ->> 'outcome' in ('proposal', 'no_change')
group by 1, 2;

-- Discovery: how many installs have ever used each feature, and how soon.
create or replace view analytics.feature_adoption as
select props ->> 'feature' as feature,
       count (distinct install_id) as installs,
       percentile_cont (0.5) within group (order by (props ->> 'days_since_install')::int) as median_days_to_first_use
from public.events
where event = 'feature_first_use'
group by 1;

-- Milestones reached.
create or replace view analytics.milestones as
select props ->> 'milestone' as milestone, count (distinct install_id) as installs
from public.events
where event = 'milestone_unlocked'
group by 1;

-- Failures by area and code, with how often DLIVE carried on by itself.
create or replace view analytics.errors as
select app_version, props ->> 'area' as area, props ->> 'code' as code,
       count (*) as occurrences, count (distinct install_id) as installs,
       count (*) filter (where (props ->> 'recovered')::boolean) as recovered
from public.events
where event = 'error'
group by 1, 2, 3;
