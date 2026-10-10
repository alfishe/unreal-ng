This directory (c:/nextzxos/cpm) contains the CP/M 3 system commands and
utilities. These are imported to the cpm-a.p3d drive image the first time that
it is used.

If you want to upgrade your existing CP/M drive image to the latest set of
files here, you can copy them yourself (using the Browser, IMPORT.COM or
BASIC's COPY command, for example). Alternatively, just delete the file CCP.COM
from your drive image. The next time you start CP/M, the files will be
automatically upgraded.

The c:/nextzxos/cpm/system directory contains system files used to start CP/M 3.

CP/M 3 is now fully Y2K compliant. This affects the programs DATE.COM, DIR.COM
and SHOW.COM. Dates can be displayed in US, UK or Year-Month-Day format. This
is set by SETDEF:
        SETDEF [US]
        SETDEF [UK]
        SETDEF [YMD]

The CP/M 3 license can be found in the c:/docs/licenses directory.
