#!/bin/sh
# Wait (up to $1 seconds, default 600) until the 1-minute load average is at most $2
# (default 12); print the load at the end. Exit 0 when the load is low, 1 on timeout
# (the caller then measures anyway and records the load next to the figure).
LIMIT=${1:-600}
MAX=${2:-12}
waited=0
while :; do
    load=$(sysctl -n vm.loadavg | awk '{print $2}')
    if awk -v l="$load" -v m="$MAX" 'BEGIN{exit !(l <= m)}'; then
        echo "load $load"
        exit 0
    fi
    if [ "$waited" -ge "$LIMIT" ]; then
        echo "load $load (still above $MAX after ${waited}s)"
        exit 1
    fi
    sleep 15
    waited=$((waited + 15))
done
