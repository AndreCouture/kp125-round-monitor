#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 12113775 Canada Inc.
#
# Small HTTP receiver for firmware/airquality_logger: accepts one JSON row per
# POST /ingest and appends it to a SQLite database. Meant to run on the home
# LAN only (plain HTTP, a shared API key, no TLS) behind the router's normal
# firewall — don't port-forward this.
#
# Env vars:
#   API_KEY   required. Must match NAS_API_KEY in the ESP32's secrets.h.
#   DB_PATH   default ./airquality.db
#   PORT      default 8787

import json
import os
import sqlite3
import time
from http import HTTPStatus

from flask import Flask, g, jsonify, request

API_KEY = os.environ.get("API_KEY")
DB_PATH = os.environ.get("DB_PATH", "./airquality.db")
PORT = int(os.environ.get("PORT", "8787"))

if not API_KEY:
    raise SystemExit("API_KEY env var is required (same value as NAS_API_KEY in secrets.h)")

app = Flask(__name__)

SCHEMA = """
CREATE TABLE IF NOT EXISTS readings (
  device_id      TEXT    NOT NULL,
  ts             INTEGER NOT NULL,      -- unix seconds, from the logger's own clock (NTP)
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
  received_at    INTEGER NOT NULL,      -- unix seconds, server clock, when this row arrived
  PRIMARY KEY (device_id, ts)
);
CREATE INDEX IF NOT EXISTS idx_readings_ts ON readings(ts);
"""

REQUIRED_FIELDS = ["device_id", "ts"]
NUMERIC_FIELDS = [
    "pm1_0", "pm2_5", "pm4_0", "pm10", "voc_index", "nox_index",
    "temp_c", "rh_pct", "co2_ppm", "printer_w",
]


def get_db():
    db = getattr(g, "_db", None)
    if db is None:
        db = g._db = sqlite3.connect(DB_PATH)
        db.execute("PRAGMA journal_mode=WAL")
        db.executescript(SCHEMA)
    return db


@app.teardown_appcontext
def close_db(_exc):
    db = getattr(g, "_db", None)
    if db is not None:
        db.close()


def check_api_key():
    return request.headers.get("X-API-Key") == API_KEY


@app.route("/health", methods=["GET"])
def health():
    return jsonify(ok=True)


@app.route("/ingest", methods=["POST"])
def ingest():
    if not check_api_key():
        return jsonify(error="bad or missing X-API-Key"), HTTPStatus.UNAUTHORIZED

    try:
        row = request.get_json(force=True, silent=False)
    except Exception:
        return jsonify(error="invalid JSON body"), HTTPStatus.BAD_REQUEST
    if not isinstance(row, dict):
        return jsonify(error="JSON body must be an object"), HTTPStatus.BAD_REQUEST

    missing = [f for f in REQUIRED_FIELDS if f not in row]
    if missing:
        return jsonify(error=f"missing fields: {missing}"), HTTPStatus.BAD_REQUEST

    values = {f: row.get(f) for f in NUMERIC_FIELDS}
    device_id = str(row["device_id"])[:64]
    try:
        ts = int(row["ts"])
    except (TypeError, ValueError):
        return jsonify(error="ts must be an integer (unix seconds)"), HTTPStatus.BAD_REQUEST
    printer_online = 1 if row.get("printer_online") else 0

    db = get_db()
    db.execute(
        """
        INSERT INTO readings
          (device_id, ts, pm1_0, pm2_5, pm4_0, pm10, voc_index, nox_index,
           temp_c, rh_pct, co2_ppm, printer_w, printer_online, received_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(device_id, ts) DO UPDATE SET
          pm1_0=excluded.pm1_0, pm2_5=excluded.pm2_5, pm4_0=excluded.pm4_0, pm10=excluded.pm10,
          voc_index=excluded.voc_index, nox_index=excluded.nox_index,
          temp_c=excluded.temp_c, rh_pct=excluded.rh_pct, co2_ppm=excluded.co2_ppm,
          printer_w=excluded.printer_w, printer_online=excluded.printer_online
        """,
        (
            device_id, ts,
            values["pm1_0"], values["pm2_5"], values["pm4_0"], values["pm10"],
            values["voc_index"], values["nox_index"],
            values["temp_c"], values["rh_pct"], values["co2_ppm"],
            values["printer_w"], printer_online, int(time.time()),
        ),
    )
    db.commit()
    return jsonify(ok=True)


@app.route("/recent", methods=["GET"])
def recent():
    if not check_api_key():
        return jsonify(error="bad or missing X-API-Key"), HTTPStatus.UNAUTHORIZED
    limit = min(int(request.args.get("limit", 50)), 1000)
    device_id = request.args.get("device_id")
    db = get_db()
    if device_id:
        cur = db.execute(
            "SELECT * FROM readings WHERE device_id = ? ORDER BY ts DESC LIMIT ?",
            (device_id, limit),
        )
    else:
        cur = db.execute("SELECT * FROM readings ORDER BY ts DESC LIMIT ?", (limit,))
    cols = [d[0] for d in cur.description]
    rows = [dict(zip(cols, r)) for r in cur.fetchall()]
    return app.response_class(json.dumps(rows), mimetype="application/json")


if __name__ == "__main__":
    # For a small home-LAN service, waitress is enough and keeps the image light.
    from waitress import serve

    print(f"airquality-receiver listening on 0.0.0.0:{PORT}, db={DB_PATH}")
    serve(app, host="0.0.0.0", port=PORT)
