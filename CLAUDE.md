# ⚠️ BU DAL BİR ARŞİV — burada geliştirme yapılmaz

Bu, Pati'nin **emekli olmuş** ilk donanımının son çalışan hâli:

- ESP32-S3-DevKitC klonu (N16R8, 16 MB flash, CH343P köprü)
- INMP441 I2S mikrofon — lehimli
- MAX98357A amfi + 5 cm 8Ω 5 W hoparlör — lehimli
- ST7789 240×240 ekran — **hiç çalışmadı, modül arızalıydı**
- Duvardan 5 V, 13 lehimli kablo

Sürüm 2.2.8. Derlenmiş `pati.bin`, `v2.2.8` Release'inin içinde.

---

## Ne yapılır, ne yapılmaz

**YAPILMAZ:** Buraya kod yazılmaz, hata düzeltilmez, sürüm çıkarılmaz.
Bu dal donmuş bir kayıt.

**YAPILIR:** Geçmişe bakmak. "Eskiden şu nasıldı?" sorusunun cevabı
burada — pin haritası, montaj rehberi, kablo şeması, 3D baskı parçaları.

---

## Güncel Pati nerede

`main` dalında. **M5Stack StickS3** (SKU K150): anahtarlık boyunda tek
parça, ekran/mikrofon/hoparlör/pil içinde, **lehim yok**.

```
git switch main
```

Kodun %85'i buradan aynen geçti. Değişenler: tek I2S saati (mikrofonla
hoparlör aynı yongada), 8 MB bölüm tablosu, 240×135 göz yerleşimi ve
hız çarpanının yazılıma taşınması.

---

## 🔴 Bu yazılım o karta, o yazılım bu karta yüklenmez

İkisi de `esp32s3`, yani birbirinin imajını kabul eder ve açar — ama
pinler, ses yolu ve bölüm tablosu farklı.

`main`'deki yazılım kendini koruyor: açılışta ES8311'i yokluyor,
bulamazsa geri alınıyor. **Bu daldaki 2.2.8'de öyle bir koruma yok**
(o yazıldığında tek donanım vardı). Yani bu kartın imajını StickS3'e
yüklemek sessizce çalışmayan bir cihaz üretir.
