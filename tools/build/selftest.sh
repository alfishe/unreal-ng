#!/usr/bin/env bash
# selftest.sh - stress and failure tests for slot.py (via slot.sh). Uses a private slots
# directory and private pool names, so it is safe to run while real builds are going on.
#
#   tools/build/selftest.sh
#
# Exit status 0 = every check passed. Takes about a minute.

set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SLOT="$HERE/slot.sh"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/slot-selftest.XXXXXX")
export UNREAL_SLOTS_DIR="$WORK/slots"
export UNREAL_NICE=0
FAILS=0

trap 'rm -rf "$WORK"' EXIT

ok()    { echo "  ok   $*"; }
fail()  { echo "  FAIL $*"; FAILS=$((FAILS + 1)); }
check() { if [ "$2" -eq 0 ]; then ok "$1"; else fail "$1"; fi; }  # description, status
now()   { python3 -c 'import time; print(time.time())'; }
secs()  { python3 -c "print(round($(now) - $1, 1))"; }             # elapsed since $1
lt()    { python3 -c "import sys; sys.exit(0 if $1 < $2 else 1)"; } # a < b
gone()  { ! kill -0 "$1" 2>/dev/null; }
max_concurrent() { awk '/\+/{c++; if(c>m)m=c} /-/{c--} END{print m+0}' "$1"; }
busy()  { "$SLOT" --status | awk -v p="$1:" '$1==p{split($2,a,"/"); print a[1]; f=1} END{if(!f) print 0}'; }
# poll up to $2 s for a condition (a command string); echo seconds taken, status 0 when it became true
wait_for() { local t0; t0=$(now); while ! eval "$1"; do lt "$(secs "$t0")" "$2" || return 1; sleep 0.1; done; secs "$t0"; }

# job.sh SECONDS LOGFILE [PIDFILE]: logs +/-, records its pid
cat > "$WORK/job.sh" <<'EOF'
#!/bin/sh
[ -n "${3:-}" ] && echo $$ > "$3"
echo + >> "$2"
sleep "$1"
echo - >> "$2"
EOF
chmod +x "$WORK/job.sh"

echo "== 1. exit status and output pass through"
"$SLOT" -q p1 -- sh -c 'echo out; exit 7' > "$WORK/o1" 2>/dev/null; rc=$?
check "exit code 7 propagated" $([ $rc -eq 7 ] && echo 0 || echo 1)
check "stdout passed through" $([ "$(cat "$WORK/o1")" = out ] && echo 0 || echo 1)
check "slot free after exit" $([ "$(busy p1)" -eq 0 ] && echo 0 || echo 1)
"$SLOT" -q p1 -- /nonexistent/cmd >/dev/null 2>&1; rc=$?
check "missing command: rc=$rc (127), slot free" $([ $rc -eq 127 ] && [ "$(busy p1)" -eq 0 ] && echo 0 || echo 1)
"$SLOT" -q p1 -- sh -c 'kill -9 $$' >/dev/null 2>&1; rc=$?
check "child killed by signal 9 -> rc=$rc (137)" $([ $rc -eq 137 ] && echo 0 || echo 1)

echo "== 2. never more than 2 concurrent (12 jobs, pool of 2)"
: > "$WORK/log2"; T=$(now)
for i in $(seq 1 12); do UNREAL_SLOTS_P2=2 "$SLOT" -q p2 -- "$WORK/job.sh" 1 "$WORK/log2" & done
wait; E=$(secs "$T")
m=$(max_concurrent "$WORK/log2")
check "max concurrent = $m (want 2)" $([ "$m" -eq 2 ] && echo 0 || echo 1)
check "all 12 ran" $([ "$(grep -c + "$WORK/log2")" -eq 12 ] && echo 0 || echo 1)
check "elapsed ${E}s plausible (6..12)" $(lt 5.9 "$E" && lt "$E" 12 && echo 0 || echo 1)
check "no slots left" $([ "$(busy p2)" -eq 0 ] && echo 0 || echo 1)

echo "== 3. pool of 1 serializes (the test pool)"
: > "$WORK/log3"
for i in 1 2 3 4; do "$SLOT" -q p3 -- "$WORK/job.sh" 1 "$WORK/log3" & done
wait
m=$(max_concurrent "$WORK/log3")
check "max concurrent = $m (want 1)" $([ "$m" -eq 1 ] && echo 0 || echo 1)

echo "== 4. SIGTERM to the wrapper: command killed, slot free at once"
"$SLOT" -q p4 -- "$WORK/job.sh" 60 "$WORK/l4" "$WORK/pid4" & W=$!
wait_for '[ -s "$WORK/pid4" ]' 5 >/dev/null
kill -TERM $W; wait $W 2>/dev/null; rc=$?
check "wrapper rc=$rc (143)" $([ $rc -eq 143 ] && echo 0 || echo 1)
check "command gone" $(gone "$(cat "$WORK/pid4")" && echo 0 || echo 1)
check "slot free" $([ "$(busy p4)" -eq 0 ] && echo 0 || echo 1)

echo "== 4b. SIGINT (Ctrl-C) is forwarded to the command"
"$SLOT" -q p4 -- "$WORK/job.sh" 60 "$WORK/l4b" "$WORK/pid4b" & W=$!
wait_for '[ -s "$WORK/pid4b" ]' 5 >/dev/null
kill -INT $W; wait $W 2>/dev/null; rc=$?
check "wrapper rc=$rc (130), command gone, slot free" $([ $rc -eq 130 ] && gone "$(cat "$WORK/pid4b")" && [ "$(busy p4)" -eq 0 ] && echo 0 || echo 1)

echo "== 5. wrapper SIGKILLed: orphan killed, slot reusable within 5 s"
"$SLOT" -q p5 -- "$WORK/job.sh" 600 "$WORK/l5" "$WORK/pid5" & W=$!
wait_for '[ -s "$WORK/pid5" ]' 5 >/dev/null
kill -KILL $W; wait $W 2>/dev/null
T=$(now); "$SLOT" -q p5 -- true; rc=$?; E=$(secs "$T")
check "next caller ran (rc=$rc) after ${E}s (limit 5)" $([ $rc -eq 0 ] && lt "$E" 5 && echo 0 || echo 1)
check "orphaned command terminated" $(gone "$(cat "$WORK/pid5")" && echo 0 || echo 1)

echo "== 5b. same, but the command ignores SIGTERM (needs the KILL escalation)"
cat > "$WORK/stubborn.sh" <<'EOF'
#!/bin/sh
trap '' TERM
echo $$ > "$1"
while :; do sleep 1; done
EOF
chmod +x "$WORK/stubborn.sh"
"$SLOT" -q p5 -- "$WORK/stubborn.sh" "$WORK/pid5b" & W=$!
wait_for '[ -s "$WORK/pid5b" ]' 5 >/dev/null
kill -KILL $W; wait $W 2>/dev/null
T=$(now); "$SLOT" -q p5 -- true; rc=$?; E=$(secs "$T")
check "next caller ran after ${E}s (limit 5)" $([ $rc -eq 0 ] && lt "$E" 5 && echo 0 || echo 1)
check "stubborn orphan terminated" $(gone "$(cat "$WORK/pid5b")" && echo 0 || echo 1)

echo "== 6. wrapper and command both SIGKILLed"
"$SLOT" -q p6 -- "$WORK/job.sh" 600 "$WORK/l6" "$WORK/pid6" & W=$!
wait_for '[ -s "$WORK/pid6" ]' 5 >/dev/null
kill -KILL $W "$(cat "$WORK/pid6")"; wait $W 2>/dev/null
T=$(now); "$SLOT" -q p6 -- true; rc=$?; E=$(secs "$T")
check "next caller ran after ${E}s (limit 5)" $([ $rc -eq 0 ] && lt "$E" 5 && echo 0 || echo 1)

echo "== 7. a queued waiter killed while waiting leaves nothing behind"
"$SLOT" -q p7 -- "$WORK/job.sh" 6 "$WORK/l7" & H=$!
sleep 1
"$SLOT" -q p7 -- true & Q=$!
sleep 1
kill -TERM $Q; wait $Q 2>/dev/null; rc=$?
check "queued waiter rc=$rc (143)" $([ $rc -eq 143 ] && echo 0 || echo 1)
check "holder still holds exactly one slot" $([ "$(busy p7)" -eq 1 ] && echo 0 || echo 1)
wait $H
check "all free afterwards" $([ "$(busy p7)" -eq 0 ] && echo 0 || echo 1)

echo "== 8. wait timeout (-t)"
"$SLOT" -q p8 -- "$WORK/job.sh" 6 "$WORK/l8" & H=$!
sleep 1
T=$(now); "$SLOT" -q -t 2 p8 -- true 2>/dev/null; rc=$?; E=$(secs "$T")
check "rc=$rc (75) after ${E}s (2..4)" $([ $rc -eq 75 ] && lt 1.9 "$E" && lt "$E" 4 && echo 0 || echo 1)
kill -TERM $H; wait $H 2>/dev/null

echo "== 9. a leftover lock file from a dead run is not an owner"
mkdir -p "$UNREAL_SLOTS_DIR/p9"; echo "99999 1 old command" > "$UNREAL_SLOTS_DIR/p9/0.lock"
T=$(now); "$SLOT" -q p9 -- true; rc=$?; E=$(secs "$T")
check "ran at once (rc=$rc, ${E}s)" $([ $rc -eq 0 ] && lt "$E" 1 && echo 0 || echo 1)

echo "== 9b. a live owner is never taken over, however long it runs"
"$SLOT" -q p9 -- "$WORK/job.sh" 4 "$WORK/l9" & H=$!
sleep 1
T=$(now); "$SLOT" -q p9 -- true; E=$(secs "$T")
check "waiter blocked until the holder finished (${E}s, want >= 2)" $(lt 2 "$E" && echo 0 || echo 1)
wait $H

echo "== 9c. a background grandchild left behind does not keep the slot"
"$SLOT" -q p9 -- sh -c 'sleep 30 & echo $! > "'"$WORK"'/gc.pid"'
check "slot free right after the command returned" $([ "$(busy p9)" -eq 0 ] && echo 0 || echo 1)
kill "$(cat "$WORK/gc.pid")" 2>/dev/null

echo "== 10. chaos: 30 jobs, every 4th wrapper SIGKILLed, pool of 2"
: > "$WORK/log10"; PIDS=""
for i in $(seq 1 30); do UNREAL_SLOTS_P10=2 "$SLOT" -q p10 -- "$WORK/job.sh" 1 "$WORK/log10" & PIDS="$PIDS $!"; done
sleep 2
n=0; for p in $PIDS; do n=$((n + 1)); [ $((n % 4)) -eq 0 ] && kill -KILL $p 2>/dev/null; done
T=$(now); for p in $PIDS; do wait $p 2>/dev/null; done; E=$(secs "$T")
check "drained in ${E}s (limit 60)" $(lt "$E" 60 && echo 0 || echo 1)
t=$(wait_for '[ "$(busy p10)" -eq 0 ]' 5); check "no slots left (${t:-timeout}s)" $([ -n "$t" ] && echo 0 || echo 1)

echo "== 11. storm of short jobs: 80 x 0.2 s, pool of 2, x3 rounds"
for round in 1 2 3; do
  : > "$WORK/log11"
  for i in $(seq 1 80); do UNREAL_SLOTS_P11=2 "$SLOT" -q p11 -- "$WORK/job.sh" 0.2 "$WORK/log11" & done
  wait
  m=$(max_concurrent "$WORK/log11")
  check "round $round: max concurrent = $m (want <= 2), ran $(grep -c + "$WORK/log11")/80" $([ "$m" -le 2 ] && [ "$(grep -c + "$WORK/log11")" -eq 80 ] && echo 0 || echo 1)
done

echo
if [ $FAILS -eq 0 ]; then echo "ALL PASSED"; else echo "$FAILS FAILED"; fi
exit $FAILS
