#!/usr/bin/env python3
"""POC 036: probes of the CPU bus on top of POC 028's patched tree (package tbp; GHDL's external names fail at elaboration here).

    patch_probes.py <out dir of patch_rtl.py>
"""
import os
import sys

out = sys.argv[1]
PROBES = [("cpu_m1_n", "std_logic"), ("cpu_mreq_n", "std_logic"), ("cpu_iorq_n", "std_logic"), ("cpu_rd_n", "std_logic"),
          ("cpu_wr_n", "std_logic"), ("cpu_a", "std_logic_vector(15 downto 0)"), ("cpu_do", "std_logic_vector(7 downto 0)"),
          ("cpu_di", "std_logic_vector(7 downto 0)"), ("z80_int_n", "std_logic"), ("dma_holds_bus", "std_logic"),
          ("z80_wait_n", "std_logic")]
with open(os.path.join(out, "tbp.vhd"), "w") as f:
    f.write("library ieee;\nuse ieee.std_logic_1164.all;\npackage tbp is\n")
    for n, t in PROBES:
        f.write(f"   signal q_{n} : {t};\n")
    f.write("end package;\n")
p = os.path.join(out, "zxnext.vhd")
text = open(p).read()
old = "use work.tbh.all;"
assert text.count(old) == 1
text = text.replace(old, old + "\nuse work.tbp.all;", 1)
old = "   p_hc <= std_logic_vector(hc);\n"
assert text.count(old) == 1
text = text.replace(old, old + "".join(f"   q_{n} <= {n};\n" for n, _ in PROBES), 1)
open(p, "w").write(text)
files = os.path.join(out, "files.txt")
lines = open(files).read().split("\n")
lines.insert(1, os.path.join(out, "tbp.vhd"))
open(files, "w").write("\n".join(lines))
