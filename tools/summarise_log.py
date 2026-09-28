#!/usr/bin/env python3
"""
Boil an SD-card log down to one row per hour, for the build guide's charts.

    tools/summarise_log.py co2_0001.csv --utc-offset 3

Writes docs/data/long-run-hourly.csv and docs/data/long-run-summary.json.
The raw log is megabytes and stays out of the repo; these two files are small.

Wall-clock time is rebuilt from uptime_s, anchored on the LAST timestamp in the
file. The time_utc column is not trusted row by row: the clock can be re-set
mid-run (one log shifts by exactly three hours), while uptime never jumps.
"""
import argparse, csv, json, os, statistics as st, datetime as dt

ap = argparse.ArgumentParser()
ap.add_argument("log")
ap.add_argument("--utc-offset", type=float, default=0, help="hours to add to UTC for local time")
ap.add_argument("--low", type=float, default=4.5)
ap.add_argument("--high", type=float, default=5.5)
a = ap.parse_args()

rows = [r for r in csv.DictReader(open(a.log)) if r["status"] != "WARMING UP"]
up = [int(r["uptime_s"]) for r in rows]
pct = [float(r["co2_ppm"]) / 10000 for r in rows]
temp = [float(r["temp_c"]) if r["temp_c"] else None for r in rows]
last = next(r for r in reversed(rows) if r["time_utc"])
boot = (dt.datetime.strptime(last["time_utc"], "%Y-%m-%dT%H:%M:%SZ")
        - dt.timedelta(seconds=int(last["uptime_s"])) + dt.timedelta(hours=a.utc_offset))
local = lambda u: boot + dt.timedelta(seconds=u)

# An excursion starts when the reading leaves the band downward and ends when it returns.
starts, inside, depth = [], False, []
for i, v in enumerate(pct):
    if v < a.low and not inside: inside, s = True, i
    elif v >= a.low and inside:
        inside = False; starts.append(s); depth.append(min(pct[s:i]))

first_hour = local(up[0]).replace(minute=0, second=0, microsecond=0)
buckets = {}
for i, u in enumerate(up):
    h = int((local(u) - first_hour).total_seconds() // 3600)
    buckets.setdefault(h, []).append(i)
exc = {}
for s in starts:
    h = int((local(up[s]) - first_hour).total_seconds() // 3600)
    exc[h] = exc.get(h, 0) + 1

here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, "..", "docs", "data")
os.makedirs(out, exist_ok=True)
with open(os.path.join(out, "long-run-hourly.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["hour", "local_time", "co2_median_pct", "co2_min_pct", "co2_max_pct",
                "quiet_median_pct", "temp_median_c", "excursions", "samples"])
    for h in sorted(buckets):
        idx = buckets[h]
        v = [pct[i] for i in idx]
        quiet = [x for x in v if a.low <= x <= a.high]
        t = [temp[i] for i in idx if temp[i] is not None]
        w.writerow([h, (first_hour + dt.timedelta(hours=h)).strftime("%Y-%m-%dT%H:00"),
                    f"{st.median(v):.4f}", f"{min(v):.4f}", f"{max(v):.4f}",
                    f"{st.median(quiet):.4f}" if len(quiet) >= 30 else "",
                    f"{st.median(t):.2f}" if t else "", exc.get(h, 0), len(idx)])

summary = dict(
    days=(up[-1] - up[0]) / 86400, samples=len(rows), restarts=sum(1 for i in range(1, len(up)) if up[i] < up[i - 1]),
    in_band_pct=100 * sum(1 for v in pct if a.low <= v <= a.high) / len(pct),
    above_band_pct=100 * sum(1 for v in pct if v > a.high) / len(pct),
    excursions=len(starts), lowest_pct=min(pct), median_depth_pct=st.median(depth) if depth else None,
    utc_offset=a.utc_offset,
    start_local=local(up[0]).strftime("%Y-%m-%d %H:%M"), end_local=local(up[-1]).strftime("%Y-%m-%d %H:%M"))
json.dump(summary, open(os.path.join(out, "long-run-summary.json"), "w"), indent=1)
print(json.dumps(summary))
