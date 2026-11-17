# Data contract

This is the one thing the device firmware, the server and the apps all have to agree on.
If the hardware ends up sending something slightly different, change it here first.

Base URL: `https://thor.cnt.sast.ca/~lantiveros1t1251/noise/` (every path below is relative to it).
The server is PHP and MariaDB on thor. There is one device, `NM-0001`, and no login.

## Device to server

### Endpoint

    POST https://thor.cnt.sast.ca/~lantiveros1t1251/noise/api/readings.php
    (plain HTTP on port 80 also works, for testing only)

Headers: `Content-Type: application/json`, `X-Device-Key: <the device key, ask Lovelyn>`, `Content-Length`, `Connection: close`.
The device key goes in the header, never in the body or in Git.

### Body (one reading, or many)

    {"device_id":"NM-0001","readings":[
      {"ts":"2026-11-09T17:30:00Z","db":52.4,"lat":53.5461,"lon":-113.4938,"clipped":false},
      {"ts":"2026-11-09T17:30:01Z","db":53.1,"lat":53.5461,"lon":-113.4938,"clipped":false}]}

A single reading can also be sent flat: `{"device_id":"NM-0001","ts":"...","db":52.4,"lat":...,"lon":...}`.

- `ts`: ISO 8601 with a timezone (`Z` for UTC). Required.
- `db`: number from 0 to 200. Required.
- `lat` and `lon`: both or neither. Leave both out while there is no GNSS fix.
- `clipped`: true or false, optional. True when the microphone maxed out.
- Up to 10,000 readings per request and 2 MB per body. Sending a batch of 30 to 60 readings is a good size for the XBee.
- Re-sending a reading with the same `device_id` and `ts` is safe, it is counted as a duplicate.

### Replies

| Status | Meaning |
|---|---|
| 200 | `{"accepted":N,"duplicates":N,"rejected":[...],"sessions":[...]}` |
| 400 | bad JSON, or every reading was rejected (reasons are in `rejected`) |
| 401 | missing or wrong `X-Device-Key` |
| 404 | unknown `device_id` (use `NM-0001`) |
| 405 | not a POST |
| 413 | body too large |
| 500 | server error |

A request counts as delivered only when the status is 200. If it is not, keep the readings and send them again later.

### Raw HTTP text to send through the XBee socket

    POST /~lantiveros1t1251/noise/api/readings.php HTTP/1.1
    Host: thor.cnt.sast.ca
    Content-Type: application/json
    X-Device-Key: <key>
    Content-Length: <length of the body in bytes>
    Connection: close

    {"device_id":"NM-0001","readings":[...]}

`Content-Length` must be the exact byte count of the body.

### Device key

The key is stored on the server in `noise-config.php` (outside Git) as `device_key`. The firmware sends it in
the `X-Device-Key` header. Without it anyone who knows a `device_id` could post data for it.

### What the device should do with each reply

| Code | Meaning | What the device should do |
|---|---|---|
| 200 | Stored (some readings may be listed under `rejected`) | Delete the buffered readings |
| 400 | Every reading was invalid, or the body is not valid JSON | Do not retry the same data |
| 401 | Missing or wrong device key | Do not retry, check the key stored in the firmware |
| 404 | Unknown `device_id` | Do not retry, the device is not registered |
| 5xx / no response | Server or network problem | Keep the data and retry |

Order inside a request does not matter, the server sorts by `ts`. Uploading buffered data after being offline is
the normal case for a batch.

## How the server groups readings

- **Sessions.** Readings from one device belong to the same session until there is a gap of more than 60 seconds
  with no readings (`SESSION_GAP_MS` in `api/lib.php`). A longer gap starts a new session.
- **Location.** A session has one location, the first GNSS fix received. If readings arrived before that first
  fix, they are updated to that location afterwards.
- **Moved flag.** If a later fix in the same session is more than 50 m (`MOVE_THRESHOLD_M`) from the first one,
  the session is flagged `moved`. The 50 m value is a placeholder until real GNSS drift has been measured.
- **Clipping.** Readings with `clipped: true` are kept and counted. They are included in the statistics, and the
  session page warns that those levels are lower bounds.
- **Duplicates.** A reading for a `device_id` and `ts` the server already has is ignored, so retrying is safe.

## Interpreted data

Computed on the server from the raw per-second readings, per session and per minute:

| Value | Meaning |
|---|---|
| `leq` | Equivalent continuous level: the energy average, `10 * log10(mean(10^(L/10)))` |
| `lmax`, `lmin` | Highest and lowest per-second reading |
| `l10`, `l50`, `l90` | Level exceeded for 10%, 50% and 90% of the time |

Per-minute rows carry `minute_start`, `leq`, `lmax` and `n` (the number of readings in that minute).
Whether levels are A-weighted (ISO 1996-2 uses A-weighted levels) is not decided yet, so the site labels them
as plain dB.

## Server to web app

| Request | Returns |
|---|---|
| `GET api/sessions.php` | `{ "sessions": [...] }`, newest first, each with its headline summary |
| `GET api/sessions.php?id=N` | One session with its summary, `per_minute` values and every raw reading |
| `GET api/sessions.php?id=N&format=csv` | The raw readings as a CSV download (`timestamp_utc,db,latitude,longitude,clipped`) |
| `POST api/delete_session.php` | Header `X-Admin-Key`, body `{"id":N}`. Returns `{deleted_session, deleted_readings}`. 401, 400, 404, 405, 413 or 500 on failure |

A session looks like this:

```json
{
  "id": 7, "device_id": "NM-0001",
  "started_at": "2026-11-09T17:30:00.000Z", "ended_at": "2026-11-09T17:50:00.000Z", "duration_s": 1200,
  "location": { "lat": 53.5461, "lon": -113.4938 },
  "moved": false, "reading_count": 1200, "clipped_count": 0,
  "warnings": [],
  "summary": { "leq": 54.2, "lmax": 71.0, "lmin": 41.3, "l10": 58.1, "l50": 52.0, "l90": 44.6 }
}
```

`location` is `null` when no GNSS fix arrived. `warnings` holds plain-language messages for the page (moved,
clipped readings, no position). Delete needs the separate `admin_key`, which the device never uses.

## Phone and device (Bluetooth)

Not part of the server. The messages between the mobile app and the XBee (status, set time, set location) are
defined in Sadie's Bluetooth protocol document in the app repository.

## Not decided yet

- How the device handles the server certificate over HTTPS (HTTP on port 80 works for testing only).
- Whether the device sends one reading per request or batches of about 30 to 60.