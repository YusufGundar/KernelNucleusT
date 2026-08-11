# knst_window — Kullanım Kılavuzu (Türkçe)

**Çok kapsamlı windows ve linux için tam destekli bir pencere yönetim kütüphanesi olmaya aday bir kütüphanedir**

**Özellikler:**
- **Performans ve Boyut arasındaki en iyi dengeyi sunmayı hedefler**
- **Modern C++ özellikleri ile kullanıcıya temiz kod yazma imkanı sunar**
- **işetim sistemi event mantığına benzer bir şekilde eventleri yakalar ve işlersiniz**
- **Güvenlik açısından daha beta durumundadır**


**Kullanım ve Bilmeniz gerekenler**
  — Öncelikle kütüphaneyi başlatmak adına programın başında 'KnstWindowSources::Init()' çağrısının yapılması gereklidir bu kütüphane için tanımlanan static kaynakları hazırlar ve başlatır
  ayrıca bu çağrı kendi içinde işletim sistemine bağlı olan monitörlerin listelerinide çıkartır yani içinde 'knst_display::refresh_monitors()' da otomatik yapılır ve knst_display:: içinden gerekli bilgileri okumak için tekrar ::refresh_monitors() yapmanı gerek yoktur ancak yenileme işlemleri için yeniden kontrol etmek için yapaiblirsiniz.


**Event İşleme Hakkında**
  — Pencerenize gelen eventler , pencere nesnesinin içinde event yapısına dolar , bunu 'get_window_event_handle()' fonksiyonu ile const olarak alıp içindeki yapıları switch case ve if else yapıları ile kontrol edebilirsiniz

  — event type'ları
    KNST_KEYBOARD_EVENT	   ==    Klavye olayları
    KNST_MOUSE_EVENT	   ==    Fare olayları
    KNST_WINDOW_RESIZE	   ==    Pencere boyutu değişti
    KNST_WINDOW_MOVE	   ==    Pencere konumu değişti
    KNST_WINDOW_FULL_SCREEN	   ==    Tam ekran modu
    KNST_WINDOW_RESTORE	   ==    Pencere eski haline döndü
    KNST_WINDOW_MAXIMIZE	   ==    Pencere maksimize edildi
    KNST_WINDOW_MINIMIZE	   ==    Pencere minimize edildi
    KNST_FOCUS_IN / OUT	   ==    Pencere odağı değişti
    KNST_ENTER_NOTIFY / LEAVE_NOTIFY	   ==    Fare pencereye girdi/çıktı
    KNST_EXPOSE	   ==    Pencere yeniden çizilmeli
    KNST_FILE_DROP_... 	   ==    	Sürükle-bırak olayları
    KNST_CLOSE_WINDOW	   ==    Pencere kapatılıyor
    KNST_DISCONNECT	   ==    Bağlantı koptu
    KNST_UNKNOWN	   ==    Tanımlanmamış olay

  —örneğin w tuşu ile ilgili herhangi bi eventi

  if(window.get_window_event_handle().type == KNST_KEYBOARD_EVENT){
  
    if(window.get_window_event_handle().key_code == KNST_KEY_W){
      
      // eğer w tuşu ile ilgili PRESS RELEASE REPEAT gibi olaylar gelirse burası tetiklenir istersenizde event nesnesinin action kısmını kontrol ederek basma çekme veya tekrarmı olduğunu anlayabilirsiniz

    }

  }
  

**Döngüler**
    — 3 ana döngü çeşidi knst_window_event_system içerisinde mevcuttur
    — block_pool_event == sadece event geldiğinde tetiklenir
    — non_block_pool_event == event varsa alır yoksa hemen döner
    — timeout_pool_event == verdiğiniz süre kadar bekler event varsa alır yoksa hemen döner


**Fonksiyonlar**
  
  void creation() noexcept	Pencerenin native kaynaklarını (handle, surface vb.) oluşturur.

  __________________________________________________________________________________________
  void show() noexcept	Oluşturulan pencereyi görünür yapar.

  __________________________________________________________________________________________
  void creation_and_show() noexcept	creation() ve show()'u tek seferde çağırır.

  __________________________________________________________________________________________
  void destroy() noexcept	Pencereyi ve tüm kaynaklarını temizler.

  __________________________________________________________________________________________
  void should_close() noexcept	Pencereyi kapatılacak olarak işaretler.

  __________________________________________________________________________________________
  const bool& is_should_close() const noexcept	Pencerenin kapatılma durumunu döndürür.

  __________________________________________________________________________________________
  void set_title(const knst_c16string& title) noexcept	Pencere başlığını değiştirir.

  __________________________________________________________________________________________
  const knst_c16string& get_title() const noexcept	Mevcut pencere başlığını döndürür.

  __________________________________________________________________________________________
  void resize(int width = KNST_DEFAULT, int height = KNST_DEFAULT) noexcept	Pencere boyutunu değiştirir.

  __________________________________________________________________________________________
  void move(int root_x, int root_y) noexcept	Pencereyi belirtilen ekran koordinatlarına taşır.

  __________________________________________________________________________________________
  void move(int root_x, int root_y, const knst_monitor& monitor) noexcept	Pencereyi belirtilen monitöre göre taşır.

  __________________________________________________________________________________________
  void toggle_fullscreen(bool fullscreen) noexcept	Tam ekran modunu açar/kapatır.

  __________________________________________________________________________________________
  void set_minimized() noexcept	Pencereyi simge durumuna küçültür.

  __________________________________________________________________________________________
  void set_maximized() noexcept	Pencereyi ekranı kaplayacak şekilde büyütür.

  __________________________________________________________________________________________
  void restore() noexcept	Pencereyi önceki haline (normal) döndürür.

  __________________________________________________________________________________________
  void hide() noexcept	Pencereyi gizler (görünmez yapar).

  __________________________________________________________________________________________
  void focus() noexcept	Pencereye odaklanmaya çalışır.

  __________________________________________________________________________________________
  void set_opacity(float opacity) noexcept	Pencerenin saydamlığını ayarlar (0.0 - 1.0).

  __________________________________________________________________________________________
  const float& get_opacity() const noexcept	Mevcut saydamlık değerini döndürür.

  __________________________________________________________________________________________
  void set_minimum_size(int width = KNST_DEFAULT, int height = KNST_DEFAULT) noexcept	Pencere minimum boyutunu ayarlar.

  __________________________________________________________________________________________
  void set_maximum_size(int width = KNST_DEFAULT, int height = KNST_DEFAULT) noexcept	Pencere maksimum boyutunu ayarlar.

  __________________________________________________________________________________________
  void set_attribute(int attribute, bool value) noexcept	Pencere özelliklerini ayarlar.
  
  __________________________________________________________________________________________
  bool get_attribute(int attribute) const noexcept	Pencere özelliklerini sorgular.

  __________________________________________________________________________________________
  void set_title_bar_height(int height) noexcept	Özel başlık çubuğu yüksekliğini ayarlar.

  __________________________________________________________________________________________
  int get_title_bar_height() const noexcept	Başlık çubuğu yüksekliğini döndürür.

  __________________________________________________________________________________________
  void set_cursor(uint16_t cursor_type) noexcept	Fare imlecini değiştirir (örn. KNST_CURSOR_HAND).

  __________________________________________________________________________________________
  void set_bmp_cursor(const knst_byte_string& data, int width, int height, int hot_x = -1, int hot_y = -1) noexcept	Özel BMP verisiyle imleç oluşturur.

  __________________________________________________________________________________________
  void reset_cursor() noexcept	İmleci varsayılan haline döndürür.

  __________________________________________________________________________________________
  void set_cursor_mode(int mode) noexcept	İmleç modunu ayarlar (NORMAL, HIDDEN, DISABLED).

  __________________________________________________________________________________________
  void set_cursor_pos_on_window(int x = KNST_DEFAULT, int y = KNST_DEFAULT) noexcept	İmleci pencere içinde belirtilen konuma taşır.

  __________________________________________________________________________________________
  void set_cursor_pos_global(int root_x = KNST_DEFAULT, int root_y = KNST_DEFAULT) noexcept	İmleci ekran koordinatlarında belirtilen konuma taşır.

  __________________________________________________________________________________________
  void set_clipboard(const knst_c16string& text) noexcept	Panoya metin kopyalar.

  __________________________________________________________________________________________
  void request_clipboard() noexcept	Panodaki metni okur ve clipboard_text değişkenine kaydeder.
  
  __________________________________________________________________________________________
  const knst_c16string& get_clipboard() const noexcept	En son okunan pano metnini döndürür.

  __________________________________________________________________________________________
  void clear_clipboard() noexcept	Pano metnini temizler.

  __________________________________________________________________________________________
  void set_drag_drop_status(bool enabled) noexcept	Sürükle-bırak özelliğini açar/kapatır.

  __________________________________________________________________________________________
  void set_redraw_callback(void (*callback)(knst_window&, void*)) noexcept	Yeniden çizim geri çağrısını ayarlar (C fonksiyonu).

  __________________________________________________________________________________________
  void call_redraw_callback() noexcept	Ayarlanan geri çağrıyı manuel olarak tetikler.

  __________________________________________________________________________________________
  const knst_window_event& get_window_event_handle() const noexcept	Event yapısına const referans döndürür.

  __________________________________________________________________________________________
  void clear_temporary_events() noexcept	Geçici event'leri temizler (örn. KEY_REPEAT).

  __________________________________________________________________________________________
  void set_user_data(void* data) noexcept	Kullanıcı verisi ayarlar.

  __________________________________________________________________________________________
  const void* get_user_data() const noexcept	Ayarlanan kullanıcı verisini döndürür.

  __________________________________________________________________________________________



**Özel Makrolar**

Pencere Özellikleri Makroları



KNST_DEFAULT--------------------------------------------------------->Varsayılan değer (örn. -10000)
KNST_DISABLE_TITLE_BAR--------------------------------------------------------->Başlık çubuğunu kapatır, özel çizim aktif eder
KNST_DISABLE_REDRAW_ON_EVENT_MANAGER--------------------------------------------------------->Event yönetiminde otomatik yeniden çizimi kapatır
Pencere Nitelikleri (Attribute)


Pencere Özellikleri -- set_attribute ' a parametre olarak verilir

KNST_WINDOW_ATTRIB_DECORATED	
KNST_WINDOW_ATTRIB_RESIZABLE	
KNST_WINDOW_ATTRIB_ALWAYS_ON_TOP	
KNST_WINDOW_ATTRIB_TRANSPARENT	


İmleç Modları (Cursor Mode)



KNST_CURSOR_NORMAL--------------------------------------------------------->Normal imleç
KNST_CURSOR_HIDDEN--------------------------------------------------------->İmleç gizli
KNST_CURSOR_DISABLED--------------------------------------------------------->İmleç devre dışı (kilitli)

İmleç Tipleri (Cursor Type)


  
KNST_CURSOR_ARROW--------------------------------------------------------->Ok (varsayılan)
KNST_CURSOR_IBEAM--------------------------------------------------------->Metin imleci
KNST_CURSOR_CROSSHAIR--------------------------------------------------------->Artı işareti
KNST_CURSOR_HAND--------------------------------------------------------->El imleci (tıklanabilir)
KNST_CURSOR_HRESIZE--------------------------------------------------------->Yatay yeniden boyutlandırma
KNST_CURSOR_VRESIZE--------------------------------------------------------->Dikey yeniden boyutlandırma
KNST_CURSOR_MOVE--------------------------------------------------------->Taşıma imleci
KNST_CURSOR_WAIT--------------------------------------------------------->Bekleme imleci
KNST_CURSOR_HELP--------------------------------------------------------->Yardım imleci
KNST_CURSOR_NOT_ALLOWED--------------------------------------------------------->İzin verilmiyor

Klavye Tuşları



KNST_KEY_A ... KNST_KEY_Z--------------------------------------------------------->Alfabe tuşları
KNST_KEY_0 ... KNST_KEY_9--------------------------------------------------------->Sayı tuşları
KNST_KEY_F1 ... KNST_KEY_F12--------------------------------------------------------->F tuşları
KNST_KEY_ESCAPE--------------------------------------------------------->ESC tuşu
KNST_KEY_ENTER--------------------------------------------------------->Enter tuşu
KNST_KEY_SPACE--------------------------------------------------------->Space tuşu
KNST_KEY_BACKSPACE--------------------------------------------------------->Backspace tuşu
KNST_KEY_TAB--------------------------------------------------------->Tab tuşu
KNST_KEY_LEFT/RIGHT/UP/DOWN--------------------------------------------------------->Ok tuşları
KNST_KEY_HOME/END/PAGE_UP/PAGE_DOWN--------------------------------------------------------->Navigasyon tuşları
KNST_KEY_INSERT/DELETE--------------------------------------------------------->Ekle/Sil tuşları
KNST_KEY_SHIFT/CONTROL/ALT/SUPER--------------------------------------------------------->Modifier tuşlar
KNST_KEY_CAPS_LOCK/NUM_LOCK/SCROLL_LOCK--------------------------------------------------------->Kilit tuşları
KNST_KEY_VOLUME_*--------------------------------------------------------->Ses kontrol tuşları
KNST_KEY_MEDIA_*--------------------------------------------------------->Medya kontrol tuşları
KNST_KEY_BROWSER_*--------------------------------------------------------->Tarayıcı tuşları
KNST_KEY_C_CEDILLA--------------------------------------------------------->Ç (Türkçe)
KNST_KEY_G_BREVE--------------------------------------------------------->Ğ (Türkçe)
KNST_KEY_I_DOTLESS--------------------------------------------------------->ı (Türkçe)
KNST_KEY_O_DIAERESIS--------------------------------------------------------->Ö (Türkçe)
KNST_KEY_S_CEDILLA--------------------------------------------------------->Ş (Türkçe)
KNST_KEY_U_DIAERESIS--------------------------------------------------------->Ü (Türkçe)

Modifier Tuşlar (Mods)



KNST_MOD_SHIFT--------------------------------------------------------->Shift tuşu basılı
KNST_MOD_CONTROL--------------------------------------------------------->Ctrl tuşu basılı
KNST_MOD_ALT--------------------------------------------------------->Alt tuşu basılı
KNST_MOD_SUPER--------------------------------------------------------->Windows/Super tuşu basılı
KNST_MOD_CAPS_LOCK--------------------------------------------------------->Caps Lock aktif
KNST_MOD_NUM_LOCK--------------------------------------------------------->Num Lock aktif
