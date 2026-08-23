# Gövde

**Şu an bir gövde yok ve gerekmiyor.** Pati tek bir parça: M5Stack
StickS3, 48 × 24 × 15 mm. Ekran, hoparlör, mikrofon ve pil zaten
içinde, kabuğu da hazır.

---

## Eski gövde nereye gitti

Burada dört STL ve bir CAD dosyası vardı: iki parçalı bir kafa, bir
gövde ve bir taban. Elle lehimlenen ESP32-S3 devkit'i, 5 cm'lik
hoparlörü ve 240×240 ekran modülünü içine alacak şekilde çizilmişlerdi.

O donanım emekli oldu ve parçalar onunla birlikte arşivde:

```
git switch --detach v2.2.8-devkit
ls enclosure/
```

Silinmediler — `devkit` dalında ve `v2.2.8-devkit` etiketinde
duruyorlar. Buradan kaldırılmalarının tek sebebi yanıltıcı olmaları:
ölçüleri artık var olmayan bir cihaza ait.

---

## İleride bir gövde yapılırsa

Çubuğu içine alan bir kabuk mantıklı olabilir — tutması kolaylaşır,
düşünce daha dayanıklı olur, bir yüz ifadesi kazanır. Yapılırsa doğru
sayılarla başlasın diye:

| Ölçü | Değer | Kaynak |
|---|---|---|
| Çubuk | 48,0 × 24,0 × 15,0 mm | M5Stack ürün sayfası |
| Ağırlık | 20 g | aynı |
| Ekran aktif alanı | 25,2 × 14,2 mm | 1.14" köşegen, 135×240 |
| Hoparlör | 20 × 11 mm (2011 kasa) | aynı |
| USB-C | kısa kenarda | — |
| Grove (HY2.0-4P) | yan tarafta | G9 / G10 |
| Hat2-Bus | üst tarafta, 2.54 mm 16 pin | — |

M5Stack cihazın kendi CAD dosyalarını yayımlıyor — tahmin etmeye gerek
yok:

<https://github.com/m5stack/M5_Hardware/tree/master/Products/K150_StickS3/Structures>

### Tasarlarken akılda tutulacaklar

**Mikrofon ve hoparlör deliği kapatılmamalı.** İkisi de çubuğun
gövdesinde ve kapatılırsa ses hem girmez hem çıkmaz.

**Hoparlörün önü açık kalmalı.** 1 W'lık küçük bir hoparlör zaten
kısık; önüne konan her katman daha da kısar. Ses seviyesi
yazılımdan sınırlı (pil yüzünden — bkz. `firmware/main/pati_ses.hpp`),
yani kaybı yazılımdan telafi edemeyiz.

**Mikrofonu hoparlörden uzaklaştırmak gerçek bir kazanç olurdu.**
Şu an ikisi birkaç santim arayla ve bu yüzden söz kesme (barge-in)
kapalı: robot konuşurken kendi sesini duyup kendi sözünü kesiyor.
Gövde ikisini ayırabilirse o özellik açılabilir
(`CONFIG_PATI_YARIM_DUPLEKS`).

**USB-C'ye erişim kalmalı.** Şarj oradan; ayrıca bir şey ters giderse
kabloyla yükleme tek kurtarma yolu.

**Ekranın önüne füme akrilik koymak** yüzü "ekran yapıştırılmış kutu"
olmaktan çıkarıyor — bu, önceki tasarımda bulunmuş bir şeydi ve hâlâ
geçerli. `panel/gelistirici.html` içindeki stüdyoda açıp kapatarak
denenebiliyor.

**Servo takılacaksa besleme AYRI olmalı, sadece GND ortak.** Grove'un
taşıyabileceği en fazla yük 4,88 V @ 0,38 A ve M5Stack, çıkış
modundaki bir arayüzden güç vermenin kısa devre ve cihaz hasarı riski
taşıdığını yazıyor.
