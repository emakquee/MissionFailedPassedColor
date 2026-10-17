# Mission Failed / Passed Color

Plugin AML untuk **GTA San Andreas Android 2.10 (arm64-v8a, 64 bit)**.
Mengubah warna teks **"MISSION FAILED!"** dan **"MISSION PASSED!"** (baris pertama saja).
Baris lain pada pesan yang sama ("RESPECT +", jumlah uang) dan elemen HUD lain
(Wasted/Busted, judul misi, subtitle, help text, radar, dsb.) tidak berubah.

Versi 1.2.0 — nama modul: `libMissionFailedColor64.so`

---

## 1. Struktur proyek

```
jni/
  Android.mk        build ndk-build (LOCAL_PATH = folder ini)
  Application.mk    arm64-v8a, android-24, c++_static
  main.cpp          seluruh logika plugin
  mod/              header & source AML (logger, config, amlmod, iaml)
  aml-psdk/         PSDK AML (struct/alamat GTA SA)
CMakeLists.txt      alternatif build dengan CMake
README.md           berkas ini
```

Build: `ndk-build` dari folder proyek (diuji pengguna dengan AML Compiler, NDK r23c).
Hasil `libMissionFailedColor64.so` ditaruh di folder mod AML.

---

## 2. Konfigurasi

File dibuat otomatis saat pertama kali jalan di folder `configs` AML
(`Android/data/com.rockstargames.gtasa/files/configs/`), nama file mengikuti GUID mod.

```ini
[MissionFailed]
Enabled = 1
R = 0
G = 200
B = 255
Labels = M_FAIL

[MissionPassed]
Enabled = 1
R = 0
G = 255
B = 0
Labels = M_PASS,M_PASSD,M_PASSR,M_PASSS

[Preferences]
Debug = 0
```

| Kunci | Arti |
|---|---|
| `Enabled` | 1 = warna diganti, 0 = pesan itu dibiarkan default |
| `R`, `G`, `B` | Warna 0–255 (nilai di luar rentang dipotong ke 0/255) |
| `Labels` | Daftar label GXT dipisah koma/spasi (maks. 8, tiap label maks. 15 karakter) yang dianggap pesan ini |
| `Debug` | 1 = tulis teks setiap *big message* di log AML beserta hasil cocok/tidak |

Alpha tidak bisa diatur: game memudarkan teks sendiri (fade in/out), jadi alpha dibiarkan.
Perubahan config baru berlaku setelah game dijalankan ulang. Kalau mengubah default di kode,
hapus file config lama agar dibuat ulang.

---

## 3. Bagaimana teks Mission Failed/Passed digambar di game

Semua temuan di bawah diverifikasi pada `libGTASA.so` 2.10 arm64 (simbol diekspor, ada debug info)
dan dicocokkan dengan source GTA Reversed serta decompile 2.10.

1. Script misi memanggil `PRINT_BIG` / `PRINT_WITH_NUMBER_BIG` dengan style 1.
   Opcode mengurangi 1, sehingga di kode game menjadi **style 0** (`STYLE_MIDDLE`).
2. `CMessages::AddBigMessage…` → `CHud::SetBigMessage` menyalin teks GXT ke
   `CHud::m_BigMessage[0]` (array `GxtChar`/`u16`, 128 karakter per style).
3. Tiap frame, `CHud::DrawSuccessFailedMessage()` (alamat 0x51E6C0 pada build yang dipakai)
   menggambar pesan itu: mengatur font Pricedown, edge, drop shadow, lalu warna dengan
   `CHudColours::GetRGB(HudColour, 7, alpha)` dan `CFont::SetColor`, kemudian `CFont::PrintString`.
   Slot warna **#7** didapat dari disassembly (`orr w1, wzr, #7` sebelum pemanggilan GetRGB).
4. Pada Android, `CFont::PrintString` tidak langsung menggambar. Teks dimasukkan ke buffer dan
   baru di-render di `CFont::RenderFontBuffer`. Di tahap render itu tag di dalam teks
   (`~r~`, `~w~`, `~y~`, …) diurai oleh `CFont::ParseToken` terhadap palet `HudColour` **pada saat itu**.

Dua konsekuensinya menentukan desain plugin:

- **Fungsi yang sama dan slot warna #7 dipakai juga oleh Mission Passed** (dan pesan style 0
  lainnya), jadi tidak cukup mengganti warna slot secara global. Teks harus diidentifikasi dulu.
- **Tag warna menimpa warna dasar.** Teks asli M_FAIL adalah `~r~MISSION FAILED!`, jadi tanpa
  penanganan khusus warna dari config tidak akan terlihat.

### Teks GXT yang relevan (American, dicocokkan lewat hash CRC32 yang di-invert)

| Label | Teks |
|---|---|
| `M_FAIL` | `~r~MISSION FAILED!` |
| `M_PASS` | `MISSION PASSED!~n~~w~$~1~` |
| `M_PASSD` | `MISSION PASSED!` |
| `M_PASSR` | `MISSION PASSED!~n~~w~RESPECT +` |
| `M_PASSS` | `MISSION PASSED!~n~~w~$~1~~n~~w~RESPECT +` |

`~n~` = baris baru, `~1~` = angka yang disisipkan game, `~w~`/`~r~` = tag warna.

---

## 4. Mekanisme plugin

### 4.1 Hook

Pada `OnModLoad`, plugin mencari simbol `_ZN4CHud24DrawSuccessFailedMessageEv`
(`CHud::DrawSuccessFailedMessage()`) lewat `aml->GetSym` dan memasang hook `aml->Hook`.
Kalau simbol tidak ditemukan, plugin mencatat error dan menonaktifkan diri (game tetap normal).

Hook adalah pembungkus; alurnya tiap frame:

```
hook()
 ├─ FindMatchingRule()        → pesan di layar cocok dengan Failed/Passed?
 │    tidak → panggil fungsi asli, selesai (nol perubahan)
 │    ya    → lanjut
 ├─ simpan HudColour[7]
 ├─ tulis RGB dari config ke HudColour[7]   (alpha tidak disentuh)
 ├─ panggil fungsi asli (menggambar pesan, font membaca warna slot 7)
 └─ kembalikan HudColour[7] ke nilai semula
```

Slot #7 hanya berubah selama fungsi itu berjalan, dan fungsi itu hanya menggambar pesan besar
tersebut. Elemen HUD lain digambar di fungsi lain di luar jendela itu, sehingga tidak terpengaruh.

### 4.2 Mengenali pesan (FindMatchingRule)

1. Ambil `CHud::m_BigMessage[0]`. Kosong → tidak ada pesan, keluar.
2. Buat salinan tanpa tag warna **baris pertama** (lihat 4.3).
3. Untuk tiap label di `Labels`: ambil teks aslinya dengan `TheText.Get(label)`, buat salinan
   dengan aturan yang sama, lalu potong di placeholder angka pertama (`~1~`…`~6~`).
   Pesan di layar dianggap cocok jika **diawali** teks hasil potongan itu
   (panjang minimal 3 karakter agar label kosong/pendek tidak cocok sembarang).
   - Pemotongan di placeholder perlu karena game mengganti `~1~` dengan angka sebenarnya
     (mis. `$500000`), sehingga pembandingan penuh akan gagal.
4. Failed dicek lebih dulu, lalu Passed.

Karena teks M_PASSD (`MISSION PASSED!`) adalah awalan semua varian Passed, semua varian tertangkap
walaupun label lain tidak terdaftar. Efek sampingnya, pesan lain yang berawalan sama
(mis. "MISSION PASSED! … SKILL +", "… NEW BEST TIME") ikut diwarnai.

Pencocokan memakai teks yang ditampilkan, bukan nama label dari script, jadi tidak bergantung
pada versi script/mod misi, dan bekerja pada bahasa lain selama teks GXT-nya diawali sama
dengan teks label bahasa yang sedang aktif (dibandingkan dengan `TheText.Get` bahasa aktif).

### 4.3 Menangani tag warna

Pada pesan yang cocok, **tag warna di baris pertama** (`~b~ ~g~ ~h~ ~l~ ~p~ ~r~ ~s~ ~w~ ~y~`)
dihapus dari salinan pesan di layar (`m_BigMessage[0]`, bukan tabel GXT). Dengan begitu baris
pertama memakai warna dasar, yaitu slot #7 yang sudah diganti plugin.

Penghapusan berhenti pada `~n~` pertama. Baris berikutnya ("RESPECT +", jumlah uang) tetap membawa
tag aslinya (`~w~`), jadi tampil dengan warna default game. `~n~`, tag tombol, dan placeholder
angka tidak pernah dihapus. Operasi ini idempoten: dijalankan tiap frame, hasilnya sama.

### 4.4 Membaca config

`LoadRule` memakai `cfg->Bind(...)` untuk tiap kunci (membuat kunci beserta nilai default bila belum
ada) lalu `cfg->Save()` menulis file. Default: Failed = cyan (0,200,255), Passed = hijau (0,255,0).
Warna default sengaja mencolok agar perubahan mudah terlihat; ubah sesuai selera.

---

## 5. Alamat & simbol yang dipakai

| Item | Simbol / alamat (2.10 arm64) |
|---|---|
| Fungsi yang di-hook | `_ZN4CHud24DrawSuccessFailedMessageEv` (0x51E6C0), diekspor di dynsym |
| Pengambil teks | `_ZN5CText3GetEPKc` (via PSDK `TheText.Get`) |
| `CHud::m_BigMessage` | slot GOT 0x84A148 (via PSDK) |
| `HudColour` | slot GOT 0x84C708 (via PSDK) |
| Slot warna pesan | indeks 7 di `HudColour` |

Hook memakai nama simbol, bukan offset tetap, jadi tahan terhadap perubahan alamat selama
simbol masih diekspor. Alamat GOT dari PSDK sudah dicocokkan dengan disassembly build di atas.

---

## 6. Keterbatasan dan catatan

- Hanya diuji untuk 2.10 arm64. Pada versi lain, simbol atau layout `m_BigMessage`/`HudColour`
  bisa berbeda; plugin akan menonaktifkan diri bila simbol hook tidak ada, tetapi tidak
  memverifikasi versi lain.
- Warna hanya satu untuk seluruh baris pertama. Efek bertingkat/gradasi tidak didukung.
- Hanya style 0 (pesan besar di tengah) yang ditangani. Pesan style lain (judul misi di kanan bawah,
  Wasted/Busted, dsb.) memakai fungsi lain dan tidak disentuh.
- Penghapusan tag warna mengubah salinan pesan di layar saja; tabel GXT dan teks lain di game tidak
  berubah.
- Biaya per frame kecil: pencocokan hanya berjalan saat ada pesan di `m_BigMessage[0]`
  (beberapa detik per misi selesai/gagal).
- Pemakaian: untuk game milik sendiri; modifikasi game mengikuti syarat layanan dan hukum setempat.

---

## 7. Menambah fitur serupa

Pola yang sama bisa dipakai ulang: cari fungsi `CHud::Draw…` yang menggambar elemen target,
temukan slot `HudColour` yang dipakainya lewat disassembly (argumen kedua `CHudColours::GetRGB`),
bungkus fungsi itu, ganti slot hanya saat teks/kondisi cocok, lalu kembalikan. Jika teksnya
mengandung tag warna, perhatikan bahwa rendering tertunda di `RenderFontBuffer`.
