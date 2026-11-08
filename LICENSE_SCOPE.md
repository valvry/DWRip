# License scope

This source distribution combines GPLv3 and AGPLv3 material under section 13
of both licenses. They are not offered as interchangeable license choices.

| Material | Applicable terms |
| --- | --- |
| Original DWRip engine, GUI, build scripts, documentation and newly created SVG icons | GPL-3.0-only; see LICENSE and the individual source headers. |
| ReInkPy L1800 write-key extraction in `src/l1800_reset_data.h` | AGPL-3.0-or-later, preserving the upstream project's terms for the extracted material. |
| ReInkPy-related EEPROM-write framing and use of those parameters in `d4_write_eeprom()` in `src/d4.c` | AGPL-3.0-or-later for that material; original DWRip portions remain GPL-3.0-only. The mixed file is identified by `GPL-3.0-only AND AGPL-3.0-or-later`. |
| Separately installed Qt, libpng, Little CMS and other dependencies | Their respective licenses; these libraries and their binaries are not redistributed in this source package. |

The explicit AGPL treatment of ReInkPy-related material avoids relying on a
conclusion that its individual hardware parameters are outside copyright.
It does not assert new exclusive rights over unprotected facts. Isolating a
parameter in a header does not remove the combined work's license obligations.

Full texts are included in [LICENSE](LICENSE) and
[LICENSES/AGPL-3.0-or-later.txt](LICENSES/AGPL-3.0-or-later.txt).
Upstream authors, sources, versions and references appear in [NOTICE.md](NOTICE.md).

## Redistributing the source

Retain the applicable copyright, license and modification notices, both
license texts, this scope explanation, and the preferred source/build files.
Do not describe the entire combined work as subject only to GPL obligations.

## Redistributing binaries or running a modified network service

Provide the corresponding source for the exact version distributed using a
method allowed by the applicable licenses. Include any necessary build and
installation material. The public source package includes the CLI/GUI source
and their Make/CMake build scripts; no prebuilt release is included.

For a modified version supporting remote user interaction over a computer
network, satisfy AGPL section 13's prominent offer of corresponding source to
those users, including incorporated GPLv3 work. Using a public source archive
does not excuse that offer when it applies. This desktop source release does
not add a network service.

References: [GPLv3 section 13](https://www.gnu.org/licenses/gpl-3.0.en.html#section13)
and [AGPLv3 section 13](https://www.gnu.org/licenses/agpl-3.0.en.html#section13).
