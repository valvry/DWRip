# Publikasi source

Salinan ini disiapkan sebagai repository baru dengan satu commit awal.
Gunakan seluruh isi source ini beserta LICENSE, LICENSES/, LICENSE_SCOPE.md,
NOTICE.md dan THIRD_PARTY.md. Jangan mencampurkan riwayat repository privat.

## Verifikasi sebelum publikasi

- Build CLI dan GUI berhasil; self-test GUI serta ekspor PRN lulus.
- Binary CLI hasil build identik byte demi byte dengan build dari repo asal
  setelah pemindahan key ke header beratribusi; fungsi reset dipertahankan.
- GPLv3 dan AGPLv3 lengkap disertakan bersama cakupan dan atribusinya.
- Profil ICC, PRN, screenshot, preset pengguna, SDK, binary dan riwayat lama
  tidak masuk dalam source yang di-commit.
- Ikon SVG dibuat baru; contoh alamat printer menggunakan placeholder.

Tidak ada reset printer atau job cetak fisik yang dikirim selama pemeriksaan.
Tidak ada perubahan visibilitas atau upload GitHub yang dilakukan oleh langkah
persiapan lokal ini. Paket ini adalah source, bukan release binary/AppImage.

## Unggah ke repository baru

1. Buat repository publik **baru dan kosong** di akun GitHub Anda, misalnya
   bernama `DWRip`. Jangan memilih import/mirror atau mengubah repo lama.
2. Dari direktori repository baru ini, atur origin ke URL repository kosong
   tersebut, lalu push branch `master`. Gunakan autentikasi GitHub Anda.
3. Pertahankan repo pengembangan lama sebagai privat. Untuk pembaruan berikutnya,
   bawa hanya perubahan source yang sudah ditinjau ke repo publik.

Panduan GitHub: [Creating a new repository](https://docs.github.com/en/repositories/creating-and-managing-repositories/creating-a-new-repository).
