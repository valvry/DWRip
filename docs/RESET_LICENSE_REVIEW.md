# Penelusuran lisensi fitur reset waste counter

Tanggal: **8 November 2026**. Kode DWRip yang diperiksa: commit
`138884fa2577a6c0b9766d27105d75779c88aa37`, khususnya `src/d4.c`, `src/d4.h`,
blok `--waste-reset` di `src/main.c`, dan riwayat penambahan fitur.

## Hasil utama

**Fitur reset tidak harus dihapus hanya karena referensinya memakai AGPL.**
GPLv3 dan AGPLv3 mengizinkan penggabungan melalui pasal 13, dengan kewajiban
AGPL tetap dipertahankan untuk bagian dan gabungan yang tercakup. Ini berbeda
dengan menyatakan semua materi tersebut tersedia di bawah GPL saja.
Rujukan: [GPLv3 pasal 13](https://www.gnu.org/licenses/gpl-3.0.en.html#section13)
dan [AGPLv3 pasal 13](https://www.gnu.org/licenses/agpl-3.0.en.html#section13).

Ada temuan yang lebih spesifik daripada audit awal:

- ReInk asli menggunakan **GPL-3.0-or-later**, bukan AGPL.
- ReInkPy menggunakan **AGPL-3.0-or-later**, terkonfirmasi dari header source
  dan metadata proyek, bukan hanya label GitHub.
- Epson-Waste-Reset memiliki lisensi proyek Apache-2.0, tetapi skripnya
  mengimpor database dari ReInkPy. Label Apache tidak cukup untuk menyimpulkan
  bahwa semua hak atas database asal sudah tersedia di bawah Apache.
- DWRip tidak mengimpor library ReInkPy dan tidak menyertakan database
  lengkapnya. Jejak yang terlihat adalah parameter L1800, urutan paket, dan
  referensi implementasi. Pembedaan antara fakta perangkat dan ekspresi
  kode/data yang dilindungi masih penting; audit teknis ini tidak memutuskan
  pertanyaan hukum tersebut.

Pilihan GPL-3.0-only untuk kode asli DWRip tetap dipertahankan dan fungsi
reset tidak dihapus. Setelah penelusuran, penanganan gabungan GPL/AGPL telah
diterapkan pada kandidat source publik: parameter dipisahkan ke
`src/l1800_reset_data.h`, materi terkait dalam `src/d4.c` ditandai, teks AGPL,
NOTICE, dan cakupan lisensi disertakan. Repository belum diunggah.

## Sumber dan versi yang diperiksa

| Sumber | Versi pemeriksaan | Lisensi yang terverifikasi | Relevansi |
| --- | --- | --- | --- |
| Gutenprint `d4lib.c` | Header upstream yang dirujuk implementasi D4 | GPL-2.0-or-later; copyright 2001 Jean-Jacques Sarton | Transport IEEE-1284.4, bukan sumber key L1800. |
| ReInk, `lion-simba/reink` | `df069125ce2afd110ee7bb5c2963318dca083102`, 17 Januari 2016 | GPL-3.0-or-later; header copyright 2008–2016 Alexey Osipov | Rumus byte pendamping perintah baca/tulis EEPROM. |
| ReInkPy, `atufi/reinkpy` | `48f172762556fcab5cb9feec08465904cf5d51c9`, juga ada pada mirror `firehills/reinkpy` | AGPL-3.0-or-later; `epson.py` memiliki SPDX eksplisit dan `pyproject.toml` menyebut AGPL versi 3 atau berikutnya | Entri L1800 dan format payload penulisan. |
| Epson-Waste-Reset, `RxNaison/Epson-Waste-Reset` | `54ec82a6c0e32197d5e1f7c7322ae3d3c312ac19`, 26 Juli 2026 | Apache-2.0 pada LICENSE proyek; asal database terpisah perlu diperhatikan | Jejak menuju database ReInkPy. |
| `Ircama/epson_print_conf` | `0d591a1f817cae0c840c157bf629d67d262848e5` | EUPL-1.2; lampiran lisensi mencantumkan GPLv3 sebagai compatible licence | Referensi format perintah; catatan DWRip menyebut L1800 tidak ditemukan di database yang dipakai saat itu. |

Versi di atas adalah titik pemeriksaan yang dapat direproduksi, tersedia
sebelum implementasi DWRip pada 19 Agustus 2026. Riwayat lokal belum mencatat
hash upstream yang benar-benar dibuka pengembang saat itu; jangan menganggap
hash pemeriksaan ini sebagai bukti versi yang diambil saat pengembangan.

Sumber primer:

- [Gutenprint d4lib.c](https://github.com/echiu64/gutenprint/blob/master/src/escputil/d4lib.c).
- [Header ReInk reink.c](https://github.com/lion-simba/reink/blob/df069125ce2afd110ee7bb5c2963318dca083102/reink.c#L1-L16)
  dan [LICENSE](https://github.com/lion-simba/reink/blob/df069125ce2afd110ee7bb5c2963318dca083102/LICENSE).
- [ReInkPy source asli](https://codeberg.org/atufi/reinkpy/src/commit/48f172762556fcab5cb9feec08465904cf5d51c9/reinkpy/epson.py),
  [mirror pada commit yang sama](https://github.com/firehills/reinkpy/blob/48f172762556fcab5cb9feec08465904cf5d51c9/reinkpy/epson.py#L1),
  dan [metadata lisensi](https://github.com/firehills/reinkpy/blob/48f172762556fcab5cb9feec08465904cf5d51c9/pyproject.toml).
- [Epson-Waste-Reset LICENSE](https://github.com/RxNaison/Epson-Waste-Reset/blob/54ec82a6c0e32197d5e1f7c7322ae3d3c312ac19/LICENSE),
  [README credits](https://github.com/RxNaison/Epson-Waste-Reset/blob/54ec82a6c0e32197d5e1f7c7322ae3d3c312ac19/README.md#credits),
  dan [skrip impor database](https://github.com/RxNaison/Epson-Waste-Reset/blob/54ec82a6c0e32197d5e1f7c7322ae3d3c312ac19/scripts/update_db.py).
- [EUPL-1.2 pada epson_print_conf](https://github.com/Ircama/epson_print_conf/blob/0d591a1f817cae0c840c157bf629d67d262848e5/LICENSE),
  khususnya klausul kompatibilitas pasal 5 dan lampirannya.

## Verifikasi asal ReInkPy

Source asli Codeberg diperiksa langsung melalui API/raw HTTP. Pemeriksaan
mirror GitHub saja tidak menjadi dasar tunggal kesimpulan lisensinya.
Identitas berkas berikut dibandingkan pada commit yang sama:

| Berkas ReInkPy | Git blob yang dibandingkan |
| --- | --- |
| `reinkpy/epson.toml` | `f58a68636d9603aee9e52766414b667419774aa3` |
| `reinkpy/epson.py` | `313ba7c025a897cfdec8db1d4b51f4face2b9fb4` |
| `LICENSE` | `be3f7b28e564e7dd05eaf59d64adba1a4065ac0e` |

`epson.toml` tidak memiliki header lisensi tersendiri pada awal berkas dan
tidak ditemukan pemberian izin permisif terpisah di berkas itu. Karena itu,
jangan menganggap database tersebut public domain hanya karena berisi data
perangkat. Ini belum menyatakan bahwa setiap angka atau key individual
memenuhi syarat perlindungan hak cipta.

## Perbandingan dengan riwayat DWRip

### 1. Pembacaan counter dan transport sudah ada lebih dahulu

Commit `d4f3dfe43a2bc43e802258d1962aa76e56b83368`, 19 Agustus 2026 pukul
13:53 WIB, menambahkan transport D4 dan pembacaan counter. Pesan commit
menyebut Gutenprint sebagai referensi serta pencocokan pembacaan dengan log
WIC. Alamat counter dan read key sudah dicatat sebelum penambahan write key.

Ini merupakan bukti riwayat pengembangan yang tersedia, bukan verifikasi
ulang fisik printer dalam penelusuran lisensi ini.

### 2. Rumus perintah tulis dirujuk dari ReInk GPLv3+

Commit `5b499b11d5114e494058c89e5722f1ace66cfa2b`, pukul 14:27 WIB,
menambahkan jalur tulis yang belum berhasil melakukan reset. Pesannya
menyebut ReInk sebagai penjelas rumus byte perintah. Pada source ReInk,
`init_command()` membentuk komplemen byte dan rotasi bit; DWRip mengirim
hasilnya sebagai byte perintah tetap. Sumber ini memakai GPLv3 atau versi
berikutnya, sehingga versi 3 tersedia dalam pemberian izinnya.

### 3. Key model ditambahkan setelah melihat entri ReInkPy

Commit `a756de0ff311e0b92cf6d1c1c27c7d645e40284c`, pukul 14:52 WIB,
menyebut jejak ReInkPy melalui Epson-Waste-Reset secara eksplisit.
`src/d4.c` menambahkan satu key model delapan karakter dan menempatkannya
setelah alamat serta nilai dalam payload. Entri L1800 upstream memuat key
yang sama dan parameter perangkat yang cocok; lihat
[entri ReInkPy L1800](https://github.com/firehills/reinkpy/blob/48f172762556fcab5cb9feec08465904cf5d51c9/reinkpy/epson.toml#L1011-L1020).

Fungsi Python `write_eeprom()` mengemas alamat/nilai lalu key. Fungsi C DWRip
merakit buffer byte sendiri dan memakai transport D4 lokal. Jalur reset di
`src/main.c` melakukan read-back bertahap dan berhenti saat penulisan gagal.
Perbandingan tidak menunjukkan impor atau penyertaan keseluruhan modul
Python/database. Namun, perbedaan bahasa, bentuk buffer, atau tidak adanya
salinan seluruh file bukan bukti otomatis bahwa tidak ada adaptasi.

### 4. Lisensi Apache pada jalur perantara bukan kesimpulan akhir

`scripts/update_db.py` pada Epson-Waste-Reset membaca `epson.toml` dari
Codeberg dan menyalin parameter termasuk read key, write key, dan alamat ke
JSON. README-nya menyebut sinkronisasi database itu. Ada hubungan asal data
yang nyata, bukan hanya kemiripan nama proyek.

Apache-2.0 kompatibel untuk dimasukkan ke karya GPLv3 dengan memenuhi
persyaratannya; lihat [penjelasan resmi Apache](https://www.apache.org/licenses/GPL-compatibility.html).
Tetapi itu tidak membuktikan adanya izin Apache dari pemegang hak atas
seluruh materi asal ReInkPy. Penelusuran ini tidak menuduh proyek perantara
melanggar lisensi; kesimpulannya hanya bahwa label root-nya tidak cukup
untuk menyelesaikan provenance DWRip.

## Jalur publikasi dengan fitur reset tetap ada

Jalur yang paling konservatif tanpa membuang fitur adalah mempertahankan
GPL-3.0-only untuk kode asli DWRip, mengidentifikasi materi ReInkPy yang
dipakai, dan mempertahankan AGPL-3.0-or-later pada materi tersebut. GPLv3
pasal 13 memungkinkan gabungan ini. Jangan mengubahnya menjadi klaim bahwa
seluruh gabungan hanya memiliki syarat GPL tanpa ketentuan AGPL.

Penerapan yang diusulkan untuk ditinjau:

1. Pisahkan parameter model yang berasal dari ReInkPy ke komponen kecil yang
   jelas asalnya; dokumentasikan commit upstream dan materi yang diambil.
   Ini memudahkan penjelasan cakupan, bukan menghapus kewajiban lisensi.
2. Pertahankan pemberitahuan kontributor upstream yang relevan, salinan AGPLv3,
   dan teks GPLv3; nyatakan lisensi bagian dan ketentuan gabungan di README.
3. Sediakan corresponding source serta skrip build yang sesuai dengan versi
   yang didistribusikan. Untuk versi termodifikasi yang mendukung interaksi
   pengguna jarak jauh melalui jaringan, terapkan penawaran source menurut
   AGPL pasal 13. Jangan menyamakan kewajiban ini dengan kewajiban mengirim
   setiap perubahan pribadi ke pemilik upstream.
4. Selesaikan juga notice komponen GPL/EUPL/Apache lain sejauh materinya
   benar-benar digunakan. Jangan menyalin semua lisensi proyek referensi
   lalu menganggap audit otomatis selesai.

Jika pemilik ingin seluruh materi reset ditawarkan **hanya di bawah GPL**,
perlu dasar yang memadai bahwa bagian yang diambil hanyalah fakta perangkat
yang tidak tercakup hak eksklusif, atau penggantian materi yang dilindungi
dengan sumber yang haknya jelas. ReInk asli yang diperiksa tidak memuat key
L1800 tersebut; sekadar mengganti nama sumber menjadi ReInk tidak menyelesaikan
asal-usul key. Opsi lain adalah izin tambahan dari pemegang hak yang relevan.

**Penerapan pada kandidat publik:** fitur reset dipertahankan dan materi
ReInkPy ditangani di bawah AGPL-3.0-or-later, dengan kode asli DWRip tetap
GPL-3.0-only. `LICENSE_SCOPE.md` dan `NOTICE.md` menjelaskan bagian serta
ketentuan gabungannya; kedua teks lisensi dan source build disertakan.
Ini tidak menetapkan bahwa seluruh kode asli DWRip diganti menjadi AGPL.
Perubahan kode terbatas pada penandaan lisensi dan pemindahan key yang sama
ke header beratribusi; perilaku reset tidak dimaksudkan berubah.
