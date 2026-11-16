# airquality-receiver

Small HTTP endpoint for `firmware/airquality_logger`: accepts one JSON row per
`POST /ingest`, appends it to a SQLite database. Meant to run on your home LAN
only — plain HTTP, a shared API key, no TLS. Don't port-forward it.

## Deploy on the DS923plus (Synology, Container Manager)

1. Copy this folder onto the NAS (e.g. via the Synology `File Station` app, or
   `scp -r tools/airquality-receiver andrecouture@ds923plus:/volume1/docker/`).
2. Copy `env.example` to `.env` (same folder, git-ignored — `docker-compose.yml`
   reads `API_KEY` from it, so the real value never lands in a commit) and set
   `API_KEY` to a value you generate yourself (`openssl rand -hex 16` on your
   Mac). Put the **same** value in `firmware/airquality_logger/secrets.h` as
   `NAS_API_KEY`.
3. In Container Manager: **Project → Create**, point it at the folder, and
   build/start it. Or from an SSH session on the NAS:
   ```
   cd /volume1/docker/airquality-receiver
   docker compose up -d --build
   ```
4. The database file lands in `./data/airquality.db` next to the compose file
   (bind-mounted, so it survives container rebuilds).
5. Check it's up: `curl http://ds923plus:8787/health` → `{"ok": true}`.

## Deploy without Docker (plain Python, e.g. for testing on a Mac)

```
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
API_KEY=change-me DB_PATH=./airquality.db python3 app.py
```

## Endpoints

- `GET /health` — no auth, for container health checks.
- `POST /ingest` — requires header `X-API-Key: <API_KEY>`. Body is one JSON
  object per row (matches what the ESP32 sends):
  ```json
  {
    "device_id": "printroom1", "ts": 1763310000,
    "pm1_0": 3.1, "pm2_5": 4.2, "pm4_0": 4.5, "pm10": 5.0,
    "voc_index": 112, "nox_index": 1,
    "temp_c": 23.4, "rh_pct": 41.2,
    "co2_ppm": 612,
    "printer_w": 187.3, "printer_online": true
  }
  ```
  Re-sending the same `(device_id, ts)` overwrites the row instead of
  duplicating it, so the ESP32's retry-on-failure queue is safe to replay.
- `GET /recent?limit=50&device_id=printroom1` — requires the same header,
  returns the most recent rows as JSON. Handy for checking the logger is
  actually getting through:
  ```
  curl -H "X-API-Key: change-me" "http://ds923plus:8787/recent?limit=5"
  ```

## Schema

```sql
CREATE TABLE readings (
  device_id      TEXT    NOT NULL,
  ts             INTEGER NOT NULL,      -- unix seconds, logger's own (NTP) clock
  pm1_0          REAL,
  pm2_5          REAL,
  pm4_0          REAL,
  pm10           REAL,
  voc_index      REAL,
  nox_index      REAL,
  temp_c         REAL,
  rh_pct         REAL,
  co2_ppm        REAL,
  printer_w      REAL,
  printer_online INTEGER,
  received_at    INTEGER NOT NULL,      -- unix seconds, server clock, when it arrived
  PRIMARY KEY (device_id, ts)
);
```

## Example: air quality during prints

Once there's some history, query the file directly with `sqlite3`:

```sql
-- Average VOC/NOx/PM2.5 while the printer was drawing power vs. idle
SELECT
  printer_online,
  ROUND(AVG(voc_index), 1) AS avg_voc,
  ROUND(AVG(nox_index), 1) AS avg_nox,
  ROUND(AVG(pm2_5), 1)     AS avg_pm2_5,
  COUNT(*)                 AS samples
FROM readings
WHERE device_id = 'printroom1'
GROUP BY printer_online;
```

## Notes

- One always-on service, a few KB/day of writes — no need for anything heavier
  than SQLite here.
- If you later want a dashboard instead of ad-hoc `sqlite3` queries, this
  database is a fine data source for one (Grafana with the SQLite plugin, or a
  tiny custom page) — not built here, just didn't want the schema to preclude
  it.
