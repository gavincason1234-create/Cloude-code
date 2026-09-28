#!/usr/bin/env bash
# Stop whatever is listening on the test port (default 3100) — usually a `next start` left over
# from an earlier look at the site. Safe to run when nothing is there.
set -uo pipefail

PORT="${SITE_PORT:-3100}"

pids=""
if command -v fuser >/dev/null 2>&1; then
  pids="$(fuser "$PORT/tcp" 2>/dev/null | tr -s ' ' '\n' | sed '/^$/d')"
elif command -v lsof >/dev/null 2>&1; then
  pids="$(lsof -t -iTCP:"$PORT" -sTCP:LISTEN 2>/dev/null)"
else
  # No fuser/lsof: read the kernel's socket table. Port in hex, state 0A = LISTEN, column 10 = inode.
  hex="$(printf '%04X' "$PORT")"
  inodes="$(awk -v p=":$hex" '$2 ~ p"$" && $4 == "0A" { print $10 }' /proc/net/tcp /proc/net/tcp6 2>/dev/null)"
  for ino in $inodes; do
    for fd in /proc/[0-9]*/fd/*; do
      if [[ "$(readlink "$fd" 2>/dev/null)" == "socket:[$ino]" ]]; then
        p="${fd#/proc/}"
        pids="$pids ${p%%/*}"
      fi
    done
  done
fi

pids="$(echo "$pids" | tr -s ' \n' '\n' | sed '/^$/d' | sort -u)"
if [[ -z "$pids" ]]; then
  echo "stop-site: nothing is listening on port $PORT"
  exit 0
fi

for pid in $pids; do
  name="$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null | head -c 80)"
  echo "stop-site: stopping pid $pid ($name) on port $PORT"
  kill "$pid" 2>/dev/null || true
done

for _ in 1 2 3 4 5 6 7 8 9 10; do
  still=""
  for pid in $pids; do kill -0 "$pid" 2>/dev/null && still="$still $pid"; done
  [[ -z "$still" ]] && { echo "stop-site: port $PORT is free"; exit 0; }
  sleep 1
done
for pid in $still; do kill -9 "$pid" 2>/dev/null || true; done
echo "stop-site: port $PORT is free (had to force it)"
