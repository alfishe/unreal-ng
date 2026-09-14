#!/usr/bin/env python3
"""Regenerate the cosim-oracle golden digests (golden/oracle.txt).

Builds the oracle harness from the PoC root (CMake, -DCOSIM=ON) if needed,
then runs `cosim-oracle --generate` from cosim/ (the golden path is relative
to the working directory).

Golden baselines must be conscious decisions. After regenerating:
  1. re-run the differential harness (cosim-ymfm) — it must stay 6/6;
  2. review `git diff golden/oracle.txt` — every changed digest must trace
     to an intentional engine change, never to a silent regression.
"""
import pathlib
import subprocess

COSIM = pathlib.Path(__file__).resolve().parent.parent
POC = COSIM.parent


def main() -> int:
    subprocess.run(
        ["cmake", "-S", str(POC), "-B", str(POC / "build"), "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", "-DCOSIM=ON"],
        check=True,
    )
    subprocess.run(["ninja", "-C", str(POC / "build"), "cosim-oracle"], check=True)
    subprocess.run([str(COSIM / "bin" / "cosim-oracle"), "--generate"],
                   check=True, cwd=COSIM)
    print()
    print("golden/oracle.txt regenerated. Before committing:")
    print("  1. cd cosim && ./bin/cosim-ymfm   # differential must stay 6/6")
    print("  2. ./bin/cosim-oracle             # self-oracle must stay 15/15")
    print("  3. git diff golden/oracle.txt     # review every changed digest")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
