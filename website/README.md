# Website and database

The server, database and web dashboard for the Noise Pollution Monitoring and Reporting System. The device sends one A-weighted sound level (dB(A)) per second to the API. The dashboard groups the readings into recording sessions and shows them as charts, summary levels and a map.

## What it does

- **API (PHP + MariaDB):** accepts readings from the device, stores them, groups them into sessions, and returns sessions and summary levels.
- **Dashboard (plain HTML, CSS and JavaScript):** one screen with
  - summary tiles (sessions, recorded time, loudest level, LAeq of the selected session),
  - a chart with Level, Per minute and Raw readings tabs,
  - the share of time spent in the quiet, moderate and loud ranges,
  - a session list and a Leaflet map (all sessions or the selected session),
  - CSV download and session delete.
- Times are stored in UTC on the server and shown in the viewer's local time zone.

## Folder layout

| Path | Contents |
|---|---|
| `web/index.html`, `web/style.css`, `web/app.js` | The dashboard. The three files must be deployed together. |
| `web/api/readings.php` | `POST`: the device uploads readings (needs the device key) |
| `web/api/sessions.php` | `GET`: session list, one session with readings, or a CSV download |
| `web/api/delete_session.php` | `POST`: deletes a session (needs the admin key) |
| `web/api/summary.php` | Summary levels: LAeq, LAmax, LAmin, LA10, LA50, LA90 and per-minute values |
| `web/api/health.php` | `GET`: checks that the database is reachable |
| `web/api/db.php`, `web/api/lib.php` | Database connection and shared helpers |
| `docs/data-contract.md` | The agreement between the firmware, the server and the apps: request format, reply codes and retry rules |
| `server/`, `package.json` | A small Node server from early development. The PHP API is what runs on the school server. |

## API summary

The full format is in [`docs/data-contract.md`](docs/data-contract.md).

| Endpoint | Method | Auth | Purpose |
|---|---|---|---|
| `api/readings.php` | POST | `X-Device-Key` header | Upload one reading or a batch (up to 10,000 readings, 2 MB) |
| `api/sessions.php` | GET | none | List sessions, or `?id=N` for one session, or `?id=N&format=csv` for a download |
| `api/delete_session.php` | POST | `X-Admin-Key` header | Delete the session given as `{"id": N}` |
| `api/health.php` | GET | none | Returns `{"ok": true}` when the database is up |

A reading has `ts` (ISO 8601 time), `db` (0 to 200), optional `lat` and `lon` (both or neither), and optional `clipped`. Sending the same `device_id` and `ts` twice is safe, and the second copy is counted as a duplicate.

## How readings become sessions

- A new session starts when more than 60 seconds pass without a reading.
- A session is marked as moved when the device travels more than 50 m from where it started.
- LAeq is the energy average of the readings, not the average of the dB values.
- LA10, LA50 and LA90 are the levels exceeded for 10, 50 and 90 percent of the session.
- The quiet, moderate and loud bands on the chart use 55 and 70 dB(A). These are placeholder thresholds.

## Setup

1. Create a MariaDB database and tables for devices, readings and sessions on the server.
2. Create `noise-config.php` in the account's home folder, **above** `public_html`, so it is never served and never committed:

   ```php
   <?php
   return [
     'db_host'    => '...',
     'db_name'    => '...',
     'db_user'    => '...',
     'db_pass'    => '...',
     'device_key' => '...',   // sent by the device in X-Device-Key
     'admin_key'  => '...',   // needed to delete a session
   ];
   ```

3. Deploy the `web/` folder to `public_html/noise/`. This repository's `.cpanel.yml` copies the files when the server pulls from GitHub.
4. Open `api/health.php` in a browser. It should return `{"ok":true,...}`.

Never put the device key or the admin key in the repository.

## Testing

Send a test reading (replace the placeholders):

```
curl -X POST "<base-url>/api/readings.php" \
  -H "Content-Type: application/json" \
  -H "X-Device-Key: <device key>" \
  -d '{"device_id":"NM-0001","readings":[{"ts":"2026-11-09T17:30:00Z","db":52.4}]}'
```

A `200` reply with `"accepted":1` means it was stored. Use `?format=csv` on a session to check the raw data.

## Known limits

- The prototype uses one device (`NM-0001`) and has no login.
- The device sends over plain HTTP, so readings and the device key are not encrypted in transit.
- Test sessions recorded with fixed coordinates are in the database until they are deleted with `delete_session.php`.
