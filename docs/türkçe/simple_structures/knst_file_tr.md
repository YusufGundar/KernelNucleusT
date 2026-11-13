# knst_file — Cross-Platform Dosya Sistemi Kütüphanesi

Selamlar! 👋 Bu doküman `knst_file` kütüphanesinin ne olduğunu, nasıl kullanıldığını ve neden KernelNucleusT'de tercih edildiğini açıklar.

Kısaca: **Dosya ve dizin işlemlerinin tamamını tek bir API altında toplayan cross-platform bir dosya sistemi kütüphanesi.** Linux, Windows ve Android'de çalışır. Okuma, yazma, silme, kopyalama, taşıma, listeleme, izinler, zaman damgaları ve **200+ dosya uzantısı tanıma** gibi her şeyi içerir.

`using knst_file_system = knst_file;` — alias hazır.

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Cross-platform** | Linux, Windows, Android'de aynı API |
| **UTF-16 API + otomatik encoding** | Kullanıcı her zaman `knst_c16string` (UTF-16) kullanır. POSIX'te sistem çağrıları için UTF-8'e, Windows'ta `W`-fonksiyonları için UTF-16'ya otomatik çevrilir |
| **Atomic write** | Crash-safe dosya kaydetme (temp + rename + fsync) |
| **200+ uzantı tanıma** | `is_image()`, `is_video()`, `is_ai_model()` gibi 24 kategori |
| **Binary-safe** | Gömülü `\0` byte'ları korunur |
| **Pool allocator** | `read_file_data<knst_pool_allocator>` / `read_file_text<knst_pool_allocator>` ile hızlı okuma (yazma tarafında yok) |
| **Path utilities** | `join_path`, `normalize_path`, `resolve_path` |
| **Time formatting** | Absolute + relative ("2h ago") |
| **Windows reserved names** | `CON`, `PRN`, `AUX` otomatik kontrol |
| **Symlink desteği** | Windows'ta Developer Mode ile çalışır |
| **Recursion guard** | `KNST_MAX_SAFE_RECURSION_DEPTH = 4096` |
| **Instance API** | Metadata cache'leyen `knst_file` nesnesi |
| **Error enum** | `knst_file_error` + `knst_file_error_string()` |

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
| **Path işlemleri** | `join_path`, `normalize_path`, `resolve_path`, `get_parent_dir`, `get_filename`, `get_directory`, `get_extension`, `get_stem`, `is_absolute_path` |
| **Well-known dizinler** | `get_current_dir`, `set_current_dir`, `get_home_dir`, `get_temp_dir`, `get_executable_dir` |
| **Varlık kontrolü** | `path_exists`, `file_exists`, `dir_exists`, `symlink_exists` |
| **Okuma** | `read_file_text`, `read_file_data`, `read_file_lines` |
| **Yazma** | `write_file_text`, `write_file_data`, `write_file_atomic`, `append_file_text`, `append_file_data`, `touch_file`, `ensure_parent_dir` |
| **Dizin işlemleri** | `create_directory`, `create_directories`, `list_directory`, `list_directory_recursive` |
| **Kopyalama** | `copy_file`, `copy_directory`, `rename`, `move_path` |
| **Silme** | `remove_file`, `remove_directory`, `remove_all` |
| **Metadata** | `get_file_size`, `get_disk_space`, `set_file_times` |
| **Uzantı sınıflandırma** | `is_image`, `is_video`, `is_audio`, `is_code`, `is_ai_model`, ... (24 kategori) |
| **Geçerlilik** | `is_valid_filename`, `sanitize_filename` |
| **Symlink** | `create_symlink`, `read_symlink_target` |
| **Fabrika** | `open`, `open_or_create`, `create_file`, `create_text`, `create_lines`, `create_dir`, `create_dirs` |

---

## 🛤️ 1) Path İşlemleri

Saf string operasyonları — dosya sistemine dokunmaz.

```cpp
knst_c16string p = u"/home/user/docs/report.tar.gz";

knst_file::get_filename(p);    // "report.tar.gz"
knst_file::get_directory(p);   // "/home/user/docs/"
knst_file::get_extension(p);   // ".gz"
knst_file::get_stem(p);        // "report.tar"
knst_file::get_parent_dir(p);  // "/home/user/docs"
```

**Not:** `get_full_extension` **static metod değil** — `full_extension` sadece instance alanı olarak var:

> 💡 **`get_directory` vs `get_parent_dir` farkı:**
> - `get_directory(p)` → dosya adını atıp **dizinin kendisini** döner. Sonuçta **trailing `/` VAR**: `"/home/user/docs/"`
> - `get_parent_dir(p)` → path'in **üst kademesine çıkar**. Sonuçta **trailing `/` YOK**: `"/home/user/docs"`
> - Bir dosya yolu için ikisi de aynı yeri gösterir (sadece `/` farkı). Bir **dizin** yolu için farklı davranır: `get_parent_dir("/home/user/docs")` → `"/home/user"` (bir üst dizin).

```cpp
knst_file f = knst_file::open(u"/home/user/docs/report.tar.gz");
// f.full_extension  →  ".tar.gz"
// Yalnız .tar.*, .min.*, .user.*, .test.*, .d.ts için dolu; diğerlerinde boş.
```

### `join_path` — Güvenli Birleştirme

```cpp
auto p = knst_file::join_path(u"/home/user", u"docs/file.txt");
// POSIX:   "/home/user/docs/file.txt"
// Windows: "\\home\\user\\docs\\file.txt"  ← native separator

// Baştaki/trailing separator'lar otomatik düzeltilir
auto q = knst_file::join_path(u"/home/user/", u"/docs/file.txt");
// "/home/user/docs/file.txt"
```

**Önemli not:** `join_path` sadece birleştirir ve native formata çevirir. `.` veya `..` **çözmez** — bunun için `normalize_path` kullan.

### `normalize_path` — `..` ve `.` çözümü

```cpp
knst_c16string messy = u"/home/user/../user/./docs//file.txt";
std::cout << knst_file::normalize_path(messy);
// "/home/user/docs/file.txt"
```

### `resolve_path` — Absolute path'e çevir

```cpp
// CWD'ye göre çözer
knst_c16string abs = knst_file::resolve_path(u"data/config.txt");
// "/home/knst/Desktop/data/config.txt" gibi
```

### `is_absolute_path`

```cpp
knst_file::is_absolute_path(u"/etc/hosts");            // POSIX: true
knst_file::is_absolute_path(u"C:\\Windows\\System32"); // Windows: true
knst_file::is_absolute_path(u"docs/file.txt");         // false
knst_file::is_absolute_path(u"\\\\server\\share");     // Windows UNC: true
```

---

## 📁 2) Well-Known Dizinler

```cpp
knst_file::get_current_dir();     // "/home/knst/Desktop"
knst_file::get_home_dir();        // "/home/knst" (Win: "C:\Users\knst")
knst_file::get_temp_dir();        // "/tmp" (Win: "C:\Users\...\Temp")
knst_file::get_executable_dir();  // Uygulamanın bulunduğu dizin

// CWD değiştirme
bool ok = knst_file::set_current_dir(u"/home/knst/projects");
```

**Platform notları:**
- **Windows:** `get_executable_dir()` → `GetModuleFileNameW`, `get_home_dir()` → `USERPROFILE` (fallback `HOMEDRIVE` + `HOMEPATH`)
- **Linux:** `get_executable_dir()` → `/proc/self/exe` symlink'i okunur, `get_home_dir()` → `$HOME`, `get_temp_dir()` → `$TMPDIR` (yoksa `/tmp`)
- **Android:** `$HOME` ve `/tmp` yok. JNI üzerinden `knst_file::set_android_dirs(temp, home, external)` çağırman gerekir. `get_executable_dir()` Android'de boş döner.

---

## 📂 3) Varlık Kontrolü

```cpp
knst_file::path_exists(u"/etc/hosts");             // herhangi bir şey mi?
knst_file::file_exists(u"/etc/hosts");             // regular file mı?
knst_file::dir_exists(u"/etc");                    // dizin mi?
knst_file::symlink_exists(u"/usr/lib/libc.so.6");  // symlink mi?
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
- **BOM (byte-order mark) otomatik atlanır** (`EF BB BF`)
- Hata olursa boş string döner
- Template param: `read_file_text<Alloc>` ile iç UTF-8 byte buffer'ının allocator'ı seçilir

> ⚠️ **Önemli:** `read_file_text<Alloc>` **her zaman `knst_c16string` döner** (yani `basic_c16string<knst_default_allocator>`). `<Alloc>` sadece dosyayı okurken kullanılan **geçici UTF-8 buffer'ı** etkiler. Pool'lu bir string (`knst_c16string_sm<>`) almak için bu API yeterli değildir — istiyorsan dönen `knst_c16string`'i `bridge_memory()` ile pool'a taşı.
>
> **Asimetri:** `read_file_data<Alloc>` ise `basic_byte_string<Alloc>` döner — yani o taraf simetrik. Sadece `read_file_text` bu açıdan özel.

### `read_file_data` — Ham Byte

```cpp
knst_byte_string data = knst_file::read_file_data<>(u"image.png");
std::cout << "Boyut: " << data.length() << " bytes\n";

// Pool allocator ile — DİKKAT: return tipi basic_byte_string<knst_pool_allocator>
knst_byte_string_sm<> fast = knst_file::read_file_data<knst_pool_allocator>(u"image.png");
// (Burada `auto` yerine açık tip — read_file_data<Alloc> gerçekten Alloc'lu tip döner.
//  read_file_text<Alloc> ise her zaman knst_c16string döner — bkz. yukarıdaki uyarı.)
```

**Binary-safe:** Gömülü `\0` byte'ları korunur.

**Sınır:** `read_file_data` **4 GB (UINT32_MAX)** üstü dosyada `TooLarge` döner.

**Hata yakalama:**
```cpp
knst_file_error err;
auto data = knst_file::read_file_data<>(u"data.bin", &err);
if (err != knst_file_error::None) {
    std::cout << "Hata: " << knst_file_error_string(err) << "\n";
}
```

### `read_file_lines` — Satır Satır

```cpp
knst_vector<knst_c16string> lines;
knst_file f = knst_file::open(u"data.txt");
f.read_file_lines(lines);

for (const auto& line : lines) {
    std::cout << line << "\n";
}

// Pool ile
f.read_file_lines<knst_pool_allocator>(lines);
```

**Özellikler:**
- `\n` ve `\r\n` ikisini de tanır
- BOM otomatik atlanır
- Boş satırlar korunur
- Son satır `\n` ile bitmiyorsa da okunur---

## ✍️ 5) Yazma

### `write_file_text` — UTF-16 → UTF-8

```cpp
knst_file::write_file_text(u"note.txt", u"Merhaba dünya 🌍");
```

### `write_file_data` — Ham Byte

```cpp
unsigned char blob[] = { 0x00, 0x11, 0x22, 0xFF };
knst_file::write_file_data(u"data.bin", blob, sizeof(blob));

// veya byte_string üzerinden
knst_byte_string bytes = /* ... */;
knst_file::write_file_data(u"data.bin", bytes);
```

### `append_file_text` / `append_file_data`

```cpp
knst_file::append_file_text(u"log.txt", u"yeni satır\n");
knst_file::append_file_text(u"log.txt", u"senkron yaz\n", /*sync=*/true);
```

**`sync=true`** → `fsync()` / `FlushFileBuffers()` çağrılır (yavaş ama güvenli).

### `touch_file` — Boş dosya oluştur / dokun

```cpp
knst_file::touch_file(u"marker.txt");                    // parent'ları da oluşturur
knst_file::touch_file(u"marker.txt", /*create_parents=*/false);
```

### `ensure_parent_dir` — Parent dizinleri hazırla

```cpp
knst_file::ensure_parent_dir(u"a/b/c/file.txt");
// "a/b/c" yoksa recursive oluşturur
```

### 🌟 `write_file_atomic` — Crash-Safe Yazma

```cpp
// Metin versiyonu
knst_file::write_file_atomic_text(u"config.json", u"{ \"version\": 2 }");

// Byte versiyonu
knst_file::write_file_atomic(u"data.bin", blob, sizeof(blob));
```

**Nasıl çalışır?**
1. `config.json.knsttmp<unix>_<pid>_<counter>` gibi geçici dosya oluşturulur (örn: `config.json.knsttmp1731681234_5678_0` — `.knsttmp` ile `<unix>` arasında ayırıcı YOK)
2. Veri yazılır + `fsync`
3. `rename()` ile asıl dosyaya taşınır (atomik)
4. **POSIX'te ayrıca dizin `fsync`'lenir** (rename'in diske gerçekten inmesi için)

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

// Filtreli recursive
knst_file::list_directory_recursive(u"/home/user/project", all,
    [](const knst_file& f) { return f.is_code(); });
```

**Güvenlik:** `follow_symlinks=true` + `max_depth<0` iken `KNST_MAX_SAFE_RECURSION_DEPTH = 4096` seviyesinde durur (sonsuz döngü koruması).

---

## 🗑️ 7) Silme

```cpp
knst_file::remove_file(u"dosya.txt");           // tek dosya
knst_file::remove_directory(u"bos_dizin");      // boş dizin
knst_file::remove_all(u"dizin");                // recursive (dolu olsa bile)
```

**`remove_all` güvenli midir?** Evet — önce tüm içeriği listeler, sonra **ters sırayla (derin→sığ)** siler, en son kök dizini siler. Symlink'leri takip etmez (symlink'in kendisini siler, hedefi değil).

---

## 📋 8) Kopyalama, Yeniden Adlandırma, Taşıma

### Kopyalama

```cpp
// Tek dosya
knst_file::copy_file(u"kaynak.txt", u"hedef.txt", true);  // overwrite=true

// Dizin (recursive)
knst_file::copy_directory(u"src_dir", u"dst_dir",
                          true /*overwrite*/,
                          false /*follow_symlinks*/);
```

**Not:** POSIX'te `copy_file` **64 KB chunk**'larla okur — büyük dosyalar için bellek dostu. Dosya modu (`st_mode & 0777`) kopyalanır.

### Yeniden Adlandırma

```cpp
knst_file::rename(u"eski.txt", u"yeni.txt");
// Windows: MoveFileExW ile MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
```

### Taşıma

```cpp
knst_file::move_path(u"src.txt", u"dst.txt", true);
```

**Nasıl çalışır?**
1. Önce `rename()` denenir (hızlı — aynı dosya sistemi)
2. Başarısız olursa (farklı disk / cross-device) → **kopyala + sil**
3. Overwrite istenirse hedef önce `remove_all()` ile silinir

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

**Windows Notu:** Symlink oluşturmak **Developer Mode** veya **admin** gerektirir. Kütüphane önce `SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE` ile dener, `ERROR_INVALID_PARAMETER` alırsa flag'siz retry eder (admin gerekir). Windows'ta reparse point buffer'ı 16 KB'a kadar okunur.

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
- **Linux:** `statvfs()` → `f_bavail * f_frsize` ve `f_blocks * f_frsize`
- **Windows:** `GetDiskFreeSpaceExW()`

---

## ⏰ 11) Zaman Damgaları

```cpp
knst_file f = knst_file::open(u"data.txt");

// Absolute — "2025-01-15 14:23:45"
f.get_created_time_string();
f.get_modified_time_string();
f.get_accessed_time_string();

// UTC versiyonu
f.get_modified_time_string(false);  // local=false

// Relative — "2h ago", "3d ago", "just now"
f.get_created_time_relative();
f.get_modified_time_relative();
f.get_accessed_time_relative();
```

**Relative format:** `5s ago`, `3m ago`, `2h ago`, `4d ago`, `2mo ago`, `1y ago`. 5 saniyeden az → `"just now"`. Gelecekte ise `"from now"` suffix'i.

**Not:** POSIX'te `created_time` aslında `ctime` (change time) — gerçek birth-time değil. Windows'ta gerçek creation time (`ftCreationTime`).

### Zaman Ayarı

```cpp
uint64_t now = static_cast<uint64_t>(std::time(nullptr));
knst_file::set_file_times(u"data.txt", now /*modified*/, now /*accessed*/);
// accessed=0 verirsen modified ile aynı olur
```---

## 🗂️ 12) Uzantı Sınıflandırma (200+ Tanıma)

Bu, kütüphanenin **en güçlü yanı**. 200+ uzantıyı tanıyor ve **24 kategoriye** ayırıyor.

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
| **`is_image()`** | Resim formatları | `.png`, `.jpg`, `.jpeg`, `.bmp`, `.tga`, `.gif`, `.tiff`, `.webp`, `.heic`, `.heif`, `.avif`, `.jxl`, `.ico`, `.cur`, `.ppm`, `.pgm`, `.pbm`, `.pnm`, `.exr`, `.hdr`, `.dds`, `.ktx`, `.ktx2`, `.astc`, `.pvr`, `.basis`, `.qoi`, `.pcx`, `.svg`, `.svgz`, `.eps`, `.iff`, `.ilbm` |
| **`is_video()`** | Video formatları | `.mp4`, `.avi`, `.mkv`, `.mov`, `.wmv`, `.flv`, `.webm`, `.m4v`, `.mpg`, `.mpeg`, `.3gp`, `.3g2`, `.ogv`, `.ts`, `.m2ts`, `.mts`, `.vob`, `.rm`, `.rmvb`, `.asf`, `.divx`, `.xvid`, `.f4v`, `.mxf`, `.m2v`, `.yuv` |
| **`is_audio()`** | Ses formatları | `.mp3`, `.wav`, `.ogg`, `.flac`, `.aac`, `.m4a`, `.wma`, `.opus`, `.aiff`, `.aif`, `.alac`, `.ape`, `.mid`, `.midi`, `.amr`, `.ac3`, `.dts`, `.mka`, `.oga`, `.au`, `.snd`, `.m4b`, `.m4p`, `.m4r`, `.weba` |
| **`is_text()`** | Basit metin | `.txt`, `.text`, `.md`, `.log`, `.nfo`, `.csv`, `.tsv`, `.rtf`, `.tex`, `.latex` |
| **`is_document()`** | Ofis dökümanı | `.doc`, `.docx`, `.odt`, `.pdf`, `.pages`, `.xls`, `.xlsx`, `.ods`, `.ppt`, `.pptx`, `.odp`, `.key`, `.xps` |
| **`is_code()`** | Kaynak kod | `.c`, `.h`, `.cpp`, `.hpp`, `.cc`, `.cxx`, `.hxx`, `.c++`, `.h++`, `.inl`, `.ipp`, `.tpp`, `.tcc`, `.m`, `.mm`, `.cu`, `.cuh`, `.cs`, `.java`, `.kt`, `.kts`, `.scala`, `.sc`, `.py`, `.pyw`, `.pyx`, `.pyi`, `.rb`, `.erb`, `.php`, `.phtml`, `.pl`, `.pm`, `.lua`, `.wlua`, `.r`, `.js`, `.mjs`, `.cjs`, `.jsx`, `.ts`, `.tsx`, `.html`, `.htm`, `.xhtml`, `.shtml`, `.css`, `.scss`, `.sass`, `.less`, `.styl`, `.vue`, `.svelte`, `.astro`, `.rs`, `.go`, `.swift`, `.d`, `.nim`, `.zig`, `.v`, `.cr`, `.hs`, `.lhs`, `.erl`, `.hrl`, `.ex`, `.exs`, `.clj`, `.cljs`, `.ml`, `.mli`, `.fs`, `.fsi`, `.fsx`, `.lisp`, `.el`, `.scm`, `.rkt`, `.jl`, `.sol`, `.dart`, `.elm`, `.sql`, `.vim`, `.groovy`, `.gradle` (100+) |
| **`is_script()`** | Script | `.sh`, `.bash`, `.zsh`, `.fish`, `.ksh`, `.csh`, `.bat`, `.cmd`, `.ps1`, `.psm1`, `.psd1`, `.vbs`, `.vbe`, `.wsf`, `.au3`, `.ahk`, `.nsi`, `.nsh` |
| **`is_shader()`** | Shader | `.glsl`, `.vert`, `.frag`, `.geom`, `.tesc`, `.tese`, `.comp`, `.hlsl`, `.fx`, `.fxh`, `.cginc`, `.hlsli`, `.spv`, `.wgsl`, `.metal`, `.msl`, `.shader` |
| **`is_archive()`** | Arşiv | `.zip`, `.rar`, `.7z`, `.tar`, `.gz`, `.tgz`, `.bz2`, `.tbz2`, `.xz`, `.txz`, `.lz`, `.lzma`, `.lzo`, `.zst`, `.lz4`, `.z`, `.cab`, `.arj`, `.lzh`, `.ace`, `.zoo`, `.pak`, `.war`, `.ear`, `.jar`, `.apk`, `.ipa`, `.xap`, `.crx`, `.xpi` |
| **`is_executable_extension()`** | Çalıştırılabilir | `.exe`, `.dll`, `.so`, `.dylib`, `.a`, `.lib`, `.o`, `.bin`, `.elf`, `.com`, `.scr`, `.pif`, `.msi`, `.msix`, `.appx`, `.deb`, `.rpm`, `.pkg`, `.app`, `.run`, `.out` |
| **`is_font()`** | Font | `.ttf`, `.otf`, `.ttc`, `.otc`, `.woff`, `.woff2`, `.eot`, `.fon`, `.fnt`, `.pfb`, `.pfm`, `.afm`, `.bdf`, `.pcf` |
| **`is_model()`** | 3D model | `.obj`, `.mtl`, `.fbx`, `.gltf`, `.glb`, `.dae`, `.3ds`, `.max`, `.blend`, `.c4d`, `.ma`, `.mb`, `.lxo`, `.stl`, `.ply`, `.step`, `.stp`, `.iges`, `.igs`, `.dwg`, `.dxf`, `.3mf`, `.amf`, `.vrm`, `.pcd`, `.las`, `.laz`, `.e57`, `.pts`, `.xyz` |
| **`is_web()`** | Web | `.html`, `.htm`, `.xhtml`, `.shtml`, `.dhtml`, `.hta`, `.xml`, `.rss`, `.atom`, `.kml`, `.gml`, `.css`, `.scss`, `.sass`, `.less`, `.styl`, `.pcss`, `.js`, `.mjs`, `.cjs`, `.jsx`, `.ts`, `.tsx`, `.vue`, `.svelte`, `.astro`, `.pug`, `.jade`, `.ejs`, `.hbs`, `.haml`, `.twig`, `.njk` |
| **`is_config()`** | Ayar | `.json`, `.json5`, `.jsonc`, `.yaml`, `.yml`, `.toml`, `.xml`, `.plist`, `.ini`, `.cfg`, `.conf`, `.config`, `.env`, `.lock`, `.prop`, `.properties` |
| **`is_database()`** | Veritabanı | `.db`, `.sqlite`, `.db3`, `.mdb`, `.accdb`, `.dbf`, `.fdb`, `.gdb`, `.ibd`, `.myd`, `.myi`, `.bson`, `.sql` |
| **`is_ebook()`** | E-kitap | `.epub`, `.mobi`, `.azw`, `.azw3`, `.azw4`, `.fb2`, `.lit`, `.lrf`, `.prc`, `.cbr`, `.cbz`, `.cb7`, `.cbt`, `.cba` |
| **`is_subtitle()`** | Altyazı | `.srt`, `.vtt`, `.ass`, `.ssa`, `.sub`, `.idx`, `.sbv`, `.smi`, `.ttml`, `.usf`, `.stl` |
| **`is_data()`** | Veri | `.bin`, `.dat`, `.data`, `.dump`, `.raw`, `.parquet`, `.avro`, `.orc`, `.arrow`, `.feather`, `.proto`, `.npy`, `.npz`, `.pkl`, `.pickle`, `.joblib`, `.geojson`, `.topojson` |
| **`is_build()`** | Build | `.mk`, `.make`, `.cmake`, `.ninja`, `.bazel`, `.gradle`, `.sbt`, `.pom` |
| **`is_scientific()`** | Bilimsel | `.hdf`, `.hdf5`, `.h5`, `.nc`, `.netcdf`, `.cdf`, `.fits`, `.fit`, `.mat`, `.dcm`, `.dicom`, `.nii`, `.mha`, `.mhd`, `.nrrd`, `.fasta`, `.fa`, `.fastq`, `.fq`, `.sam`, `.bam`, `.vcf`, `.gff`, `.gtf`, `.bed`, `.pdb`, `.cif`, `.mol`, `.mol2`, `.sdf`, `.smi`, `.smiles` |
| **`is_disk_image()`** | Disk imajı | `.iso`, `.img`, `.vhd`, `.vhdx`, `.qcow`, `.qcow2`, `.vmdk`, `.vdi`, `.vbox`, `.dmg`, `.nrg`, `.mdf`, `.mds`, `.ccd`, `.toast`, `.cdr` |
| **`is_ai_model()`** | AI model | `.pt`, `.pth`, `.ckpt`, `.pb`, `.h5`, `.tflite`, `.onnx`, `.keras`, `.ggml`, `.gguf`, `.caffemodel`, `.pdparams`, `.safetensors`, `.mlmodel` |
| **`is_game_asset()`** | Oyun asset | `.unity`, `.prefab`, `.asset`, `.uasset`, `.umap`, `.upk`, `.pak`, `.utoc`, `.ucas`, `.tscn`, `.tres`, `.escn`, `.gd`, `.p3d`, `.wad`, `.bsp`, `.mdl`, `.vvd`, `.vtx`, `.anim` |
| **`is_cad()`** | CAD | `.dwg`, `.dxf`, `.dwf`, `.dwfx`, `.step`, `.stp`, `.iges`, `.igs`, `.prt`, `.asm`, `.sldprt`, `.sldasm` |

### Extension Kontrolü

```cpp
f.has_extension(u".txt");              // tek uzantı
f.has_extension(u".TXT", false);       // case-insensitive (varsayılan)
f.has_extension(u".TXT", true);        // case-sensitive
f.has_full_extension(u".tar.gz");      // çift uzantı
f.has_any_extension({u".txt", u".md"}); // birden fazla
```

**Performans:** Uzantı karşılaştırması **8 byte'a kadar bit-pack** ile **O(1)** yapılır. Yani 1000 dosyayı saniyede filtreleyebilirsin. Uzantı > 8 karakter ise string fallback devreye girer.

### Double Extension Notu

`full_extension` **sadece şu pattern'lerde** dolu olur:
- `.tar.*` → `.tar.gz`, `.tar.bz2`, `.tar.xz` vs.
- `.min.*` → `.min.js`, `.min.css`
- `.user.*` → `.user.js`
- `.test.*` → `.test.js`
- `.d.ts`

Diğer durumlarda `full_extension` boş kalır. `extension` ise her zaman son noktadan sonrasını verir.

---

## ✅ 13) Dosya Adı Doğrulama

```cpp
knst_file::is_valid_filename(u"normal.txt");      // true
knst_file::is_valid_filename(u"inva/lid.txt");    // false (Linux: /, Win: / ve \)
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

**Windows reserved names:** `CON`, `PRN`, `AUX`, `NUL`, `COM1-9`, `LPT1-9`. Bunlar stem olarak eşleşirse başına `_` eklenir.

**Windows'da ekstra:**
- Trailing `.` ve boşluklar temizlenir
- Kontrol karakterleri (`< 0x20`) ve `<>:"/\|?*` yasak

**POSIX'te:** Sadece `/` ve kontrol karakterleri yasak.

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
std::cout << f.get_size_string() << "\n"; // "1 MB" gibi

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
f.save(u"yeni içerik");              // text
f.save_atomic(u"güvenli yazma");     // atomic text
f.save_lines({u"satır1", u"satır2"});
f.write_text(u"içerik");
f.write_atomic_text(u"x");           // atomic text
f.write_atomic(bytes);               // atomic byte
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
f.error_string();   // son hata mesajı

// Path yardımcıları
f.absolute_path();  // resolve edilmiş absolute yol
f.is_absolute();    // absolute mi?
f.stem();           // uzantısız ad
f.resolved_parent();// parent dizin
f.valid_filename(); // name geçerli mi?

// Disk / zaman
f.disk_space(free_b, total_b);
f.set_times(now, now);

// Symlink
f.read_symlink_target();
f.create_symlink_here(target, /*as_directory=*/false);

// Dizin
f.create_directory();
f.create_directories();
f.ensure_parent_dir();
f.path_exists();
f.ensure_exists();

// Listeleme
f.list_directory(entries);
f.list_directory(entries, filter);
f.list_directory_recursive(all);
f.list_directory_recursive(all, filter);
```

### Fabrika Metodları

```cpp
// Aç veya oluştur
knst_file f1 = knst_file::open_or_create(u"yeni.txt");

// Direkt oluştur (içerikle)
knst_file f2 = knst_file::create_file(u"note.txt"); // open_or_create alias
knst_file f3 = knst_file::create_text(u"note.txt", u"merhaba");
knst_file f4 = knst_file::create_lines(u"list.txt", {u"a", u"b"});
knst_file f5 = knst_file::create_dir(u"yeni_dizin");
knst_file f6 = knst_file::create_dirs(u"a/b/c");
```

---

## 🛡️ 16) Hata Yönetimi

Her metod hata durumunda `false` veya boş döner. Detaylı hata bilgisi için:

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

Kütüphane `\\?\` prefix'ini otomatik tanır (`normalize_path` içinde erken döner).

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
    
    knst_vector<knst_c16string> lines;
    f.load_lines(lines);
    
    for (const auto& line : lines) {
        if (line.starts_with(u"version=")) {
            cfg.version = line.substr(8, line.length() - 8);
        }
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
    std::cout << "Ses    : " << audio  << "\n";
    std::cout << "Kod    : " << code   << "\n";
    std::cout << "Diğer  : " << other  << "\n";
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
| `read_file_data` | O(n) | Tek seferlik resize + read (4 GB üstü `TooLarge`) |
| `write_file_atomic` | O(n) + fsync | Güvenli ama yavaş |
| `list_directory` | O(entries) | Sadece bir seviye |
| `list_directory_recursive` | O(total_entries) | Recursive, guard 4096 |
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

- ✅ **Cross-platform uygulama** — Windows + Linux + Android aynı kod
- ✅ **Dosya tipi analizi** — 200+ uzantı tanıma hazır
- ✅ **Config dosyası yönetimi** — atomic write desteği
- ✅ **Log sistemi** — append + sync desteği
- ✅ **Dosya tarayıcı / organizer** — recursive listeleme + filtre + recursion guard
- ✅ **Backup aracı** — copy_directory + disk space
- ✅ **KernelNucleusT**'nin diğer modülleriyle uyum (`knst_c16string`, `knst_byte_string`, `knst_pool_allocator`)
- ✅ **UTF-8 / UTF-16 dönüşümü** — sen uğraşmıyorsun
- ✅ **Hata yönetimi** — `knst_file_error` enum'u ile net
- ✅ **Binary-safe** — gömülü `\0` kaybolmuyor
- ✅ **Instance API** — metadata cache ile tekrar disk okumadan