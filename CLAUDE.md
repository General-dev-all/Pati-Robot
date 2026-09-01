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

Konak testleri (üçü de geçmeli):
```
cd firmware\test && derle.bat
```
1. Göz çizici ↔ tarayıcı, piksel piksel
2. Hafıza motoru ↔ Python prototipi
3. Yeniden örnekleyici — Pati'nin sesi

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

**USB'yi çekince seri kablo da gidiyor.** Pilde ne olduğu seri porttan
görülemiyor — tek pencere panelin `api/durum` → `guc` alanı.

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

## Gelecek: 2 DC motor + 2 servo

Pati'ye ileride **iki DC motor ve iki servo** eklenecek, **ayrı
beslemeli**. Henüz yapılmadı ama bugünkü kararları etkiliyor, o yüzden
burada:

**Ayrı besleme pazarlıksız.** Kart 250 mAh'lik hücreyle zaten sınırda:
01.09.2026'da yalnızca hoparlör akımıyla brownout yaşandı
(`TESHIS.md`). Motorları aynı raydan beslemek Pati'yi her harekette
kapatır. Motor beslemesi ayrı olmalı ve **yalnızca toprak ortak**.

**Boş pin az.** StickS3'te ne varsa `pati_pinler.h`'de yazılı: I2C
(47/48), I2S (14-18), ekran (38-45), tuşlar (11/12). Kalanlar Grove
portu ve HAT başlığı. Dört PWM kanalı (2 motor + 2 servo) buraya
sığmalı; sığmazsa I2C'den bir PWM sürücüsü (PCA9685 gibi) tek çözüm ve
hat zaten kurulu.

**Gürültü asıl risk.** Motor akımı I2S ve I2C hatlarına biniyor.
Belirtisi tanıdık olacak: ses cızırdar, ES8311 cevap vermez, M5PM1
NACK verir — yani `TESHIS.md`'deki "yanlış kart" tablosunun aynısı.
Motor eklendikten sonra bu belirtiler görülürse **önce gürültüye
bakılmalı**, yazılıma değil.

**Zamanlama bütçesi dar.** Göz karesi 24-30 ms, bütçe 50 ms. LEDC
donanımdan sürüyor, yani PWM'in kendisi bedava; ama motor mantığı ses
ve göz görevleriyle aynı çekirdekleri paylaşacak. O gün geldiğinde
`TESHIS.md`'deki sayılar (atlanan kare, dahili SRAM dip noktası)
karşılaştırma zemini olacak — bugünkü değerler oraya yazılı.

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
