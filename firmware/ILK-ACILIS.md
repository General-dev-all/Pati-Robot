# StickS3 — ilk açılış listesi

Kart kutudan çıktığında sırayla bakılacak şeyler. Her adımda **ne
görmen gerektiği** ve **görmezsen hangi tek satırın değişeceği** yazıyor.

Yazılım gerçek kartta hiç çalışmadı (kartlar Eylül 2026'da geliyor).
Kaynakta `⚠️` ile işaretli her yer, burada bir satıra karşılık geliyor.

---

## 0. Yükleme

Sağdaki Type-C porta tak. İndirme moduna girmek gerekirse: yan
düğmeye **basılı tut**, içerideki yeşil ışık yanıp sönünce bırak.

```
cd firmware
idf.py -p <PORT> flash monitor
```

`KARTA-YUKLE.bat` aynı şeyi yapıyor ama COM portunu CH343 köprüsüne
göre arıyor; StickS3'te köprü farklı olabilir, o zaman portu elle ver.

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
| `L3B acilamadi` | register yazımı başarısız → `PATI_PM1_*` (`pati_pinler.h`) |

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

**Ses kesik kesik** → DMA tamponu. `DMA_TANIM` (`main/pati_ses.cpp`)
24 ve bu ~512 ms tutuyor. Açılışta
`DMA icin ic RAM yetmedi` uyarısı varsa tampon yarıya inmiş demektir.

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

| Doğrulandı (kartsız) | Nasıl |
|---|---|
| Göz çizici ↔ tarayıcı | 16 ifade, 240×135, **0 farklı piksel** |
| Yeniden örnekleyici | perde, süre, parça sınırı, 500 sn kayma, sınırlayıcı |
| Hafıza motoru ↔ Python | 1008 kontrol |
| Derleme, bölüm tablosu | `pati.bin` 1,46 MB / 3,94 MB yuva |

| Doğrulanmadı — kart gerekiyor |
|---|
| ES8311 register dizisi (sesin gerçekten çıkması) |
| M5PM1 register bit düzeni (L3B ve amfi) |
| Ekran yönü, aynalama ve piksel kayması |
| Pil ömrü ve güvenli ses seviyesi |
| Mikrofon kazancı |
