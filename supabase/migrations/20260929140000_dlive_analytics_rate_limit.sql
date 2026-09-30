-- ingest_events, rate-limited. docs/ANALYTICS.md "Security".
--
-- The anon key ships inside the app, so anyone can call ingest_events. What they cannot do is
-- read a row; what they could do is spam. Two limits, both counted on the server's clock
-- (received_at), never the Mac's:
--   - per install: 600 events in the last hour. Ordinary use is well under 150; a week's
--     offline backlog (the app keeps at most 2,000) drains over a few hours instead.
--   - everyone: 30,000 events in the last hour, so nobody can fill the database, even with a
--     fresh install ID on every call. A flood can make genuine events wait; it cannot make
--     the database grow without bound.
-- Over a limit, PostgREST answers 429, which the app treats as "try later" and backs off
-- (30 s doubling to 15 min) with its rows kept. A batch must be one install's.

create index if not exists events_install_received_idx on public.events (install_id, received_at);
create index if not exists events_received_idx on public.events (received_at);

create or replace function public.ingest_events (rows jsonb)
returns void
language plpgsql
security definer
set search_path = ''
as $$
declare
    per_install_per_hour constant int := 600;
    everyone_per_hour    constant int := 30000;
    batch_install uuid;
    batch_size    int;
    installs      int;
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

    if (select count (*) from public.events
         where install_id = batch_install and received_at > now() - interval '1 hour') + batch_size > per_install_per_hour
       or (select count (*) from public.events
            where received_at > now() - interval '1 hour') + batch_size > everyone_per_hour
    then
        raise sqlstate 'PGRST'
            using message = '{"code":"DLIVE429","message":"too many events; try again later"}',
                  detail  = '{"status":429,"headers":{"Retry-After":"60"}}';
    end if;

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
