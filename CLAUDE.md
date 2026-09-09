# Pati

Bir çocuk için sesli arkadaş robotu. ESP32-S3 üzerinde çalışıyor, sesi
Gemini Live API'ye ham WebSocket ile gönderiyor ve cevabı hoparlöre
veriyor. Yerel model yok, aracı sunucu yok, telefon uygulaması yok.
Ayarlar robotun kendi yayınladığı web panelinden yapılıyor.

Kod yorumları ve belgeler **Türkçe**, README İngilizce. Yeni yazılan
yorumlar da Türkçe olmalı.

---

## 🔴 DONANIM — burada karışıklık pahalıya patlar

**Pati = M5Stack StickS3 (SKU K150).** Anahtarlık boyunda tek parça;
ekran, mikrofon, hoparlör ve pil içinde. **Lehim yok, montaj yok.**

Bundan önce Pati elle lehimlenmiş bir ESP32-S3 devkit'ti (INMP441 +
MAX98357A + 240×240 ekran, 13 kablo). **O donanım emekli.** Ekran modülü
hiç çalışmadı ve üçüncü kez ekran almak yerine her şeyi içinde olan bir
karta geçildi.

### Kurallar

1. **`main` yalnızca StickS3'tür.** Buraya devkit için kod, pin, belge
   ya da ölçü eklenmez.
2. **`devkit` dalı ve `v2.2.8-devkit` etiketi ARŞİVDİR.** Eski kaynak,
   montaj rehberi, kablo şeması ve STL'ler orada. **Oraya geliştirme
   yapılmaz** — sadece geçmişe bakmak için.
3. **Eski karta bu yazılım yüklenmez.** Kullanıcı isterse bile önce
   uyar: aynı yonga ailesi olduğu için yüklenir ve açılır, ama ses ve
   ekran çalışmaz.

Eski karta bakmak gerekirse:
```
git show v2.2.8-devkit:firmware/main/pati_pinler.h
git switch --detach v2.2.8-devkit
```

### Yanlış karta karşı yapısal koruma

Firmware açılışta ES8311'i I2C'den yokluyor. Bulamazsa kendini "sağlam"
işaretlemiyor ve yeniden başlıyor; önyükleyici eski sürüme geri dönüyor
(`app_main.cpp` → `donanimi_dogrula`). Yalnızca OTA ile gelen imaj için
— kabloyla yüklenende geri dönülecek bir şey yok, o yüzden orada
yeniden başlatmıyor (açılış döngüsü olurdu).

**Bu korumayı zayıflatma.** Güncelleme manifestinde donanım alanı yok;
tek engel bu.

---

## Kırmızı çizgi: Pati'nin sesi

Puck'ın ham sesi yetişkin bir erkek. Pati'nin afacan çocuk sesi
**tamamen çalma hızı çarpanından** geliyor (1.30×). Live API'de tizlik
ve hız ayarlanamıyor, başka yolu yok.

StickS3'te bu bir I2S saat ayarı **değil** — mikrofonla hoparlör aynı
yongada ve aynı saati paylaşıyor. Hat 48 kHz'de sabit; çarpan yazılımda,
yeniden örneklemeyle uygulanıyor (`pati_ornekleyici.hpp`).

Bu dosyaya dokunan her değişiklikten sonra `firmware/test/derle.bat`
çalıştırılmalı. Kullanıcının tek pazarlıksız şartı bu ses.

---

## Komutlar

ESP-IDF v5.5. **PowerShell** şart — `idf_tools.py` MSYS'i reddediyor,
Git Bash çalışmıyor.

```powershell
. "$env:IDF_PATH\export.ps1"
cd firmware
idf.py build
idf.py -p <PORT> flash monitor
```

Bir şey takılıyor, donuyor ya da kendiliğinden yeniden başlıyorsa:
**`firmware/TESHIS.md`** — belirti → sebep tablosu, sağlıklı sayılar,
gözlem yöntemleri ve daha önce yapılmış yanlış teşhisler.

**`firmware/BEDEN.md`** — gövde: kablo şeması, pin tablosu, devreye
alma sırası ve ölçülmesi bekleyen sayılar. Motora, servoya ya da
kumandaya dokunmadan önce oku.

**`firmware/PIL.md` — DEVAM EDEN İŞ.** Pati pilde hâlâ çöküyor
(konuşmaya başlarken brownout, 20-60 sn'de bir). Amaç, kullanıcının
feda etme sırası (ses 0.70'in altına İNMEZ), denenenler, denenmemiş
adaylar ve A/B ölçüm yöntemi orada. Pil tarafına dokunmadan önce oku.

Konak testleri (dördü de geçmeli):
```
cd firmware\test && derle.bat
```
1. Göz çizici ↔ tarayıcı, piksel piksel
2. Hafıza motoru ↔ Python prototipi
3. Yeniden örnekleyici — Pati'nin sesi
4. Beden matematiği — servo aralığı, sürüş karıştırması, hız tavanı

Üretilen dosyalar **elle düzenlenmez**:
`goz_uret.mjs` → `pati_goz_uretilmis.h`, `prompt_uret.py` →
`pati_kisilik_uretilmis.h`. İkisi de yazdığını geri okuyup doğruluyor.

---

## Yayınlama

`main`'e push = yayın. GitHub derliyor, Release açıyor, cihaz panelden
indiriyor. Elle yükleme yok.

`firmware/surum.txt`:
- 1. satır sürüm (CI son yayından +1 verir; dosyadaki büyükse onu alır)
- 2. satır **ebeveynin panelde okuduğu not — boş olamaz**, CI reddeder

---

## Sık düşülen tuzaklar

**`sdkconfig` zaten varsa `sdkconfig.defaults` OKUNMAZ.** Ve
`idf.py reconfigure` DE YETMİYOR — dosyayı silmek gerekiyor.
`CONFIG_IDF_TARGET` zaten bu yüzden defaults'ta duruyor, silmek güvenli.
İki kez yaşandı.

**`.bat` dosyaları CRLF olmalı.** LF olursa `cmd.exe` her satırın ilk
harfini yiyor ve testler sessizce atlanıyor. `.gitattributes` sabitliyor
ama düzenleyen araca dikkat.

**M5PM1'in L3B katmanı açılışta gelmiyor.** Mikrofon, hoparlör ve ekran
arka ışığı ondan besleniyor. Açılmazsa üçü de ölü ve her çağrı `ESP_OK`
dönüyor — tam bir yazılım hatası gibi görünür. `pati_guc.cpp` açıyor.

**M5Stack'in pin tablosunda `DIN`/`DOUT` kodeğin ağzından.** Kodeğin
`DOUT`'u ESP32'nin girişi.

**`esp_codec_dev` 8 bitlik I2C adresi bekliyor** (içeride `>> 1`
yapıyor). ES8311 için `0x30`, `0x18` değil.

**PSRAM 80 MHz sarsıntıya dayanmıyor.** Cihaz sallanınca veri yolu
kilitleniyor, `panic_enable_cache` donuyor ve donanım bekçisi kartı
sıfırlıyor — yığın izi basılamıyor çünkü CPU kod çalıştırmıyor.
40 MHz'de bitti (`TESHIS.md`). Flash hâlâ 80 MHz; aynı sınıftan belirti
çıkarsa sıradaki yer orası.

**Pilde ses seviyesi.** M5Stack yazıyor: yüksek seviyede çekilen akım
cihazı yeniden başlatıyor. Varsayılan 1.00 ve bilinçli düşük. Pilde
`SES_PIL_TAVANI` (0.70) devreye giriyor; karar VIN gerilimine bakılarak
veriliyor, "kablo takılı mı"ya değil (`pati_guc.cpp`, `guc_kaynak`).

**`CONFIG_MBEDTLS_DYNAMIC_BUFFER` "PSRAM'den al" demek DEĞİL.** Tamponu
serbest bırakıyor, nereden alındığını değiştirmiyor. PSRAM için
`MBEDTLS_EXTERNAL_MEM_ALLOC` gerekiyor. Yorum yıllarca "PSRAM'den
alınsın" diyordu ve alınmıyordu; dahili SRAM 1903 bayta kadar indi.

🔴 **"Pati duyuyor ama konuşmuyor" ilk bakılacak yer GEMINI KOTASI.**
Live API kotası **dakikalık** ve dolduğunda sunucu açık bir kota mesajı
yerine `1011 'Internal error occurred.'` dönebiliyor. Üstelik kendi
kendini besliyor: kota dolunca bağlantı kopuyor, Pati yeniden
bağlanıyor, her bağlanma kotadan yiyor. Cihaz tarafı bu sırada
tamamen sağlıklı görünür — mikrofon çalışır, transkript **doğru**
gelir, hata bile çıkmaz. İki eleme yolu yanlıştır: "kota dolsaydı
transkript de gelmezdi" (girdi transkripsiyonu ayrı çalışıyor) ve REST
ile `generateContent` denemek (**Live API'nin kotası ayrı havuz**).
Doğrusu bilgisayardan ham WebSocket ile sınamak — `TESHIS.md`.

🔴 **Sıcak döngüye I2C, kilit ya da NVS koyma.** 02.09.2026'da pilde
çökme aralığını 4,5 dakikadan **0,7 dakikaya** düşüren gerileme buydu:
göz görevi her karede (50 ms) kilit alan iki durum sorusu soruyordu,
tuş görevi 100 ms'de bir I2C okuyordu. İkisi de yazılırken "küçük bir
çağrı" görünüyordu. Bir çağrının ucuzluğu tek başına anlamlı değil —
**saniyede kaç kez yapıldığıyla çarpılmalı.** Çare: seyrek yokla,
sonucu sakla (`TESHIS.md`).

**Yığın hesabı görevin kodunu değil, çağırdığı en derin şeyi sayar.**
`pati_tus` görevine 2048 bayt verilmişti, gerekçesi de yazılıydı: "iki
GPIO okuması ve bir bayrak". Doğruydu — ta ki o bayrağın arkasına
`esp_deep_sleep_start()` girene kadar. Yığın taştı ve dışarıdan
görünüşü tam bir "kapanma" taklidiydi: ekran söndü, cihaz yeniden
başladı. Seri log olmadan ayırt edilemezdi (`TESHIS.md`).

**Seri portu açmak cihazı sıfırlıyor.** ESP32-S3'te `USB_UART_CHIP_RESET`
üretiliyor, DTR/RTS kapalı olsa bile. Yani "çalışan cihazı bozmadan
dinleyeyim" diye port açmak, tam da bozmak oluyor.

**Sürüm notu kabuğa gömülmemeli.** CI notu `${{ }}` ile doğrudan komut
satırına koyuyordu; nota çift tırnak yazılınca (`"şarja tak"`) tırnak
kapandı ve yayım koptu. Aynı tuzak tek tırnak, ters bölü ve `$(...)`
ile de kuruluyor. Not artık `env` üzerinden geçiyor — ebeveyne
gösterilecek serbest metin, noktalama düşünmeyi gerektirmemeli.

**USB'yi çekince seri kablo da gidiyor.** Pilde ne olduğu seri porttan
görülemiyor — tek pencere panelin `api/durum` → `guc` alanı.

---

## Tuşlar

Çocuğun kullandığı iki düğme. İkisi de **tek hareket**, çünkü çift tık
bir çocuğun tutturabileceği bir şey değil ve yan düğme sert.

| | Kısa | Uzun (1,2 sn) |
|---|---|---|
| **Mavi tuş** (ekranın sağı) | **sayfayı ilerletir**: gözler → bilgi → panel QR → gözler | derin uyku |
| **Yan güç düğmesi** | **tamamen kapat / aç** | — |

İki sayfa **iki ayrı kişi için**: bilgi sayfası çocuk için (pil, wifi
adı, sinyal), panel QR ebeveyn için (paneli açmanın yolu). Süreleri de
farklı — bilgi 15 sn, QR 45 sn, çünkü QR ebeveynin telefonu çıkarıp
kamerayı açmasını bekliyor.

Bilgi sayfası 15 saniyede kendiliğinden kapanıyor; çocuk unutursa Pati
yüzsüz kalmasın diye.

**Mavi tuş ekranda ne yazıyorsa onu yapar.** Güncelleme sayfası
açıkken kısa basış bilgi sayfasını değil **güncellemeyi** başlatıyor.
Aynı tuşun ekrandaki yazıyla çelişmesi, çocuğa tuşun bozuk olduğunu
düşündürürdü.

### Gözlerin yerine geçen perdeler

Hepsi göz görevinden çiziliyor — şerit tamponları ve SPI onun malı,
başka görevden çizmek ekranı bozar (`pati_perde.hpp`).

| Perde | Ne zaman | Nasıl kapanır |
|---|---|---|
| Düşük pil | %20 altı, dakikada bir | 3 saniye sonra |
| Bilgi | mavi tuş | tuş, uyarı, ya da 15 sn |
| **WiFi aranıyor** | ağ yokken (kurulum kipi hariç) | ağ gelince |
| **Kurulum** | Pati kendi ağını yayınlarken | ağa bağlanınca |
| **Panel QR** | mavi tuşun ikinci basışı | tuş, uyarı, ya da 45 sn |
| **Güncelleme** | açılışta yeni sürüm varsa | tuş, ya da 20 sn |

🔴 **"AP'im açık mı" ile "durumum Kurulum mu" AYRI İKİ SORU.**
09.09.2026'da gerçek cihazda öğrenildi. Kurulum kipinde çip **APSTA**'da:
AP telefon için, STA tarama için — ve STA tarafı bir IP alırsa
`IP_EVENT_STA_GOT_IP` gelip `g_durum` sessizce `Bagli` oluyor, **AP hâlâ
ayaktayken.**

Belirtisi yanıltıcıydı ve **üç yerde birden** çıktı:

| Nerede | Ne oldu |
|---|---|
| Captive portal 302'si | telefon bağlanıyor, sayfa kendiliğinden açılmıyor — *"panel bazen çıkmıyor"* |
| Kurulum perdesi | ekranda hiç görünmüyor |
| Bilgi sayfası | **kendi AP adını** "bağlı olduğum ağ" diye yazıyor |

Üçü de "AP açık mı" diye sorması gerekirken "durum ne" diye soruyordu.
Doğru kaynak **`ag_kurulum_agi_acik()`** (`g_kurulum_modu`): yalnızca
`ap_ac()` kaldırıyor, yalnızca iki çıkışta iniyor, STA olaylarından
etkilenmiyor. **`AgDurumu` bir durum makinesi hâli; radyonun o an ne
yaydığını anlatmıyor.**

İlk kanıt ekrandı: bilgi sayfası ağ adını yalnızca bağlıyken yazıyor,
ekranda "Pati-9EFD" yazıyordu. Belirtinin nerede olduğunu değil,
**hangi soruyu yanlış sorduğumuzu** gösterdi.

🔴 **Kurulum kipinde ekran artık gözler değil, yönlendirme gösteriyor.**
Eskiden gözler duruyordu ve gerekçesi yazılıydı: *"o hâli gözler
anlatıyor (uykulu) ve panel açık"*. Yanlıştı — **ebeveyn panelin açık
olduğunu göremiyor.** Telefonu Pati'nin ağına bağlayınca yakalama
sayfası bazen kendiliğinden açılmıyor ve o an cihazda hiçbir şey ne
yapılacağını söylemiyor; ebeveynin `pati.local` diye bir adres olduğunu
bilmesi beklenemez. Susan bir robot neden sustuğunu söylemeli — WiFi
perdesini de aynı gerekçe doğurmuştu.

Kurulum perdesi **iki adımlı** ve adımı *telefonun AP'ye bağlı olup
olmadığı* belirliyor (`ag_telefon_bagli`, sayaç zaten vardı):

| Adım | Ekranda |
|---|---|
| telefon yok | ağ adı büyük büyük (`Pati-9EFD`), **QR yok** |
| telefon bağlı | QR (`http://192.168.4.1`) + adres yazısı |

⚠️ **QR'ı önce göstermemek bilinçli.** `192.168.4.1` ancak telefon
Pati'nin ağına girdikten *sonra* bir yere gidiyor. Önce okutulsa
tarayıcı açılmaz ve ebeveyn bunu "QR bozuk" diye okurdu — adres,
ancak çalışır hâle geldiği an ekrana geliyor. Aynı kural panel QR
sayfasında da var: bağlıyken `pati.local`, kurulumda `192.168.4.1`,
ikisi de değilse **QR hiç çizilmiyor**.

🔴 **QR koyu-üstüne-açık olmalı.** Pati'nin ekranı siyah zemin +
turkuaz yazı; QR'ı o düzende çizmek *ters QR* olurdu ve bazı telefonlar
okumaz. Beyaz bir kartın üzerine siyah çiziliyor (`pati_perde.cpp` ·
`qr_ciz`). Matrisler `qr_uret.py` ile üretiliyor ve o betik yazmadan
önce çıkan pikselleri **bağımsız bir çözücüyle** (zxing-cpp) okuyup
doğruluyor.

**WiFi perdesi neden var:** wifi olmadan Pati konuşamıyor ve çocuk için
sebebi görünmüyordu — gözler normal bakıyor, robot cevap vermiyor.
Çocuk bunu "bozuldu" diye okur. Susan bir robot neden sustuğunu
söylemeli.

⚠️ **Güncelleme pilde %40 altında teklif edilmiyor.** İndirme, wifi
alıcısı ile flash yazmayı aynı anda çalıştırıyor — Pati'nin en yüksek
akım çektiği iş, ve pilde brownout henüz çözülmedi (`PIL.md`). Yarım
kalan OTA güvenli (önyükleyici eski sürüme döner) ama çocuk için
"güncelliyorum" deyip kapanmak kötü. Ekranda "önce şarja tak" yazıyor
ve tuş o sırada bir şey yapmıyor.

**Derin uyku gerçek kapanma değil.** Ekran söner, ses biter, L3B kesilir
— ama M5PM1 ayakta kalır (yeşil ışık yanar) ve pil yavaş akar. Karşılığı
aynı tuşla geri gelebilmek: ESP32 tamamen kapalıyken hiçbir yazılım
çalışmadığı için cihaz kendini **açamaz**. Gerçek kapanma yan düğmede.

🔴 **Yan düğme yazılımdan yönetiliyor.** `pati_pinler.h` uzun süre
"ESP32'ye görünmüyor, değiştirilemiyor" diyordu ve **ikisi de yanlıştı**:
durumu I2C'den okunuyor (`BTN_STATUS` 0x48) ve zamanlamaları
ayarlanabiliyor (`BTN_CFG_1` 0x49). Tek tıkla kapatabilmek için M5PM1'in
tek-tık-sıfırlaması kapatıldı; açık olsaydı cihaz biz okuyamadan
yeniden başlardı.

⚠️ **Kurtarma yolları bilerek açık:** indirme modu kilidine (`DL_LOCK`)
ve çift tıkla kapanmaya **hiç dokunulmuyor**. Yazılım açılmazsa yan
düğmeye 4 saniye basmak indirme moduna sokuyor. Kapatmanın ve
yüklemenin bizim koda bağlı olmayan bir yolu her zaman kalmalı.

---

## Durum

**GERÇEK KARTTA ÇALIŞIYOR** — 01.09.2026'da ilk kez ayağa kalktı.
Mikrofon, hoparlör, ekran, gözler, wifi, panel, Gemini sohbeti: hepsi
sınandı ve çalışıyor. Sohbet turları dönüyor.

İlk açılışta bulunan üç şey — hepsi düzeltildi, hepsi kaynakta yazılı:

1. **I2C 100 kHz olmalı, 400 değil.** M5PM1 adresini cevaplıyor ama
   register erişimini reddediyor. Tek sebep, dört belirti: L3B açılmaz
   → mikrofon/hoparlör/arka ışık beslenmez → ES8311 cevap vermez →
   donanım kilidi "yanlış kart" der, ekran simsiyah kalır.
2. **`DIN`/`DOUT` ters okunmuştu.** M5Stack'in tablosu satırı çevre
   birimiyle etiketliyor ama sinyal adlarını ESP32'nin ağzından
   yazıyor (aynı sayfadaki LCD satırındaki "MOSI" gibi). Doğrusu:
   `dout = G14` (hoparlör), `din = G16` (mikrofon). Yanlışken iki yön
   birden ölüydü.
3. **CPU 160 MHz'deydi** (IDF varsayılanı). Kare süresi 45–49 ms,
   bütçe 50 ms, 5 saniyede ~90 kare atlanıyordu. 240 MHz'de aynı kare
   29–31 ms ve atlama duruyor.

Kalan bilinmeyen yok denecek kadar az; `firmware/ILK-ACILIS.md` neyin
doğrulandığını ve neyin hâlâ ayarlanabilir olduğunu tutuyor.

---

## Beden — 2 DC motor + 2 servo

Pati bir gövdeye takılabiliyor: iki tekerlek (sarı TT redüktörlü motor,
L9110S sürücü), iki kol (SG90 sınıfı mini servo) ve gövdenin kendi
**4'lü AA pil** yuvası. StickS3 takılınca Pati bunu kendi anlıyor;
takılı değilken bedenle ilgili hiçbir şey görünmüyor.

Ayrıntı ve devreye alma ölçümleri: **`firmware/BEDEN.md`**. Kabloyu
takacak kişi için tek sayfalık şema: **`firmware/Pati_Tek_Bakis_Kablolama_Diyagrami.png`**.

🔴 **AA pilin (+) ucu StickS3'e HİÇBİR ŞEKİLDE gitmiyor.** Gövdeden
Stick'e giden sekiz kablonun hepsi ya toprak ya Stick'in kendi çıkışı;
6 V hattı gövdenin içinde kalıyor. Sebebi Hat2-Bus'ın dizilişi: sinyal
pinlerinin arasında BAT, 5V_IN, 3V3_L2 ve EXT_5V duruyor. Bu bir uyarı
değil, **pin seçiminin sebebi** — kural böyle kurulunca yanlış pine
kayan bir kablonun en kötü sonucu "motor sürekli dönüyor" oluyor.

🔴 **Pati kendi kararıyla DÖNER, kendi kararıyla İLERLEMEZ.**
Konuşurken arada kendiliğinden kıpırdıyor ve çocuk "dans et" deyince
yapıyor — ama bunların hepsi **yerinde dönüş**. Gerçekten yol almak
hâlâ yalnızca panelden, çocuğun parmağı altında ve ölü adam
zamanlayıcısıyla.

Ayrım keyfî değil: **Pati'de uçurum sensörü yok**, masanın kenarını
görebileceği hiçbir yol yok. Kendi kararıyla ilerleyen bir Pati eninde
sonunda düşer; "az hareket etsin" bunu geciktirir, engellemez.

Bu yüzden özerkliğin tamamı **tek bir sayıyla** anlatılıyor:
`sol = +teker, sag = -teker`. İki tekerlek her zaman ters yönde, yani
yer değiştirme sıfır. **Bu bir yorum değil, tipin kendisi** — jest
tablosunda ileri giden bir kare *yazılamıyor*
(`pati_beden_matematik.hpp`), konak testi her girdide `sol + sag == 0`
olduğunu tarıyor. Kuralı bilmeyen biri de bozamıyor.

⚠️ **Sesli komutun asıl riski yanlış anlama değil, DOĞRU anlama.**
"İleri git" doğru anlaşılırsa da Pati düşer. Üstelik joystick'in
aksine sesli komutun üstünde parmak yok: ölü adam zamanlayıcısı onu
koruyamaz, çünkü onay sürekli değil tek seferlik. Çözüm "model daha
iyi anlasın" değil, **sözlükte ilerlemenin hiç olmaması**. Pati
"yürüyemem" diyor ve paneli gösteriyor (`prototype/yuz.py` ·
`BEDEN_PROMPT_EKI`).

Panelde **her uzvun kendi kipi var** (tekerlekler / kollar), üç hâlli:
`KIP_ACIK` (varsayılan) · `KIP_KUMANDA` (yalnızca panelden) ·
`KIP_KAPALI` (hiç). Açma-kapama yetmiyordu: *kapalı* ile *sadece
kumandadan* arasındaki fark, çocuğun elinden kumandayı alıp almamak.

Sesli komut `KIP_KUMANDA`'da çalışmıyor — üstünde parmak yok, yani
Pati'nin kendi hareketi sayılıyor. Bunun için **isteğin kaynağı istekle
birlikte tek atomikte** taşınıyor (`no + kaynak * 256`); iki ayrı
değişken olsaydı yarış çıkar ve Pati'nin kendi isteği "kumandadan
geldi" diye geçerdi.

Kipler tek bir boğazda okunuyor (`pati_beden.cpp`), yeni bir yol açanın
unutabileceği ikinci bir kontrol yok.

🔴 **Pati bedeninin olup olmadığını BİLİYOR — ve bu ölçülmüş bir
hatadan çıktı.** Çocuk "kolunu kaldır" dedi, Pati "benim kolum yok ki"
dedi. Yanlıştı; gövdesi var, o an takılı değildi. Model bunu bilemezdi:
ana prompt "küçücük bir robotsun, gözlerin ekranda" diyor ve model
oradan **doğru** çıkarım yapıyor — eksik olan promptta olmayan bilgiydi.

Ana prompta dokunulmadı (Aşama 1'de ölçüldü, değişirse sayılar
karşılaştırılamaz). `yuz.PROMPT_EKI` deseni izlendi: duruma göre ek —
beden var / beden yok / az önce takıldı / tekerlek anahtarı kapalı.
Bedensizken `hareket` alanı **şemada hiç yok**, yani Pati yapamayacağı
bir şeyi teklif bile edemiyor.

⚠️ **Bir yetenek anahtarla kapatıldıysa MODELE de söylenmeli.**
Yalnızca motoru durdurmak yetmiyor: Pati "dans ediyorum!" der,
tekerlekler dönmez ve çocuk robotun bozulduğunu düşünür. Aynı gerekçe
panelde de var (tekerlekli jest düğmeleri anahtar kapalıyken sönüyor).

**Hareket için İKİNCİ BİR ARAÇ YOK, var olana alan eklendi.**
Ölçülmüş: her araç çağrısı cevabın önüne bir gidiş-dönüş koyuyor
(~682 ms) ve cihazda araçlar **sıralı** çalışıyor. İkinci bir araç,
modele durup beklemek için ikinci bir sebep olurdu. Bunun yerine zaten
açık olan `yuz_ifadesi` aracına isteğe bağlı `hareket` alanı eklendi.
Alan ve bedeni anlatan prompt eki **yalnızca beden takılıyken**
gönderiliyor; beden takılıp çıkarılınca beden katmanı oturum
tazelemesi istiyor (`ayar_yenileme_iste`).

🔴 **Motorlar anlık tam güce ASLA geçmiyor.** Kullanıcının açık isteği
(06.09.2026): *"bir daha motorları anlık %100'de yapma, ne olursa olsun
dikkatli gidelim."* Bir DC motorun en yüksek akımı kalkış anında oluyor
ve o tepe AA hattında gerilim çöküşü yapıyor — aynı hatta duran servolar
o çöküşü görüyor. Her değişim rampalı (`motor_rampa`) ve konak testi
koruyor. **Durmak rampadan geçmiyor:** ölü adam ya da parmağın kalkması
beklemez.

🔴 **Kalkmak ile gitmeye devam etmek AYRI iki sayı.** Motor dururken
yüksek güç istiyor, döndüğünde çok azı yetiyor. İkisi tek sayıya
bağlanınca robot ya ötüyor ya fırlıyor — arada kullanılabilir yer
kalmıyor (gerçek kartta ölçüldü, kullanıcının şikâyeti buydu). Çözüm:
kısa bir **kalkış darbesi** (%85, 180 ms, kendisi de rampalı — 100 ms'de
tepeye) sürtünmeyi kırıyor, sonra **gitme tabanı** (%17) devreye
giriyor. Joystick tepkisi ayrıca **karesel**: yarım itişte hızın dörtte
biri. Darbe, yukarıdaki kurala bilinçli ve **onayı alınmış** istisna.

🔴 **Ölü adam zamanlayıcısı pazarlıksız.** Komut gelmeden 600 ms geçerse
motorlar duruyor. Panel dokunma sürerken 150 ms'de bir gönderiyor.
Çocuk parmağını kaldırırsa, telefon kilitlenirse, wifi takılırsa Pati
duruyor — **panelin doğru davranmasına güvenmiyoruz.**

**Sıcak döngü tuzağına en yakın duran dosya `pati_beden.cpp`.** Bir
servo döngüsü doğası gereği 20-50 ms'de bir dönmek ister. Çözüm döngüyü
yavaşlatmak değil, **çoğu zaman hiç var olmaması**: görev boşta 200
ms'de bir uyanıyor, 20 ms'lik döngü yalnızca gerçekten bir şey hareket
ederken çalışıyor, ve komut gelince bildirimle uyandırılıyor. İçinde
I2C, kilit ve NVS yok; tek donanım teması LEDC yazmaçları.

**Motor gürültüsü asıl risk ve belirtisi tanıdık olacak:** ses cızırdar,
ES8311 cevap vermez, M5PM1 NACK verir — yani `TESHIS.md`'deki "yanlış
kart" tablosunun aynısı. Motor eklendikten sonra bu belirtiler görülürse
**önce gürültüye bakılacak, yazılıma değil.** Çare 100 nF'ları motor
uçlarına takmak.

**Sıradaki adım bedenin Pati'yi de beslemesi.** `PIL.md`'deki brownout
hâlâ açık ve bedende 4 AA pil var. Hat2-Bus'ın EXT_5V pini varsayılan
olarak giriş kipinde; 6 V → 5 V küçük bir çevirici Stick'i besleyebilir.
O zaman `guc_kaynak()` "USB" görür ve ses tavanı ile göz hızı
kendiliğinden yükselir — kod zaten öyle yazılmış. Ama o kablo yukarıdaki
kuralı deler, yani ayrı bir aşama ve ayrı bir karar.

---

## Çalışma biçimi

- **Test/teşhis kodu firmware'e commit edilmez.** Konak testleri
  (`firmware/test/`) ayrı ve depoya girer; geçici ölçüm kodu girmez.
- **Tahmin değil ölçüm.** Bu depoda birkaç kez "sebep şu olmalı" denildi
  ve ölçünce yanlış çıktı. Bir sayı iddia ediliyorsa nereden geldiği
  yazılmalı.
- **Yorumlar NEDEN'i anlatır**, ne yaptığını değil. Bir tuzak varsa
  belirtisi de yazılır ("şöyle görünürse bu değer yanlış").
- `PLAN.md` ve `prototype/NOTLAR.md` depoya girmez ama diskte durur:
  satın alınanlar, ölçümler, varsayımlar, saat yiyen tuzaklar.
