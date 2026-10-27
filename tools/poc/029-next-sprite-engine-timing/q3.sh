#!/bin/bash
cd "$(dirname "$0")"
run() {
  desc=$1; shift
  r=$(./tb -gg_scn=2 "$@" 2>&1 | grep -E "STATUS|SAMPLE" | tail -2 | tr '\n' ' ')
  echo "Q3 [$desc] $r"
}
run "baseline over=1 xa10 xb15" -gg_xa=10 -gg_xb=15 -gg_over=1
run "over=0 default clip xa10 xb15" -gg_xa=10 -gg_xb=15 -gg_over=0
run "over=1 clip-en x 100..120(=200..241) sprites at 10/15" -gg_xa=10 -gg_xb=15 -gg_over=1 -gg_clipen=1 -gg_cx1=100 -gg_cx2=120
run "over=0 clip x1=100..120 (shifted 4*32+..) sprites 10/15" -gg_xa=10 -gg_xb=15 -gg_over=0 -gg_cx1=100 -gg_cx2=120
run "over=0 y=230 (lines>=224)" -gg_xa=10 -gg_xb=15 -gg_over=0 -gg_y=230
run "over=1 y=230" -gg_xa=10 -gg_xb=15 -gg_over=1 -gg_y=230
run "x>=320 both: xa330 xb335 over=1" -gg_xa=330 -gg_xb=335 -gg_over=1
run "xa312 xb320" -gg_xa=312 -gg_xb=320 -gg_over=1
run "xa300 xb318 (overlap 318..319 only inside)" -gg_xa=300 -gg_xb=318 -gg_over=1
run "wrap: xa500 xb505 (overlap in 505..511 invisible, wrapped 0..)" -gg_xa=500 -gg_xb=505 -gg_over=1
run "wrap: xa500 xb2 (overlap at 0..3 after wrap)" -gg_xa=500 -gg_xb=2 -gg_over=1
run "zero_on_top=1 baseline" -gg_xa=10 -gg_xb=15 -gg_over=1 -gg_zot=1
run "over=1 clipen y clip excludes lines (cy 0..10) y=100" -gg_xa=10 -gg_xb=15 -gg_over=1 -gg_clipen=1 -gg_cy1=0 -gg_cy2=10 -gg_y=100
