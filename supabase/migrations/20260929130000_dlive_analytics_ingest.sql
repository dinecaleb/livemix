-- DLIVE events arrive through one function, not through the table.
--
-- PostgREST's "ignore duplicates" upsert needs SELECT on the table, and the anon key must
-- never be able to read it. So the anon key loses INSERT on public.events and gets EXECUTE on
-- public.ingest_events(rows) instead: it inserts a batch, skips an event_id that already
-- landed (a retry after a timeout), and returns nothing. The table's checks still apply, so a
-- malformed row fails the batch, which the app counts and drops (docs/ANALYTICS.md).

revoke insert on public.events from anon;
drop policy if exists "anon inserts events" on public.events;
-- RLS stays on with no policy for anon: the table is closed to the API entirely.

create or replace function public.ingest_events (rows jsonb)
returns void
language plpgsql
security definer
set search_path = ''
as $$
begin
    if jsonb_typeof (rows) <> 'array' or jsonb_array_length (rows) > 100 then
        raise exception 'rows must be an array of at most 100 events' using errcode = '22023';
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
