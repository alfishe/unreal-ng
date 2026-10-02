#!/bin/sh
# Compare two eve-replay --dump directories picture by picture; print the frames that differ
# and how many bytes differ.
A=$1
B=$2
for f in "$A"/*.argb; do
    name=$(basename "$f")
    if [ ! -f "$B/$name" ]; then
        echo "$name: missing in $B"
        continue
    fi
    n=$(cmp -l "$f" "$B/$name" | wc -l | tr -d ' ')
    [ "$n" != "0" ] && echo "$name: $n bytes differ"
done
exit 0
