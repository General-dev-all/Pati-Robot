# Pil modu — Pati pilde çökmemeli

Bu belge **devam eden bir işin** çalışma notu. `TESHIS.md` çözülmüş
sorunları anlatır; burası henüz çözülmemiş olanı.

---

## Amaç

**Pati pille çalışırken çökmemeli.**

Çocuk Pati'yi çoğunlukla pilde kullanacak. "Şarja tak" bir cevap değil —
çocuğu sürekli kabloya bağlı kalmaya zorlayamayız. Harici besleme
(ileride motorlar için gelecek olan) bu sorunu çözer ama **pil modunu
ortadan kaldırmaz**; ikisi ayrı iş.

USB'de Pati sorunsuz çalışıyor. Sorun yalnızca pilde.

---

## 🔴 Kullanıcının öncelik sırası — pazarlık burada biter

Feda etme sırası, en önemliden en feda edilebilire:

1. **Çökmeme** — her şeyin üstünde
2. **Ses seviyesi** — `SES_PIL_TAVANI` **0.70'te sabit, DAHA AŞAĞI
   İNİLMİYOR.** Kullanıcı bunu iki kez açıkça söyledi. Pilde kısık ses
   gerçek bir kayıp: çocuk duyamıyorsa robot işe yaramıyor.
3. **Ekran parlaklığı** — pilde %45. Gerekirse biraz daha kısılabilir,
   ama önce diğerleri denenmeli.
4. **Gözler** — pilde feda edilebilir tek yer. İfadeler güzel ama
   akıcılık, sohbetin kendisinden önce gelmez.

---

## Belirti

Pilde, Pati **konuşmaya başladığı anda** brownout:

```
00:13:03  ifade=dusunuyor    <- cevap uretiliyor
00:13:05  COKTU
00:13:10  geri geldi -> acilis=brownout
```

Öncesinde pil geriliminde sarkma görülüyor (3816 tabanından 3696'ya).
Sıklık: **20-60 saniyede bir**, pil 3,8-3,9 V civarındayken.

Pati 5 saniyede kendine geliyor — bekçi işini yapıyor. Ama çocuk için
cümle ortasında kaybolmak kabul edilemez.

---

## Ölçülmüş gerçekler

- **Pil sağlam.** 4,13 V'tan başlıyor; çökmeler 3,8-3,9 V'ta da,
  4,0 V'ta da oluyor. "Pil bitmiş" değil.
- **İki kart da aynı.** İkinci kart kutudan yeni çıktı, aynı davranıyor.
  **Donanım kusuru değil**, garanti konusu değil.
- **Tetikleyici konuşma.** Her çökme `dusunuyor`/`konusuyor` anında.
  Yani amfi + telsiz + PSRAM aynı anda akım çekerken.
- **Sıcaklık etken değil.** 55-64 °C ölçüldü; bu yonga için normal,
  PMIC'in ısıl koruması bu sıcaklıkta devreye girmez.
- **Göz çizici en büyük sürekli CPU müşterisi.** Kare başına çizim
  24-30 ms, bütçe 50 ms — bir çekirdeğin yarısından fazlası kesintisiz.
  İşin %85'i şekil rasterlemesinde.

---

## Denenenler

| Deneme | Gerekçe | Sonuç |
|---|---|---|
| Ses 1.00 → 0.70 | amfi akımı azalsın | ❌ çökme sürdü |
| Ekran %100 → %45 | taban akım düşsün, tepeye pay kalsın | ❌ çökme sürdü |
| PSRAM 80 → 40 MHz | veri yolu kilitleniyordu | ✅ ama **başka bir sınıfı** çözdü (bkz. TESHIS.md), brownout'u değil |
| Gözler 20 → 10 fps (pilde) | CPU doluluğu yarıya insin | ✅ **çökme arası ~40 sn → ~4,5 dk** (aşağıda) |
| Gözler konuşurken 10 → 5 fps | çökmelerin hepsi konuşma anında | 🔄 sınanmadı |
| Göz parlama katmanı 3 → 1 (pilde) | çizim maliyetinin **%81'i** bu katmanlarda (ölçülmüş) | 🔄 sınanmadı |
| Ekran %45 → %35 | taban akım | 🔄 sınanmadı |
| Wifi verici 20 → 15 dBm (pilde) | telsiz gönderirken 250-350 mA | ❌ **geri alındı** — menzil çöktü (3/4 → 1/4), "modemin dibinde bile zor çekiyor" |
| Ses tavanı 0.70 → 0.65 | hoparlör tepe akımı | 🔄 sınanmadı |
| Flash 80 → 40 MHz | PSRAM'den sonraki ikinci saat | 🔄 sınanmadı |
| Brownout eşiği | eşik gereksiz hassas olabilir | ❌ **çürütüldü** — zaten en toleranslı (aşağıda) |
| Açılışta güç kipinin geç girmesi | 6 sn boyunca tam yük | ✅ kusur, düzeltildi (aşağıda) |

⚠️ İlk ikisi **sağlam akıl yürütmeye** dayanıyordu ve tutmadı. Ders:
akım üzerine akıl yürütmek bu projede işe yaramıyor, çünkü **hiç akım
ölçülmedi.** İşe yarayan iki değişikliğin de mekanizması sayıyla
görülmüştü: PSRAM'de program sayacı, gözlerde kare süresi.

### Göz kare hızı ölçümü — 02.09.2026 akşamı

2. kart, 9 dakika 6 saniye pilde, sallamadan, normal sohbet.

| | Zemin (20 fps) | 10 fps |
|---|---|---|
| Çökme | 20-60 sn'de bir | 2 çökme / 9 dk |
| Dağılım | düzenli | 4 dk 52 sn → 49 sn → **3 dk 19 sn temiz** |
| Pil aralığı | 3,8-4,0 V | **3,83 → 3,68 V** |

Yaklaşık **yedi kat seyrelme**, üstelik zeminden **daha düşük gerilimde**.
Göz yükü gerçek bir etken — bu artık tahmin değil.

**Ama çökme sıfırlanmadı.** Göz yükü tek sebep değil.

İkinci bulgu: çökmeler 3744 ve 3732 mV'ta oldu, ardından **3682 mV'ta
3,5 dakika hiç çökme olmadı.** Yani "pil azaldıkça daha sık çöker"
tek başına doğru değil. Tek ölçüm bunu kesinleştirmiyor; `cokme_mv`
alanı (panel → `guc.cokme_mv`) tam bu soruyu biriktirmek için var.

### Açılış penceresi — bulunmuş ve düzeltilmiş kusur

Aynı kayıtta görüldü:

```
19:16:04  cokme=1  goz_fps=20   <- yeniden basladi, gozler HALA tam hizda
19:16:10  cokme=1  goz_fps=10   <- pil kipi ANCAK simdi girdi
```

Güç kipi ana döngüde uygulanıyordu ve o döngü **wifi bağlanıp ilk Gemini
oturumu açıldıktan sonra** başlıyor. Yani Pati brownout'tan yeni
kalkmışken — pil zaten düşük, telsiz bağlanıyor (en yüksek akım), TLS
el sıkışması CPU yiyor — gözleri de tam hızda çiziyordu. Wifi yavaş
bağlansa bu pencere yirmi saniye olurdu.

Çözüm: ayrı bir **güç gözcüsü görevi** (`app_main.cpp` → `guc_gozcusu`),
gözler açılır açılmaz başlıyor ve hiçbir şey beklemiyor.

---

## 🔴 Brownout eşiği — araştırıldı, ÇÜRÜTÜLDÜ

02.09.2026'da şu hipotez kuruldu: "çökmelerin hepsi brownout diyor,
belki eşik gereğinden hassas ve cihaz aslında çalışabilecekken
resetleniyor."

**Yanlış çıktı.** ESP32-S3'te seviyelerin yönü ESP32'nin **tersi**:

```
LVL_SEL_1 = 3.30 V   (en hassas)
LVL_SEL_3 = 2.98 V
LVL_SEL_4 = 2.84 V
LVL_SEL_7 = 2.44 V   (en toleransli)
```

Kaynak: `esp-idf/components/esp_hw_support/power_supply/port/esp32s3/Kconfig.power`
— forum yorumu değil, IDF'in kendi tanımı. (Bir forumda "level 7 =
2,98 V" yazıyordu ve o ESP32 içindi; ona güvenilseydi eşik yanlış
yönde değiştirilecekti.)

Pati **zaten `LVL_SEL_7`'de**, yani en toleranslı ayarda. Düşürülecek
yer yok.

**Ama bu bir şey öğretti ve önemli:** ray gerçekten **2,44 V'a
düşüyor.** 3,3 V beslemede bu küçük bir dalgalanma değil, ciddi bir
çöküş. Yani sorun ölçüm eşiğinde değil, gerçekten güçte — ve yazılım
tarafında aranacak şey "hangi kod çöktürüyor" değil, **"hangi kod
akım çekiyor"**.

---

## Denenmemiş adaylar

**Wifi verici gücü** — en ciddi şüpheli. ESP32-S3 telsizi gönderirken
250-350 mA çekiyor; bu muhtemelen hoparlör amfisinden **daha büyük** bir
tepe. `esp_wifi_set_max_tx_power()` ile kısılabilir. Bedeli menzil, ve
wifi zaten 1-3/4 olduğu için bedava değil.

**CPU 240 → 160 MHz (pilde).** Kayda değer akım kazancı. Ama 20 fps'te
kare bütçesi zaten dardı (160 MHz'de kare 45-49 ms / bütçe 50 ms);
gözler zaten 10 fps'e indiyse bu artık mümkün olabilir.

**Wifi güç tasarrufu (pilde).** Ortalama akımı ciddi düşürür. ⚠️ Ama
`WIFI_PS_NONE` **bilerek** seçilmişti: radyo DTIM aralıklarında uyuyunca
ses tamponu kuruyor ve konuşma kesiliyor. Kullanıcının kırmızı çizgisi
tam da bu.

**Akıllı göz optimizasyonu** — kare hızını düşürmek kaba bir çözüm.
Kalan gerçek kazanç şurada:
- *Sadece değişeni çiz.* Gözler kırpma aralarında sabit; o karelerde iş
  yapılmamalı.
- *Boştayken yavaşla, hareket olunca hızlan.*
- ~~*Konuşma başlarken yükü bırak.*~~ → yapıldı: pilde `konusuyor`
  ifadesinde kare aralığı 200 ms (`PIL_KONUSMA_FPS`). Henüz ölçülmedi.

**Ekranı düşük pilde daha da kısmak.** Yapılmadı, bilerek. Kullanıcının
sırasında ekran ışığı sesin altında ama gözlerin üstünde, ve düşük
gerilim–çökme ilişkisi henüz ölçümle görülmedi. Önce `cokme_mv` verisi
birikmeli.

---

## Düşük pil uyarısı

**Neden var:** çocuk Pati'yi her zaman dolu pille kullanmayacak, ve
"şarja tak" diye bir şey söylenmezse pilin bittiğini ancak Pati
sustuğunda anlayacak. Cümlenin ortasında sessizce ölmektense uyarmak
iyi. Kullanıcının kendi isteği.

- Eşik **%20**, histerezisli (%25'in üstüne çıkmadan sönmüyor)
- **Dakikada bir, üç saniye**, tam ekran — turuncu pil sembolü, yüzde,
  altında **ŞARJA TAK**
- USB'de hiç çıkmıyor: şarj olan pil için uyarmak saçma

**Yüzde ham gerilimden hesaplanmıyor.** Gerilim yük altında sarkıyor
(02.09.2026: konuşurken 3784 → 3664 mV, 120 mV). Anlık okumayla yüzde
verilse çocuk her konuştuğunda pil düşüp çıkardı. Onun yerine **son 30
saniyenin tepe değeri** kullanılıyor — sarkma hep aşağı doğru olduğu
için tepe, dinlenmiş gerilime en yakın olan. Cihazda doğrulandı: anlık
3752 mV okunurken yüzde 54'te kaldı, gerilim 3870'e dönünce 55 oldu.

⚠️ **Gerilim → yüzde tablosu ölçülmüş değil** (`pati_guc.cpp`, `EGRI`).
Lityum hücrelerin bilinen boşalma eğrisi. Gerçekten ölçmek için pili
tam doludan tam boşa sabit yükte boşaltmak gerekir; yapılmadı. Yüzde
bir gösterge, yakıt ölçer değil.

🔴 **Uyarı ekranı yalnızca göz görevinden çizilebilir.** Şerit tamponları
ve SPI onun malı; başka görevden çizmek ekranı bozar. `pati_uyari.hpp`
bunu anlatıyor, `gozler_pil_uyarisi()` sadece bayrak bırakıyor.

---

## Nasıl ölçülüyor

🔴 **USB'yi çekince seri kablo da gidiyor.** Pilde tek pencere panel.

```powershell
(Invoke-WebRequest http://pati.local/api/durum -UseBasicParsing).Content
```

`guc` alanı: `kaynak`, `pil_mv`, **`pil_yuzde`**, `pil_dusuk`, `vin_mv`,
`sicaklik_c`, `ses_tavani`, `goz_fps`, `acilis`, **`cokme`**,
**`cokme_mv`** (son çökmedeki pil gerilimi — "pil azalınca daha sık mı
çöküyor" sorusunu sınayan tek veri).

**`cokme` sayacı cihazda, NVS'te durur** ve açılışı atlatır. Yalnızca
arıza sayılır (brownout, panic, bekçi); düğmeye basmak ve kabloyla
yükleme sayılmaz. Dün gece çökmeler ancak bilgisayardan ağ üzerinden
sayılabiliyordu; çocuğun evinde öyle bir imkân olmayacak, ve yapılacak
işin tamamı "şu değişiklik çökmeyi azalttı mı" karşılaştırması.

**A/B yöntemi:** sayacı not al → USB'yi çek → 5-10 dakika normal konuş
(sallamadan; sallama ayrı bir değişken) → sayaca tekrar bak.

Zemin: **02.09.2026 akşamı, 10 fps ile 9 dakikada 2 çökme.**
(Ondan önceki zemin 20 fps ile 20-60 saniyede birdi.)

⚠️ **Sayaç geliştirme sırasında kirleniyor.** 02.09.2026 gecesi 2'den
17'ye çıktı ve artışın neredeyse tamamı yazılım denemelerindendi:
yığın taşması panic'leri, tam ekran çizim brownout'ları, seri port
açmanın ürettiği sıfırlamalar. Ölçüme başlarken **o anki değeri not
al**, mutlak sayıya bakma.

---

## Pili elle korumak — tuşlar

Çocuk Pati'yi kullanmadığında kapatabiliyor artık; bu, pil ömrünü
uzatmanın yazılımdan bağımsız tek yolu.

- **Yan düğmeye tek tık:** tamamen kapatır (M5PM1 sistem komutu). Pil
  hiç akmaz. Açmak yine tek tık.
- **Mavi tuşa uzun bas:** derin uyku. Ekran ve ses gider ama M5PM1
  ayakta kalır, yani pil **yavaş akmaya devam eder**. Karşılığı aynı
  tuşla geri gelebilmek.

Uzun süre kullanılmayacaksa doğru olan yan düğme.

⚠️ Derin uykunun akımı **ölçülmedi**. "Haftalarca dokunulmazsa biter"
tahmini, ölçüm değil. Merak edilirse yöntem basit: dolu pille uykuya
al, bir gece bekle, panelden `pil_mv`'ye bak.

---

## Karar kuralı

Bir değişiklik çökme sıklığını **belirgin şekilde** azaltmıyorsa **geri
alınır.** Görünümü ya da sesi bedava bozmuyoruz. Bu gece iki değişiklik
tam bu yüzden geri alındı ya da sorgulandı.
