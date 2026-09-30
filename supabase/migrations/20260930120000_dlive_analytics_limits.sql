-- ingest_events, limited by where a batch comes from and by what it weighs. docs/ANALYTICS.md "Security".
--
-- QA-2026-09-30 found three gaps in 20260929140000_dlive_analytics_rate_limit.sql:
--   1. The only limit a key-holder with fresh install IDs meets is the global one (30,000 an
--      hour), and hitting it locks every genuine install out for as long as they keep going.
--   2. props were capped with pg_column_size, which measures the *compressed* value: a
--      well-compressing 8 KB check let far larger rows in, and 30,000 of them an hour grew the
--      table by hundreds of megabytes an hour.
--   3. The limits were counted and then inserted with nothing in between, so concurrent calls
--      could all pass the same count and overshoot it together.
--
-- So: every call is serialised on one advisory lock (the volume is small - a few calls a minute
-- from a church's Mac - so this costs nothing real); each source address may send 1,200 events an
-- hour (a busy install sends under 150; twenty installs behind one church NAT still fit), counted
-- by a hash of the address kept only for the current and the previous hour; props are capped at
-- 8 KB of JSON text as sent (a crash's stack, the largest thing the app sends, is at most 6,000
-- characters) - measured as text, so compression no longer hides a larger row; and a table that has reached 5 GB takes nothing more until someone
-- looks at it. Over any limit PostgREST answers 429 and the app keeps its rows and backs off.

-- Written only by ingest_events (security definer). Not exposed: the analytics schema is
-- revoked from anon and authenticated.
create table if not exists analytics.ingest_sources (
    source  text        not null,            -- md5 of the caller's address: counted, never stored in the clear
    hour    timestamptz not null,
    events  int         not null default 0,
    primary key (source, hour)
);
revoke all on analytics.ingest_sources from anon, authenticated;

-- What new rows may weigh, as text. NOT VALID: rows already stored are not re-checked.
alter table public.events drop constraint if exists events_props_text_size;
alter table public.events add constraint events_props_text_size
    check (octet_length (props::text) <= 8192) not valid;

create or replace function public.ingest_events (rows jsonb)
returns void
language plpgsql
security definer
set search_path = ''
as $$
declare
    per_install_per_hour constant int    := 600;
    per_source_per_hour  constant int    := 1200;
    everyone_per_hour    constant int    := 30000;
    max_props_bytes      constant int    := 8192;
    max_table_bytes      constant bigint := 5368709120;     -- 5 GB
    batch_install uuid;
    batch_size    int;
    installs      int;
    headers       json;
    address       text;
    source_key    text;
    this_hour     timestamptz := date_trunc ('hour', now());
    from_source   int;
begin
    if jsonb_typeof (rows) <> 'array' or jsonb_array_length (rows) = 0 or jsonb_array_length (rows) > 100 then
        raise exception 'rows must be an array of 1 to 100 events' using errcode = '22023';
    end if;
    batch_size := jsonb_array_length (rows);

    select count (distinct r ->> 'install_id'), min (r ->> 'install_id')::uuid   -- no min(uuid) before Postgres 18
      into installs, batch_install
      from jsonb_array_elements (rows) as r;
    if installs <> 1 then
        raise exception 'a batch carries one install' using errcode = '22023';
    end if;
    if exists (select 1 from jsonb_array_elements (rows) as r
                where octet_length (coalesce (r -> 'props', '{}'::jsonb)::text) > max_props_bytes) then
        raise exception 'props may be at most 8 KB' using errcode = '22023';
    end if;

    -- One call at a time: a count and the insert it allows are one step.
    perform pg_advisory_xact_lock (hashtext ('dlive.ingest_events'));

    -- Where it came from, as the API gateway reports it. Hashed, and only ever compared.
    headers := nullif (current_setting ('request.headers', true), '')::json;
    address := coalesce (headers ->> 'cf-connecting-ip',
                         nullif (split_part (coalesce (headers ->> 'x-forwarded-for', ''), ',', 1), ''),
                         'unknown');
    source_key := md5 ('dlive:' || trim (address));

    delete from analytics.ingest_sources where hour < this_hour - interval '1 hour';
    select coalesce (sum (events), 0) into from_source
      from analytics.ingest_sources where source = source_key;

    if from_source + batch_size > per_source_per_hour
       or (select count (*) from public.events
            where install_id = batch_install and received_at > now() - interval '1 hour') + batch_size > per_install_per_hour
       or (select count (*) from public.events
            where received_at > now() - interval '1 hour') + batch_size > everyone_per_hour
       or pg_total_relation_size ('public.events') > max_table_bytes
    then
        raise sqlstate 'PGRST'
            using message = '{"code":"DLIVE429","message":"too many events; try again later"}',
                  detail  = '{"status":429,"headers":{"Retry-After":"60"}}';
    end if;

    insert into analytics.ingest_sources (source, hour, events)
    values (source_key, this_hour, batch_size)
    on conflict (source, hour) do update set events = analytics.ingest_sources.events + excluded.events;

    insert into public.events (event_id, install_id, session_id, event, ts,
                               app_version, os, os_version, arch, props)
    select (r ->> 'event_id')::uuid,
           (r ->> 'install_id')::uuid,
           (r ->> 'session_id')::uuid,
           r ->> 'event',
           (r ->> 'ts')::timestamptz,
           r ->> 'app_version',
           r ->> 'os',
           r ->> 'os_version',
           r ->> 'arch',
           coalesce (r -> 'props', '{}'::jsonb)
    from jsonb_array_elements (rows) as r
    on conflict (event_id) do nothing;
end;
$$;

revoke all on function public.ingest_events (jsonb) from public, authenticated;
grant execute on function public.ingest_events (jsonb) to anon;
