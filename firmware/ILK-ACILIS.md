# StickS3 — ilk açılış listesi

Kart takıldığında sırayla bakılacak şeyler. Her adımda **ne
görmen gerektiği** ve **görmezsen hangi tek satırın değişeceği** yazıyor.

**01.09.2026: kart geldi ve her şey çalıştı.** Aşağıdaki liste artık
"bilinmeyeni bul" değil, "bir şey ters giderse nereye bak" listesi.
İlk açılışta bulunan üç şey en sonda ayrı bir bölümde.

---

## 0. Yükleme

Sağdaki Type-C porta tak. İndirme moduna girmek gerekirse: yan
düğmeye **basılı tut**, içerideki yeşil ışık yanıp sönünce bırak.

```
cd firmware
idf.py -p <PORT> flash monitor
```

`KARTA-YUKLE.bat` aynı şeyi yapıyor ve portu kendi buluyor. StickS3'te
USB-UART köprüsü **yok** — ESP32-S3'ün kendi USB'si Windows'ta
"USB Seri Cihaz (COMx)" diye görünüyor.

⚠️ **Seri portu izlerken DTR/RTS'e dokunma.** Bu kartta o hatlar kartı
indirme moduna sokuyor (`boot:0x0 DOWNLOAD`, yeşil ışık yanıp söner) ve
uygulama hiç açılmaz. `esp_idf_monitor --no-reset` kullan; sıfırlamak
için yan düğmeye bir kez kısa tıkla.

⚠️ **Açılışın ilk ~1 saniyesi USB'de görülmez** — her sıfırlamada USB
yeniden numaralanıyor ve bilgisayar portu o kadar süre kaybediyor. O
yüzden ağ kalktıktan sonra bir `===== DURUM =====` özeti basılıyor;
güç, kodek, ses ve ekranın durumu orada.

🔴 **`idf.py flash`, `app-flash` değil.** Bölüm tablosu özel ve 8 MB'a
göre yeniden yazıldı; sadece uygulamayı yazmak eski tabloyu bırakır.

---

## 1. Güç katmanı — her şeyden önce

Seri portta şunu görmelisin:

```
pati.guc: guc hazir — I2C 48/47, L3B acik, amfi acik
```

**Görmezsen** hiçbir şey çalışmaz: mikrofon sağır, hoparlör sessiz,
ekran siyah. Üçü birden.

| Ne yazıyor | Anlamı |
|---|---|
| `M5PM1 (0x6E) cevap vermiyor` + `I2C hattinda HIC KIMSE yok` | I2C hattı sorunu — pin ya da güç |
| `M5PM1 (0x6E) cevap vermiyor` + başka adresler listeleniyor | hat sağlam, M5PM1'in adresi farklı |
| `PYG2 BEKLENEN GIBI AYARLANMADI` | **register bit düzeni varsayımı yanlış** — aşağı bak |
| `L3B acilamadi` | register yazımı başarısız → `PATI_PM1_*` (`pati_pinler.h`) |

**`BEKLENEN GIBI AYARLANMADI` çıkarsa:** M5PM1'e yazıp geri okuyoruz ve
yonga bizim yazdığımızı anlamamış. Bu depoda `FUNC0` register'ının pin
başına iki bit tuttuğu ve pin N'in `[2N+1:2N]` bitlerinde olduğu
**varsayılıyor** — sürücü kütüphanesinin başlığında adresler var ama bit
yerleşimi yazmıyor. Log satırı `mode=0x..` ve `out=0x..` değerlerini
basıyor; hangi bitin oynadığına bakılıp `pm1_cikis` (`pati_guc.cpp`)
düzeltilir.

Bu satır çıkmıyorsa varsayım doğrudur ve konu kapanmıştır.

Kod: [`main/pati_guc.cpp`](main/pati_guc.cpp)

---

## 2. Donanım kilidi

```
pati.guc:  (sessiz — her şey yolunda)
```

**`===== YANLIS DONANIM =====` görüyorsan** ES8311 cevap vermiyor. Yanlış
karta yüklemiş olabilirsin; StickS3'teysen I2C hattı ya da L3B'den
gelen besleme sorunlu (adım 1'e dön).

---

## 3. Ekran

Açılışta test deseni çıkıyor. Soldan sağa altı dikey çubuk:
**kırmızı · yeşil · mavi · beyaz · siyah · turkuaz**

Seri port zaten neyin yanlış olduğunu söylüyor, ama özet:

| Gördüğün | Değişecek satır (hepsi `main/pati_ekran.cpp` başında) |
|---|---|
| Kırmızı ile mavi yer değişmiş | `BAYT_CEVIR` |
| Siyah ile beyaz yer değişmiş | `RENK_TERS` |
| Çubuklar **yatay** duruyor | `EKR_CEVIR` |
| Sıra tersten (turkuaz solda) | `EKR_AYNA_X` |
| Görüntü baş aşağı | `EKR_AYNA_Y` |
| Kenarda ince çöp şeridi, birkaç piksel kayık | `EKR_KAYMA_X` / `EKR_KAYMA_Y` — **önce ikisini takas et** |
| Hiçbir şey yok | L3B (adım 1) ya da `PATI_EKR_BLK` |

Kayma değerleri en olası aday: panelin 135×240 görüntüsü 240×320'lik
çerçeve belleğinin ortasına düşüyor ve yatay kullanımda satır/sütun
kaymaları yer değiştiriyor. Doğrulanmadı.

---

## 4. Ses

```
pati.ses: ses hazir — ES8311 48000 Hz mono, mik 16000 Hz, hoparlor 24000 Hz x 1.30
```

Sonra Pati konuşunca dinle. Bu sıralamayla:

**Hiç ses yok** → amfi (adım 1'deki `amfi acik` satırı) ya da
`kodek etkinlestirilemedi`.

**Ses var ama çok kısık** → `SES_SEVIYESI_BASLANGIC` (`main/pati_ses.hpp`)
1.00 olarak başlıyor. Panelden yükselt.
⚠️ **Pilde çalışırken çok yükseltme.** M5Stack, yüksek seviyede çekilen
akımın cihazı yeniden başlattığını yazıyor. Pati cümle ortasında
kapanıp açılıyorsa seviye yüksek demektir.

**Ses cızırtılı / robotik** → yeniden örnekleyici. Ama konak testi bunu
zaten doğruluyor:
```
cd firmware\test && derle.bat
```
Test geçiyorsa sorun örnekleyicide değil, kodek register'larında.

**Mikrofon duymuyor** → `set_mic_gain` 30 dB (`main/pati_ses.cpp`).
MEMS mikrofonun sinyali zayıf; 36 veya 42 dB denenebilir.

**Ses kesik kesik** → DMA tamponu. Açılışta seri porta yazılıyor:

```
pati.ses:   DMA tamponu 16 x 1024 = 341 ms, ~64 KB (iki yön)
```

341 ms, ölçülen en uzun Gemini parçasının çalma süresinin (215 ms) bir
buçuk katı. Takılma olursa `DMA_TANIM` (`main/pati_ses.cpp`) 20'ye,
sonra 24'e çıkarılabilir — her adım ~8 KB iç RAM yiyor ve o RAM'i wifi
ile TLS de kullanıyor.

⚠️ Açılışta `ic RAM yetmedi` uyarısı varsa tampon kendiliğinden
küçülmüştür; o zaman **artırmak değil**, `DMA_TANIM`'ı kalıcı olarak
indirmek gerekiyor — yer, çalışırken başka bir şeyi açlıkta bırakıyor
demektir.

---

## 5. Gözler

Ekranda iki turkuaz göz. Kırpıyor, nefes alıyor, bakınıyor.

Göz **çok büyük / çok küçük / kenardan taşıyor** görünüyorsa: değerler
ölçülerek seçildi ama görünüm kararı senin. `panel/studyo.html`
kaydırıcılarla canlı deneme için; beğendiğin değerleri
`panel/gozler240.js` içindeki `AYAR` bloğuna yaz, sonra:

```
cd firmware && node goz_uret.mjs
cd test && derle.bat          # C++ ile tarayıcı hâlâ aynı mı
```

---

## 6. Ağ ve panel

Kayıtlı ağ yoksa Pati kendi ağını açıyor. Telefondan bağlan, ev
wifi'sini gir. Sonra panel: **http://pati.local**

Gemini anahtarı panelden giriliyor; yazılımın içinde anahtar yok.

---

## Ne çalışıyor, ne doğrulanmadı

Aşağıdaki ölçümler 01.09.2026 tarihli ilk açılışa aittir. Güncel pil
çalışması ve 05.09 düzeltmeleri için `PIL.md` esas alınır.

| Doğrulandı (kartsız) | Nasıl |
|---|---|
| Göz çizici ↔ tarayıcı | 16 ifade, 240×135, **0 farklı piksel** |
| Yeniden örnekleyici | perde, süre, parça sınırı, 500 sn kayma, sınırlayıcı |
| Hafıza motoru ↔ Python | 1008 kontrol |
| Derleme, bölüm tablosu | `pati.bin` 1,46 MB / 3,94 MB yuva |

| Kartta doğrulandı (01.09.2026) | Ölçüm |
|---|---|
| Güç katmanı, L3B, amfi | `guc hazir — I2C 48/47 @100 kHz` |
| ES8311 kodek | register dökümü: ADC açık, seviye `0xbf`, port susturulmamış |
| Mikrofon | konuşurken tepe genlik 11.000–32.000 |
| Hoparlör | 1 kHz test sesi duyuldu |
| Ekran, gözler | kare 29–31 ms (bütçe 50.000), atlama ~0 |
| Wifi, panel, Gemini | sohbet turları dönüyor |
| Mikrofon kazancı (18 dB) | 22 raporda **sıfır kırpma**, konuşma 21.000–27.000 |
| Kopan oturumun toparlanması | gerçek kopmada 25–26 ms'de yeniden bağlandı |
| Güç kaynağı algılama | USB'de VIN 4878 mV, pilde 0–2 mV |
| Pilde ses tavanı | pile geçince `ses_tavani` 1.00 → 0.70 |
| Yonga sıcaklığı | çalışırken 57–64 °C |
| Dahili SRAM | dip nokta 49.067 bayt (önce 1.903'tü) |

| Hâlâ ayarlanabilir | Belirtisi |
|---|---|
| Ekranın yönü/aynalaması | görüntü ters ya da yan durur |
| `SES_PIL_TAVANI` (0.70) | pilde ses kısık gelir ya da hâlâ brownout olur |
| DMA tamponu (341 ms) | konuşurken kısa takılmalar |
| Göz yerleşimi | görünüm tercihi, `panel/gelistirici.html` |

| Açık kalan | Not |
|---|---|
| Wifi sinyali 1–2/4 | zayıf; TCP takılması ve kopma buradan geliyor, yeri değişmeli |
| Pilde konuşurken reset | iki kartta da görüldü; güncel kayıt `PIL.md` |
| Pil ömrü | hiç ölçülmedi |
| Uyku/uyanma | sonraki tuş/yığın düzeltmeleri için `TESHIS.md`; uyku akımı ölçülmedi |

> Kesilme, donma ve çökme teşhisi için ayrı belge: **`TESHIS.md`**.

---

## İlk açılışta bulunan üç şey

**1. I2C 400 kHz çalışmıyor, 100 kHz gerekiyor.** M5PM1 adresini
cevaplıyor (yoklama geçiyor) ama ilk register erişiminde NACK veriyor.
Tek sebep, dört belirti: L3B açılmaz → mikrofon, hoparlör ve arka ışık
beslenmez → ES8311 cevap vermez → donanım kilidi "yanlış kart" der,
ekran simsiyah kalır. Kaynağı M5Stack'in kendi sürücüsü:
`_requestedSpeed = M5PM1_I2C_FREQ_100K`, `speed400k = false`.

**2. `DIN`/`DOUT` ters okunmuştu.** M5Stack'in tablosu satırı
`ES8311` diye etiketliyor ama sinyal adlarını **ESP32'nin ağzından**
yazıyor — aynı sayfadaki LCD satırındaki `MOSI` de ekranın değil
ESP32'nin çıkışı. Doğrusu `dout = G14` (hoparlör), `din = G16`
(mikrofon). Yanlışken iki yön birden ölüydü ve belirti "mikrofon tam
sıfır okuyor" idi.

Elenenler (her biri o an doğru cevap gibi görünmüştü): kodek saati,
ADC seviyesi, `no_dac_ref`, I2S yuvası (sol/sağ), analog/PDM seçimi.
Kodeği kesin olarak eleyen şey yongadan alınan **register dökümü**
oldu.

**3. CPU 160 MHz'deydi.** IDF'in varsayılanı; hiç ayarlanmamıştı. Kare
45–49 ms sürüyor ve 5 saniyede ~90 kare atlanıyordu. 240 MHz'de aynı
kare 29–31 ms, atlama duruyor.
