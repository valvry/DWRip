# DWRip

DWRip adalah RIP untuk printer Epson L1800 di Linux, dengan CLI C11 dan GUI Qt6.
Proyek independen ini tidak berafiliasi dengan produsen printer atau software
RIP komersial yang disebut sebagai referensi interoperabilitas.

Salinan ini disiapkan untuk publikasi source dengan riwayat Git baru.
Fitur reset tetap disertakan; materi ReInkPy yang dipakai ditandai dan
ketentuan gabungannya dijelaskan di [cakupan lisensi](LICENSE_SCOPE.md).

## Lisensi

Kode dan dokumentasi asli serta ikon baru dalam salinan ini memakai GNU GPL
versi 3 saja (`GPL-3.0-only`); lihat [LICENSE](LICENSE). Materi ReInkPy yang
diidentifikasi dalam fitur reset mengikuti `AGPL-3.0-or-later`, dengan
[teks lengkap AGPLv3](LICENSES/AGPL-3.0-or-later.txt). Ini merupakan gabungan
menurut pasal 13 GPLv3/AGPLv3, bukan pilihan bebas antara kedua lisensi.
Copyright (C) 2026 Didik DW and contributors.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.

Lisensi komponen pihak ketiga tetap berlaku pada komponen masing-masing.
Lihat [cakupan lisensi](LICENSE_SCOPE.md), [atribusi dan perubahan](NOTICE.md),
serta [catatan komponen](THIRD_PARTY.md).

## Build

```sh
sudo apt install build-essential cmake libpng-dev liblcms2-dev
make
sh gui/setup-qt.sh
cmake -S gui -B gui/build
cmake --build gui/build -j2
./gui/build/dwrip-gui
```

GUI memakai Qt6 Widgets dan Svg. CLI juga bisa digunakan secara mandiri:

```sh
./dwrip --help
./dwrip -i desain.png -o hasil.prn -c YKWWMC -S 40x40
```

Pengujian tanpa printer:

```sh
./dwrip -t 568x227 -o uji.prn -c YKWWMC
```

Untuk GUI self-test tanpa display, gunakan Qt yang disiapkan di atas:

```sh
QT_DIR="$PWD/gui/.qt/6.8.3/gcc_64"
QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH="$QT_DIR/lib" \
  QT_PLUGIN_PATH="$QT_DIR/plugins" ./gui/build/dwrip-gui --self-test
```

## Profil warna dan materi uji

Salinan ini tidak menyertakan profil ICC vendor, PRN referensi, screenshot,
gambar uji lama, atau preset sesi pengguna. Pilih profil ICC yang Anda miliki
izin penggunaannya melalui Settings, atau lewat CLI `-I /path/profil-anda.icc`.
Tanpa `-I`, engine memakai konversi bawaan. Kualitas warna bergantung pada
profil, tinta, media, dan kalibrasi; tidak ada profil universal yang dijanjikan.

Ikon GUI pada salinan ini dibuat ulang dengan bentuk SVG sederhana dan tidak
mengambil aset dari bundel lama. Riwayat Git lama tidak disertakan.

## Distribusi binary

Kandidat ini menyiapkan source, bukan release AppImage. Jika membagikan binary,
sertakan lisensi/notice yang diperlukan dan corresponding source yang cocok,
termasuk source dependensi bila diwajibkan lisensinya. Memublikasikan source
DWRip saja tidak otomatis melengkapi kewajiban semua library yang dibundel.
