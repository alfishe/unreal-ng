#!/bin/zsh
# Record a Time Profiler trace with xctrace and export its time-profile table to XML (macOS, Xcode command line
# tools). See README.md.
#
#   record.sh [-o out] [-t seconds] [-T template] [-e VAR=value ...] -- command [arguments]   launch a command
#   record.sh [-o out] [-t seconds] [-T template] -p pid                                     attach to a process
#
# Writes <out>.trace (open it in Instruments) and <out>.xml (the input of xctrace_profile.py). Default out:
# scratch/profile/<command name>-<time>. The launched command gets every UNREAL_* variable of this shell plus
# the -e ones (xctrace does not pass the environment on by itself).
set -eu

out=""
seconds=""
template="Time Profiler"
pid=""
envs=()
while getopts "o:t:T:p:e:h" opt; do
    case $opt in
        o) out=$OPTARG ;;
        t) seconds=$OPTARG ;;
        T) template=$OPTARG ;;
        p) pid=$OPTARG ;;
        e) envs+=(--env "$OPTARG") ;;
        h | *) sed -n '2,11p' "$0"; exit 1 ;;
    esac
done
shift $((OPTIND - 1))
[ "${1:-}" = "--" ] && shift
if [ -z "$pid" ] && [ $# -eq 0 ]; then
    sed -n '2,11p' "$0"
    exit 1
fi

if [ -z "$out" ]; then
    name=${pid:+pid$pid}
    name=${name:-$(basename "$1")}
    out=scratch/profile/$name-$(date +%Y%m%d-%H%M%S)
fi
mkdir -p "$(dirname "$out")"
rm -rf "$out.trace" "$out.xml"

args=(record --template "$template" --output "$out.trace")
[ -n "$seconds" ] && args+=(--time-limit "${seconds}s")
if [ -n "$pid" ]; then
    args+=(--attach "$pid")
else
    for var in ${(k)parameters[(I)UNREAL_*]}; do
        envs+=(--env "$var=${(P)var}")
    done
    args+=($envs --launch -- "$@")
fi

echo "recording to $out.trace"
xcrun xctrace "${args[@]}"
xcrun xctrace export --input "$out.trace" \
    --xpath '/trace-toc/run[@number="1"]/data/table[@schema="time-profile"]' --output "$out.xml"
echo "exported $out.xml"
