# Komponen dan referensi pihak ketiga

## Library yang dipakai saat build/runtime

| Komponen | Pemakaian | Rujukan lisensi |
| --- | --- | --- |
| Qt6 Core, Gui, Widgets, Svg | GUI; diinstal terpisah | https://doc.qt.io/qt-6.8/licensing.html |
| libpng | Pembacaan PNG pada CLI | https://github.com/libpng/libpng/blob/libpng16/LICENSE |
| Little CMS 2 | Konversi warna ICC pada CLI; MIT | https://littlecms.com/color-engine/ |
| zlib | Dependensi libpng | https://zlib.net/zlib_license.html |

Library dan SDK tidak disalin ke repository source ini. Periksa versi dan
lisensi aktual saat menyiapkan binary; Qt juga membawa komponen pihak ketiga.

## Referensi protokol

- Gutenprint `src/escputil/d4lib.c`: copyright (C) 2001 Jean-Jacques Sarton,
  GPL versi 2 atau versi berikutnya. Transport pada `src/d4.c` dan `src/d4.h`
  merujuk layout tersebut. Sumber:
  https://github.com/echiu64/gutenprint/blob/master/src/escputil/d4lib.c
- Volume drop dalam `src/halftone.c`/`.h` merujuk tabel Gutenprint untuk
  ESC(e) 0x33. Situs proyek: https://gimp-print.sourceforge.io/
- Format PRN dan perintah cleaning/status juga dikembangkan dari observasi
  output printer dan log perangkat. File output, log, screenshot, software
  vendor, dan profil warna vendor tidak disertakan di salinan ini.

## Status reset counter

Penelusuran versi historis mengonfirmasi ReInk asli GPL-3.0-or-later
(copyright 2008–2016 Alexey Osipov) dan ReInkPy AGPL-3.0-or-later
(proyek Daniel Gerber). Source serta database ReInkPy dari Codeberg
dicocokkan dengan mirror GitHub pada commit yang sama.

Epson-Waste-Reset memiliki lisensi Apache-2.0 pada proyeknya, tetapi skrip
pembaruan mengimpor database ReInkPy. Label itu saja bukan bukti bahwa seluruh
materi database asal telah diberi izin Apache.

Fungsi reset DWRip tetap disertakan. Materi parameter ReInkPy ditempatkan di
`src/l1800_reset_data.h` dengan AGPL-3.0-or-later; framing terkait pada
`d4_write_eeprom()` diidentifikasi dalam berkas campuran `src/d4.c`.
Kode asli DWRip tetap GPL-3.0-only. Teks kedua lisensi, atribusi, serta
kewajiban corresponding source disertakan. Lihat
[cakupan lisensi](LICENSE_SCOPE.md) dan [NOTICE](NOTICE.md).
Lihat [laporan lengkap](docs/RESET_LICENSE_REVIEW.md) untuk lingkup, bukti,
versi sumber, serta dasar penanganan lisensi dan corresponding source.

Rujukan:
https://github.com/lion-simba/reink/blob/df069125ce2afd110ee7bb5c2963318dca083102/reink.c
https://codeberg.org/atufi/reinkpy/src/commit/48f172762556fcab5cb9feec08465904cf5d51c9/reinkpy/epson.py
https://github.com/RxNaison/Epson-Waste-Reset/blob/54ec82a6c0e32197d5e1f7c7322ae3d3c312ac19/scripts/update_db.py

## Aset salinan ini

Ikon di `gui/resources/icons/` dibuat baru untuk kandidat publik pada
8 November 2026, memakai GPL-3.0-only seperti kode asli DWRip. Profil ICC,
sampel PRN, screenshot, preset pengguna, dan ikon lama tidak disertakan.
