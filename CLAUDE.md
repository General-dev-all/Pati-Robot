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

**`sdkconfig` zaten varsa `sdkconfig.defaults` OKUNMAZ.** Varsayılan
değiştirdikten sonra `idf.py reconfigure`, yoksa ölçüm anlamsız çıkar.
Bir kez yaşandı.

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

**Pilde ses seviyesi.** M5Stack yazıyor: yüksek seviyede çekilen akım
cihazı yeniden başlatıyor. Varsayılan 1.00 ve bilinçli düşük.

---

## Durum

Yazılım tamam ve derleniyor. Kartsız doğrulanabilen her şey doğrulandı:
gözler 240×135'te tarayıcıyla **0 farklı piksel**, yeniden örnekleyici
gerçek Gemini kaydıyla sınandı.

**Gerçek kartta hiç çalışmadı** — kartlar Eylül 2026'da geliyor.
Doğrulanmamış varsayımlar (kodek register'ları, ekran yönü ve kayması,
güç rayları) kaynakta `⚠️` ile işaretli ve hepsi tek satırlık anahtar.

Kart gelince sırayla ne yapılacağı: `firmware/ILK-ACILIS.md`.

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
