# Authors, sources and modifications

## DWRip

Copyright (C) 2026 Didik DW and contributors.
Original DWRip code and documentation: GPL-3.0-only, except for the separately
identified ReInkPy-related material described in [LICENSE_SCOPE.md](LICENSE_SCOPE.md).

The public source snapshot was prepared on 2026-11-08. It omits the private
development history, vendor ICC profiles, reference PRNs, screenshots, user
presets, SDKs, prebuilt binaries and old icon assets. New SVG icons were
created for this snapshot under GPL-3.0-only.

## ReInkPy

Upstream project author: Daniel Gerber <dg@atufi.org>, as identified in the
upstream `pyproject.toml`. The upstream `epson.py` header identifies its license
as AGPL-3.0-or-later; the project includes the AGPLv3 license text. No additional
copyright year or ownership assertion is invented for upstream data.

Source: https://codeberg.org/atufi/reinkpy
Verified commit: `48f172762556fcab5cb9feec08465904cf5d51c9`.
The original Codeberg files were compared with the mirror at the same commit:
https://github.com/firehills/reinkpy/tree/48f172762556fcab5cb9feec08465904cf5d51c9

Material used: the L1800 model write-key parameter and EEPROM-write framing
reference. The complete model database and Python library are not bundled.
The extraction is in `src/l1800_reset_data.h`; framing and parameter use are
identified in `d4_write_eeprom()` in `src/d4.c`.

Changes on 2026-11-08: extracted the existing model key into an attributed C
header, clarified the write-key comment, and added license/source notices.
The command bytes, reply processing and reset behavior are unchanged.
See [the included AGPLv3 text](LICENSES/AGPL-3.0-or-later.txt).

## ReInk

Protocol reference: `lion-simba/reink`, commit
`df069125ce2afd110ee7bb5c2963318dca083102`.
The `reink.c` header states:
Copyright (C) 2008-2016 Alexey Osipov <public@alexey.osipov.name>.
License: GPL version 3 or, at the recipient's option, any later version.

The reference explains the complement/rotation bytes of EEPROM read/write
commands. No complete ReInk module or printer database is bundled here.
https://github.com/lion-simba/reink/blob/df069125ce2afd110ee7bb5c2963318dca083102/reink.c

## Gutenprint

Protocol reference: `src/escputil/d4lib.c`.
Copyright (C) 2001 Jean-Jacques Sarton <jj.sarton@t-online.de>.
License: GPL version 2 or, at the recipient's option, any later version.
Version 3 is the applicable compatible version for material incorporated in
this GPLv3 distribution. The full DWRip transport is not represented as an
unmodified copy of Gutenprint.

The D4 transport references its protocol layout; drop volumes reference
Gutenprint's ESC(e) 0x33 drop table. No Gutenprint binary or complete driver
is redistributed here.
https://github.com/echiu64/gutenprint/blob/master/src/escputil/d4lib.c
https://gimp-print.sourceforge.io/

## Other research references

`Ircama/epson_print_conf` (EUPL-1.2) and
`RxNaison/Epson-Waste-Reset` (Apache-2.0 at the project root) were research
references. Their complete code and databases are not bundled. The latter's
update script imports ReInkPy data, so this distribution does not rely on its
Apache label as a replacement license for ReInkPy material.

References are not a representation that every external project's license
has been imposed on independently authored DWRip code. The explicitly
incorporated ReInkPy-related material is handled as described above.
