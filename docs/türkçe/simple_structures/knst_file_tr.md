# knst_file — Cross-Platform Dosya Sistemi Kütüphanesi

Selamlar! 👋 Bu doküman `knst_file` kütüphanesinin ne olduğunu, nasıl kullanıldığını ve neden KernelNucleusT'de tercih edildiğini açıklar.

Kısaca: **Dosya ve dizin işlemlerinin tamamını tek bir API altında toplayan cross-platform bir dosya sistemi kütüphanesi.** Linux, Windows ve Android'de çalışır. Okuma, yazma, silme, kopyalama, taşıma, listeleme, izinler, zaman damgaları ve **200+ dosya uzantısı tanıma** gibi her şeyi içerir.

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Cross-platform** | Linux, Windows, Android'de aynı API |
| **UTF-8 her yerde** | POSIX'te UTF-8, Windows'ta UTF-16'ya otomatik çevrilir |
| **Atomic write** | Crash-safe dosya kaydetme (temp + rename) |
| **200+ uzantı tanıma** | `is_image()`, `is_video()`, `is_ai_model()` gibi 25+ kategori |
| **Binary-safe** | Gömülü `\0` byte'ları korunur |
| **Pool allocator** | `knst_pool_allocator` ile hızlı okuma/yazma |
| **Path utilities** | `join_path`, `normalize_path`, `get_parent_dir` |
| **Time formatting** | Absolute + relative ("2h ago") |
| **Windows reserved names** | `CON`, `PRN`, `AUX` otomatik kontrol |
| **Symlink desteği** | Windows'ta Developer Mode ile çalışır |

---

## 🚀 Hızlı Başlangıç

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // Dosya yaz
    knst_file::write_file_text(u"merhaba.txt", u"Merhaba Dünya!");

    // Oku
    knst_c16string content = knst_file::read_file_text(u"merhaba.txt");
    std::cout << content << "\n";   // "Merhaba Dünya!"

    // Metadata
    knst_file f = knst_file::open(u"merhaba.txt");
    std::cout << "Boyut: " << f.size << " bytes\n";
    std::cout << "Uzantı: " << f.extension << "\n";
    std::cout << "Değişim: " << f.get_modified_time_string() << "\n";

    // Sil
    knst_file::remove_file(u"merhaba.txt");
    return 0;
}
```

---

## 📋 Genel Bakış

| Kategori | Metodlar |
|---|---|
| **Path işlemleri** | `join_path`, `normalize_path`, `get_parent_dir`, `get_filename`, `get_extension`, `get_stem`, `is_absolute_path` |
| **Well-known dizinler** | `get_current_dir`, `get_home_dir`, `get_temp_dir`, `get_executable_dir` |
| **Varlık kontrolü** | `path_exists`, `file_exists`, `dir_exists`, `symlink_exists` |
| **Okuma** | `read_file_text`, `read_file_data`, `read_file_lines` |
| **Yazma** | `write_file_text`, `write_file_data`, `write_file_atomic`, `append_file_text` |
| **Dizin işlemleri** | `create_directory`, `create_directories`, `list_directory`, `list_directory_recursive` |
| **Kopyalama** | `copy_file`, `copy_directory`, `rename`, `move_path` |
| **Silme** | `remove_file`, `remove_directory`, `remove_all` |
| **Metadata** | `get_file_size`, `get_disk_space`, `set_file_times` |
| **Uzantı sınıflandırma** | `is_image`, `is_video`, `is_audio`, `is_code`, `is_ai_model`, ... (25+ kategori) |
| **Geçerlilik** | `is_valid_filename`, `sanitize_filename` |
| **Symlink** | `create_symlink`, `read_symlink_target` |

---

## 🛤️ 1) Path İşlemleri

Saf string operasyonları — dosya sistemine dokunmaz.

```cpp
knst_c16string p = u"/home/user/docs/report.tar.gz";

knst_file::get_filename(p);    // "report.tar.gz"
knst_file::get_directory(p);   // "/home/user/docs/"
knst_file::get_extension(p);   // ".gz"
knst_file::get_full_extension(p);  // ".tar.gz"
knst_file::get_stem(p);        // "report.tar"
knst_file::get_parent_dir(p);  // "/home/user"
```

### `join_path` — Güvenli Birleştirme

```cpp
auto p = knst_file::join_path(u"/home/user", u"docs/file.txt");
// "/home/user/docs/file.txt"

// Baştaki/trailing separator'lar otomatik düzeltilir
auto q = knst_file::join_path(u"/home/user/", u"/docs/file.txt");
// "/home/user/docs/file.txt"
```

### `normalize_path` — `..` ve `.` çözümü

```cpp
knst_c16string messy = u"/home/user/../user/./docs//file.txt";
std::cout << knst_file::normalize_path(messy);
// "/home/user/docs/file.txt"
```

### `is_absolute_path`

```cpp
knst_file::is_absolute_path(u"/etc/hosts");           // Linux: true
knst_file::is_absolute_path(u"C:\\Windows\\System32"); // Windows: true
knst_file::is_absolute_path(u"docs/file.txt");         // false
```

---

## 📁 2) Well-Known Dizinler

```cpp
knst_file::get_current_dir();     // "/home/knst/Desktop"
knst_file::get_home_dir();        // "/home/knst" (Win: "C:\Users\knst")
knst_file::get_temp_dir();        // "/tmp" (Win: "C:\Users\...\Temp")
knst_file::get_executable_dir();  // Uygulamanın bulunduğu dizin
```

**Not:** Windows'ta `get_executable_dir()` `GetModuleFileNameW` kullanır. Linux'ta `/proc/self/exe` symlink'i okunur.

---

## 📂 3) Varlık Kontrolü

```cpp
knst_file::path_exists(u"/etc/hosts");      // herhangi bir şey mi?
knst_file::file_exists(u"/etc/hosts");      // regular file mı?
knst_file::dir_exists(u"/etc");             // dizin mi?
knst_file::symlink_exists(u"/usr/lib/libc.so.6"); // symlink mi?
```

---

## 📖 4) Okuma

### `read_file_text` — UTF-8 Metin

```cpp
knst_c16string content = knst_file::read_file_text(u"data.txt");
std::cout << content << "\n";
```

**Özellikler:**
- UTF-8 → UTF-16 otomatik dönüşüm
- BOM (byte-order mark) otomatik atlanır
- Hata olursa boş string döner

### `read_file_data` — Ham Byte

```cpp
knst_byte_string data = knst_file::read_file_data<>(u"image.png");
std::cout << "Boyut: " << data.length() << " bytes\n";
```

**Binary-safe:** Gömülü `\0` byte'ları korunur.

### `read_file_lines` — Satır Satır

```cpp
knst_vector<knst_c16string> lines;
knst_file f = knst_file::open(u"data.txt");
f.read_file_lines(lines);

for (const auto& line : lines) {
    std::cout << line << "\n";
}
```

**Özellikler:**
- `\n` ve `\r\n` ikisini de tanır
- BOM otomatik atlanır
- Boş satırlar korunur

---

## ✍️ 5) Yazma

### `write_file_text` — UTF-16 → UTF-8

```cpp
knst_file::write_file_text(u"note.txt", u"Merhaba dünya 🌍");
```

### `write_file_data` — Ham Byte

```cpp
unsigned char blob[] = { 0x00, 0x11, 0x22, 0xFF };
knst_file::write_file_data(u"data.bin", blob, sizeof(blob));

// veya
knst_byte_string bytes = /* ... */;
knst_file::write_file_data(u"data.bin", bytes);
```

### `append_file_text` / `append_file_data`

```cpp
knst_file::append_file_text(u"log.txt", u"yeni satır\n");
knst_file::append_file_text(u"log.txt", u"senkron yaz\n", /*sync=*/true);
```

**`sync=true`** → `fsync()`/`FlushFileBuffers()` çağrılır (yavaş ama güvenli).

### 🌟 `write_file_atomic` — Crash-Safe Yazma

```cpp
knst_file::write_file_atomic_text(u"config.json", u"{ \"version\": 2 }");
```

**Nasıl çalışır?**
1. `config.json.knsttmp_12345_678` gibi geçici dosya oluşturulur
2. Veri yazılır + `fsync`
3. `rename()` ile asıl dosyaya taşınır (atomik)
4. POSIX'te ayrıca dizin fsync'lenir

**Neden önemli?** Yazma sırasında güç kesilirse eski dosya bozulmadan kalır.

---

## 📋 6) Dizin İşlemleri

### Oluşturma

```cpp
knst_file::create_directory(u"yeni_dizin");
knst_file::create_directories(u"a/b/c/d/e");   // recursive
```

### Listeleme

```cpp
knst_vector<knst_file> entries;
knst_file::list_directory(u"/home/user", entries);

for (const auto& e : entries) {
    std::cout << e.name 
              << "  (" << (e.is_directory ? "dir" : "file") << ")"
              << "  " << e.size << " bytes\n";
}
```

### Filtreli Listeleme

```cpp
knst_vector<knst_file> txt_files;
knst_file::list_directory(u"/home/user", txt_files,
    [](const knst_file& f) {
        return f.has_extension(u".txt");
    });
```

### Recursive Listeleme

```cpp
knst_vector<knst_file> all;
knst_file::list_directory_recursive(u"/home/user/project", all);

// Sadece derinlik 2'ye kadar
knst_file::list_directory_recursive(u"/home/user/project", all, 2);

// Symlink'leri takip et
knst_file::list_directory_recursive(u"/home/user/project", all, -1, true);
```

---

## 🗑️ 7) Silme

```cpp
knst_file::remove_file(u"dosya.txt");           // tek dosya
knst_file::remove_directory(u"bos_dizin");      // boş dizin
knst_file::remove_all(u"dizin");                // recursive (dolu olsa bile)
```

**`remove_all` güvenli midir?** Evet — önce tüm içeriği listeler, sonra ters sırayla (derin→sığ) siler.

---

## 📋 8) Kopyalama ve Taşıma

### Kopyalama

```cpp
// Tek dosya
knst_file::copy_file(u"kaynak.txt", u"hedef.txt", true);  // overwrite=true

// Dizin (recursive)
knst_file::copy_directory(u"src_dir", u"dst_dir", true);
```

**Not:** POSIX'te `copy_file` 64 KB chunk'larla okur — büyük dosyalar için bellek dostu.

### Yeniden Adlandırma

```cpp
knst_file::rename(u"eski.txt", u"yeni.txt");
```

### Taşıma

```cpp
knst_file::move_path(u"src.txt", u"dst.txt", true);
```

**Nasıl çalışır?**
1. Önce `rename()` denenir (hızlı)
2. Başarısız olursa (farklı disk) → kopyala + sil
3. Overwrite istenirse hedef önce silinir

---

## 🔗 9) Symlink

```cpp
// Oluştur
knst_file::create_symlink(u"/etc/hosts", u"hosts_link");

// Hedefi oku
knst_c16string target = knst_file::read_symlink_target(u"hosts_link");
std::cout << target << "\n";   // "/etc/hosts"

// Kontrol
knst_file f = knst_file::open(u"hosts_link");
if (f.is_symlink) {
    std::cout << "Bu bir symlink\n";
}
```

**Windows Notu:** Symlink oluşturmak **Developer Mode** veya **admin** gerektirir. Kütüphane otomatik olarak `SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE` bayrağını dener.

---

## 💾 10) Disk Alanı

```cpp
uint64_t free_bytes = 0, total_bytes = 0;
if (knst_file::get_disk_space(u"/home", free_bytes, total_bytes)) {
    std::cout << "Total: " << total_bytes / (1024ULL*1024*1024) << " GB\n";
    std::cout << "Free : " << free_bytes  / (1024ULL*1024*1024) << " GB\n";
}
```

**Platform farkı:**
- **Linux:** `statvfs()`
- **Windows:** `GetDiskFreeSpaceExW()`

---

## ⏰ 11) Zaman Damgaları

```cpp
knst_file f = knst_file::open(u"data.txt");

// Absolute — "2025-01-15 14:23:45"
f.get_created_time_string();
f.get_modified_time_string();
f.get_accessed_time_string();

// Relative — "2h ago", "3d ago", "just now"
f.get_created_time_relative();
f.get_modified_time_relative();
f.get_accessed_time_relative();
```

**Not:** POSIX'te `created_time` aslında `ctime` (change time) — gerçek birth-time değil. Windows'ta gerçek creation time.

### Zaman Ayarı

```cpp
uint64_t now = static_cast<uint64_t>(std::time(nullptr));
knst_file::set_file_times(u"data.txt", now /*modified*/, now /*accessed*/);
```

---

## 🗂️ 12) Uzantı Sınıflandırma (200+ Tanıma)

Bu, kütüphanenin **en güçlü yanı**. 200+ uzantıyı tanıyor ve 25+ kategoriye ayırıyor.

### Kullanım

```cpp
knst_file f = knst_file::open(u"photo.png");

if (f.is_image())   std::cout << "Resim\n";
if (f.is_video())   std::cout << "Video\n";
if (f.is_audio())   std::cout << "Ses\n";
if (f.is_code())    std::cout << "Kod\n";
// ...
```

### Tüm Kategoriler

| Kategori | Ne tanır? | Örnek uzantılar |
|---|---|---|
| **`is_image()`** | Resim formatları | `.png`, `.jpg`, `.webp`, `.svg`, `.dds`, `.ktx2` |
| **`is_video()`** | Video formatları | `.mp4`, `.mkv`, `.webm`, `.mov`, `.avi` |
| **`is_audio()`** | Ses formatları | `.mp3`, `.flac`, `.opus`, `.m4a`, `.wav` |
| **`is_text()`** | Basit metin | `.txt`, `.md`, `.log`, `.csv` |
| **`is_document()`** | Ofis dökümanı | `.docx`, `.pdf`, `.xlsx`, `.pptx` |
| **`is_code()`** | Kaynak kod | `.cpp`, `.py`, `.rs`, `.js`, `.go`, 100+ |
| **`is_script()`** | Script | `.sh`, `.bat`, `.ps1`, `.vbs` |
| **`is_shader()`** | Shader | `.glsl`, `.hlsl`, `.frag`, `.wgsl` |
| **`is_archive()`** | Arşiv | `.zip`, `.tar.gz`, `.7z`, `.apk` |
| **`is_executable_extension()`** | Çalıştırılabilir | `.exe`, `.so`, `.dll`, `.deb`, `.rpm` |
| **`is_font()`** | Font | `.ttf`, `.otf`, `.woff`, `.woff2` |
| **`is_model()`** | 3D model | `.obj`, `.fbx`, `.gltf`, `.stl` |
| **`is_web()`** | Web | `.html`, `.css`, `.vue`, `.svelte` |
| **`is_config()`** | Ayar | `.json`, `.yaml`, `.toml`, `.ini` |
| **`is_database()`** | Veritabanı | `.db`, `.sqlite`, `.mdb` |
| **`is_ebook()`** | E-kitap | `.epub`, `.mobi`, `.azw3`, `.cbz` |
| **`is_subtitle()`** | Altyazı | `.srt`, `.vtt`, `.ass`, `.ssa` |
| **`is_data()`** | Veri | `.parquet`, `.avro`, `.npy`, `.pkl` |
| **`is_build()`** | Build | `.cmake`, `.ninja`, `.gradle` |
| **`is_scientific()`** | Bilimsel | `.hdf5`, `.fits`, `.dcm`, `.fasta` |
| **`is_disk_image()`** | Disk imajı | `.iso`, `.img`, `.vhd`, `.qcow2` |
| **`is_ai_model()`** | AI model | `.pt`, `.onnx`, `.gguf`, `.safetensors` |
| **`is_game_asset()`** | Oyun asset | `.unity`, `.uasset`, `.pak`, `.bsp` |
| **`is_cad()`** | CAD | `.dwg`, `.dxf`, `.step`, `.sldprt` |

### Extension Kontrolü

```cpp
f.has_extension(u".txt");              // tek uzantı
f.has_extension(u".TXT", false);       // case-insensitive (varsayılan)
f.has_extension(u".TXT", true);        // case-sensitive
f.has_full_extension(u".tar.gz");      // çift uzantı
f.has_any_extension({u".txt", u".md"}); // birden fazla
```

**Performans:** Uzantı karşılaştırması **8 byte'a kadar bit-pack** ile O(1) yapılır. Yani 1000 dosyayı saniyede filtreleyebilirsin.

---

## ✅ 13) Dosya Adı Doğrulama

```cpp
knst_file::is_valid_filename(u"normal.txt");      // true
knst_file::is_valid_filename(u"inva/lid.txt");    // false (Linux: /, Win: /)
knst_file::is_valid_filename(u"with:colon.txt");  // false (Windows)
knst_file::is_valid_filename(u"CON");             // false (Windows reserved)
knst_file::is_valid_filename(u"file.");           // false (Windows trailing dot)
```

### Sanitization

```cpp
knst_c16string bad = u"my:file?name*.txt";
knst_c16string safe = knst_file::sanitize_filename(bad);
// "my_file_name_.txt"

// Replacement karakteri değiştirilebilir
knst_c16string safe2 = knst_file::sanitize_filename(bad, u'-');
// "my-file-name-.txt"
```

**Windows reserved names:** `CON`, `PRN`, `AUX`, `NUL`, `COM1-9`, `LPT1-9`.

---

## 🧠 14) Pool Allocator ile Hızlı Okuma

```cpp
// Varsayılan allocator (malloc/free)
auto data = knst_file::read_file_data<>(u"data.bin");

// Pool allocator — çok sayıda küçük okuma için hızlı
auto pool_data = knst_file::read_file_data<knst_pool_allocator>(u"data.bin");

// Text
knst_c16string text = knst_file::read_file_text<knst_pool_allocator>(u"data.txt");

// Lines
knst_vector<knst_c16string> lines;
knst_file f = knst_file::open(u"data.txt");
f.read_file_lines<knst_pool_allocator>(lines);
```

### Ne Zaman Pool Kullanmalı?

| Durum | Pool kullan? |
|---|---|
| Tek tük dosya okuma | ❌ Gereksiz |
| Binlerce küçük dosya okuma (log parser) | ✅ Evet, ciddi kazanç |
| Kısa string'ler (config, JSON key) | ✅ Evet |
| Büyük binary dosyalar (100 MB+) | ❌ malloc daha uygun |

---

## 📊 15) `knst_file` Sınıfı (Instance)

Statik metodların yanında bir de **instance API**'si var. Metadata'yı cache'ler.

### Açma ve Metadata

```cpp
knst_file f = knst_file::open(u"data.txt");

std::cout << f.path          << "\n";  // tam yol
std::cout << f.name          << "\n";  // "data.txt"
std::cout << f.extension     << "\n";  // ".txt"
std::cout << f.full_extension<< "\n";  // ".tar.gz" varsa
std::cout << f.parent_dir    << "\n";  // "/home/user/"

std::cout << f.size          << "\n";  // bytes
std::cout << f.get_size_string() << "\n"; // "1.5 MB"

std::cout << (f.exists        ? "var" : "yok") << "\n";
std::cout << (f.is_file       ? "dosya" : "") << "\n";
std::cout << (f.is_directory  ? "dizin" : "") << "\n";
std::cout << (f.is_symlink    ? "symlink" : "") << "\n";
std::cout << (f.is_hidden     ? "gizli" : "") << "\n";
std::cout << (f.is_readonly   ? "readonly" : "") << "\n";
std::cout << (f.is_executable ? "çalıştırılabilir" : "") << "\n";

std::cout << f.get_modified_time_string()   << "\n";
std::cout << f.get_modified_time_relative() << "\n";  // "2h ago"
```

### Instance Metodları

```cpp
knst_file f = knst_file::open(u"data.txt");

// Okuma
knst_c16string text = f.load();
knst_vector<knst_c16string> lines;
f.load_lines(lines);
knst_byte_string bytes = f.read_file_data<>();

// Yazma
f.save(u"yeni içerik");
f.save_atomic(u"güvenli yazma");
f.save_lines({u"satır1", u"satır2"});
f.write_data(bytes);
f.append_text(u"ek metin");
f.append_line(u"yeni satır");

// Kopyalama / Taşıma
f.copy_to(u"kopya.txt", true);
f.move_to(u"yeni_yer.txt", true);
f.rename_to(u"yeni_isim.txt");

// Silme
f.remove();         // tek dosya/dizin
f.remove_all();     // recursive

// Bilgi
f.refresh();        // metadata'yı yenile
f.reset();          // tüm bilgiyi sıfırla
```

### Fabrika Metodları

```cpp
// Aç veya oluştur
knst_file f1 = knst_file::open_or_create(u"yeni.txt");

// Direkt oluştur (içerikle)
knst_file f2 = knst_file::create_text(u"note.txt", u"merhaba");
knst_file f3 = knst_file::create_lines(u"list.txt", {u"a", u"b"});
knst_file f4 = knst_file::create_dir(u"yeni_dizin");
knst_file f5 = knst_file::create_dirs(u"a/b/c");
```

---

## 🛡️ 16) Hata Yönetimi

Her metod hata durumunda `false` veya boş döner. Ama daha detaylı hata bilgisi için:

```cpp
knst_file_error err;
auto data = knst_file::read_file_data<>(u"data.bin", &err);

if (err != knst_file_error::None) {
    std::cout << "Hata: " << knst_file_error_string(err) << "\n";
}
```

### Hata Enum'u

```cpp
enum class knst_file_error : uint8_t {
    None = 0,
    NotFound,          // "File not found"
    PermissionDenied,  // "Permission denied"
    IsDirectory,       // "Path is a directory"
    TooLarge,          // "File too large"
    OutOfMemory,       // "Out of memory"
    ReadError,         // "Read error"
    InvalidPath,       // "Invalid path"
    SymlinkLoop,       // "Symlink loop detected"
    NotAFile,          // "Not a regular file"
    AlreadyExists,     // "Already exists"
    NotEmpty,          // "Directory not empty"
    WriteError,        // "Write error"
    DiskFull,          // "Disk full"
    TempFileError,     // "Temporary file error"
    Unknown            // "Unknown error"
};
```

### Instance Error

```cpp
knst_file f = knst_file::open(u"olmayan.txt");
if (f.error != knst_file_error::None) {
    std::cout << f.error_string() << "\n";  // "File not found"
}
```

---

## 🌍 17) Platform Farkları

| Konu | Linux (POSIX) | Windows |
|---|---|---|
| **Path separator** | `/` | `\` (kabul: `/`) |
| **Absolute path** | `/etc/hosts` | `C:\...`, `\\server\share` |
| **Case sensitivity** | Case-sensitive (ext4) | Case-insensitive (NTFS) |
| **Hidden file** | `.` ile başlar | `FILE_ATTRIBUTE_HIDDEN` flag |
| **Executable bit** | `x` izni | Yok — `.exe` uzantısı |
| **Symlink** | Her zaman izinli | Developer Mode/admin gerekli |
| **Reserved names** | Yok | `CON`, `PRN`, `AUX`, `NUL`, `COM1-9`, `LPT1-9` |
| **Forbidden chars** | `/`, kontrol karakterleri | `<>:"/\|?*` |
| **Path uzunluğu** | `PATH_MAX` (4096) | `MAX_PATH` (260) — uzun yol için `\\?\` prefix |

### Uzun Yol Desteği (Windows)

```cpp
// Windows'ta 260 karakterden uzun yollar için:
knst_c16string long_path = u"\\\\?\\C:\\very\\long\\path\\...\\file.txt";
knst_file::write_file_text(long_path, u"data");
```

Kütüphane `\\?\` prefix'ini otomatik tanır.

---

## 🔥 18) Gerçek Kullanım Örnekleri

### Örnek 1: Basit Log Yazma

```cpp
void log_message(const knst_c16string& msg) {
    knst_c16string log_path = u"app.log";
    
    knst_c16string line = u"[";
    line.append(knst_file::open(log_path).get_modified_time_string());
    line.append(u"] ");
    line.append(msg);
    line.append(u"\n");
    
    knst_file::append_file_text(log_path, line, /*sync=*/true);
}
```

### Örnek 2: Config Dosyası Okuma

```cpp
struct Config {
    knst_c16string version;
    int max_threads = 4;
};

Config load_config(const knst_c16string& path) {
    Config cfg;
    
    knst_file f = knst_file::open(path);
    if (!f.exists) {
        return cfg;  // varsayılan
    }
    
    // Basit key=value parse
    knst_vector<knst_c16string> lines;
    f.load_lines(lines);
    
    for (const auto& line : lines) {
        if (line.starts_with(u"version=")) {
            cfg.version = line.substr(8, line.length() - 8);
        }
        // ...
    }
    return cfg;
}
```

### Örnek 3: Recursive Dizin Tarama

```cpp
// Projedeki tüm .cpp dosyalarını bul
knst_vector<knst_file> cpp_files;
knst_file::list_directory_recursive(u"/home/user/project", cpp_files,
    [](const knst_file& f) {
        return f.is_code() && f.has_extension(u".cpp");
    });

std::cout << "Toplam " << cpp_files.size() << " .cpp dosyası\n";

// Satır sayısını topla
uint64_t total_lines = 0;
for (const auto& f : cpp_files) {
    knst_vector<knst_c16string> lines;
    knst_file file = knst_file::open(f.path);
    file.load_lines(lines);
    total_lines += lines.size();
}
std::cout << "Toplam satır: " << total_lines << "\n";
```

### Örnek 4: Crash-Safe Config Kaydetme

```cpp
void save_config(const knst_c16string& path, const Config& cfg) {
    // JSON benzeri içerik oluştur
    knst_c16string content = u"{\n";
    content.append(u"  \"version\": \"");
    content.append(cfg.version);
    content.append(u"\",\n");
    content.append(u"  \"max_threads\": ");
    content.append(cfg.max_threads);
    content.append(u"\n}\n");

    // Atomic write — güç kesilse bile bozulmaz
    knst_file::write_file_atomic_text(path, content);
}
```

### Örnek 5: Dosya Tipi Analiz Aracı

```cpp
void analyze_directory(const knst_c16string& dir) {
    knst_vector<knst_file> all;
    knst_file::list_directory(dir, all);

    uint32_t images = 0, videos = 0, code = 0, audio = 0, other = 0;

    for (const auto& f : all) {
        if (f.is_directory) continue;
        
        if (f.is_image())        images++;
        else if (f.is_video())   videos++;
        else if (f.is_audio())   audio++;
        else if (f.is_code())    code++;
        else                     other++;
    }

    std::cout << "Resim  : " << images << "\n";
    std::cout << "Video  : " << videos << "\n";
    std::cout << "Ses    : " << audio << "\n";
    std::cout << "Kod    : " << code << "\n";
    std::cout << "Diğer  : " << other << "\n";
}
```

### Örnek 6: Güvenli Dosya Adı Temizleme

```cpp
knst_c16string user_input = u"kullanıcının girdiği://dosya*adı?.txt";
knst_c16string safe_name = knst_file::sanitize_filename(user_input);

if (!knst_file::is_valid_filename(safe_name)) {
    std::cerr << "Hala geçersiz!\n";
    return;
}

knst_c16string full_path = knst_file::join_path(u"/home/knst/files", safe_name);
knst_file::write_file_text(full_path, u"içerik");
```

---

## 📊 19) Performans Notları

| İşlem | Karmaşıklık | Not |
|---|---|---|
| `read_file_text` | O(n) | UTF-8 → UTF-16 dönüşümü |
| `read_file_data` | O(n) | 64 KB chunk'larla (büyük dosya dostu) |
| `write_file_atomic` | O(n) + fsync | Güvenli ama yavaş |
| `list_directory` | O(entries) | Sadece bir seviye |
| `list_directory_recursive` | O(total_entries) | Recursive |
| `has_extension` | **O(1)** | Bit-pack ile 8 byte'a kadar |
| `is_image`, `is_video`... | **O(1)** | Compile-time hash switch |
| `normalize_path` | O(n) | Tek geçiş |

### Önbellekleme İpucu

```cpp
// ❌ Yavaş: her seferinde disk okuma
for (int i = 0; i < 100; ++i) {
    auto content = knst_file::read_file_text(u"config.txt");
    // ...
}

// ✅ Hızlı: bir kez oku, bellekte tut
auto content = knst_file::read_file_text(u"config.txt");
for (int i = 0; i < 100; ++i) {
    // content'i kullan
}
```

### Pool Allocator İpucu

```cpp
// Yüzlerce küçük dosya okuyorsan pool kullan
for (int i = 0; i < 1000; ++i) {
    auto data = knst_file::read_file_data<knst_pool_allocator>(paths[i]);
    // ...
}
```

---

## 🎯 Size Getireceği Kazanç

`knst_file` şu durumlarda ciddi avantaj sağlar:

- ✅ **Cross-platform uygulama** — Windows + Linux aynı kod
- ✅ **Dosya tipi analizi** — 200+ uzantı tanıma hazır
- ✅ **Config dosyası yönetimi** — atomic write desteği
- ✅ **Log sistemi** — append + sync desteği
- ✅ **Dosya tarayıcı / organizer** — recursive listeleme + filtre
- ✅ **Backup aracı** — copy_directory + disk space
- ✅ **KernelNucleusT**'nin diğer modülleriyle uyum (`knst_c16string`, `knst_byte_string`)

