
@echo off

..\..\tools\asw\asw -U -L main_ngs.a80
..\..\tools\asw\p2bin main_ngs.p neogs.rom -r $-$ -k
