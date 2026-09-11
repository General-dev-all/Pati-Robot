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

Bu işin güncel belirtisi pilde konuşurken reset. Geçmişte USB'de de
ayrı PSRAM/bekçi arızaları görüldü; her reset aynı kök nedene bağlanmaz.

---

## 🔴 Kullanıcının öncelik sırası — pazarlık burada biter

Feda etme sırası, en önemliden en feda edilebilire:

1. **Çökmeme** — her şeyin üstünde
2. **Ses seviyesi** — `SES_PIL_TAVANI` **0.70'te sabit, DAHA AŞAĞI
   İNİLMİYOR.** Kullanıcı bunu iki kez açıkça söyledi. Pilde kısık ses
   gerçek bir kayıp: çocuk duyamıyorsa robot işe yaramıyor.
3. **Ekran parlaklığı** — pilde %35. Gerekirse biraz daha kısılabilir,
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

- **Dolu pilde de oluyor.** 4,13 V'tan başlıyor; çökmeler 3,8-3,9 V'ta da,
  4,0 V'ta da oluyor. "Pil bitmiş" değil.
- **İki kart da aynı.** İkinci kart kutudan yeni çıktı, aynı davranıyor.
  Bu, tek karta özgü kusuru daha az olası kılar; ortak tasarım sınırını
  veya yük altında besleme sorununu elemez.
- **Tetikleyici konuşma.** Her çökme `dusunuyor`/`konusuyor` anında.
  Yani amfi + telsiz + PSRAM aynı anda akım çekerken.
- **ESP32 sıcaklığı 55-64 °C ölçüldü.** Bu, PMIC veya regülatörün
  sıcaklığı değildir; onların ısıl korumasını tek başına elemez.
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
| Gözler konuşurken 10 → 5 fps | konuşma anındaki yük | 05.09: devreye giriş kusuru düzeltildi; kartta yeniden ölçülecek |
| Göz parlama katmanı 3 → 1 (pilde) | çizim maliyetinin **%81'i** bu katmanlarda (ölçülmüş) | 🔄 sınanmadı |
| Ekran %45 → %35 | taban akım | Kodda uygulanıyor; pil etkisi doğrulanmadı |
| Wifi verici 20 → 15 dBm (pilde) | telsiz gönderirken 250-350 mA | ❌ **geri alındı** — menzil çöktü (3/4 → 1/4), "modemin dibinde bile zor çekiyor" |
| Ses tavanı 0.70 → **0.60** | hoparlör yükü | Resetin bittiği doğrulanmadı; 05.09'da güncel 0.70 alt sınırı geri getirildi |
| Flash 80 → 40 MHz | PSRAM'den sonraki ikinci saat | Kodda ve yerel sdkconfig'de 40 MHz; pil etkisi doğrulanmadı |
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

**Sınır:** 2,44 V seçilen kademenin nominal eşiğidir; rayın gerçek
minimum gerilimi osiloskopla ölçülmedi. Brownout kaydı besleme
düşmesini bildirir. 3,3 V beslemede bu küçük bir dalgalanma değil, ciddi bir
çöküş. Yani sorun ölçüm eşiğinde değil, gerçekten güçte — ve yazılım
tarafında aranacak şey "hangi kod çöktürüyor" değil, **"hangi kod
akım çekiyor"**.

---

## Denenmemiş adaylar

**Wifi verici gücü:** 15 dBm denemesi menzil kaybı nedeniyle geri
alındı. Pati'nin anlık radyo akımı ölçülmedi; genel veri sayfası
sayıları cihaz ölçümü değildir. Yeni ölçüm olmadan tekrar kısılmıyor.

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

**Ekranı düşük pilde ayrıca kısmak.** Genel pil parlaklığı %35;
yüzdeye bağlı ek kademe yapılmadı. Kullanıcının
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
**`cokme_mv`** (son arızadan sonraki açılışta ölçülen pil gerilimi;
çökme anının minimumu değildir). `cokme_mv_ani=yeniden_acilis` bu
anlamı açıklar. Alan adı eski panel araçları için korunmuştur.

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


## 05.09.2026 — konuşma yolunun kaynak incelemesi

**Durum: yazılım kusurları düzeltildi; pilde resetin bittiği henüz
kanıtlanmadı.** Cihaz açıldıktan sonra panelden sürüm 3.0.9, pil kaynağı ve 0.60
ses sınırı doğrulandı. Konuşma denemesinde reset sayısı 14 → 17 oldu;
yeni açılışlar `brownout` bildirdi. Ham zamanlı kayıt depoya girmeyen
`prototype/olcumler/pil-2026-09-05-once.jsonl` dosyasında.
14 sayısı ilk panel okumasından, sürekli kayıt ise 15'ten başlıyor.

### Kanıtlanan kusur: konuşma tasarrufu atlanıyordu

`gemini_live_client.cpp` ilk ses paketinden **önce**
`StateChanged(Speaking)` üretiyor. `pati_sohbet.cpp` bu olayda
`g_konusuyor=true` yapıyor; ardından ses paketinde gözleri değiştiren
`if (!g_konusuyor)` çalışmıyor. Gözler düşünme ifadesinde kalabiliyor.
Eski 5 fps kararı ise yalnızca ifade adı `konusuyor` ise geçerliydi.
Yüz aracı başka ifade seçtiğinde de tasarruf kayboluyordu.

Düzeltme: yüz geçişi durum olayında yapılır. Güç kararı ayrıca ses
sürücüsünün bildirdiği DMA çalma penceresine bağlandı. Her 512 örneklik
bloktan önce ve yazımdan sonra kilitsiz bir 32 bit damga güncellenir.
Pencere, gerçekten ayrılan DMA kapasitesinin yukarı yuvarlanmış
süresidir (16 × 1024 / 48000 → 342 ms). Tampon dolu değilse bu süre
fazladan tasarruf bırakabilir; gerçek ses bitiş ölçümü değildir.
Sunucu üretimi bitirse veya yüz değişse de kalan ses korunur.

Panelde `guc.goz_fps` artık aynı etkin hedefi gösterir: pilde ses
penceresinde 5, sessizken 10, USB'de 20. Önceden konuşurken de 10
gösteriyordu. Bu hedef FPS'tir; ölçülen kare sayısı değildir.

### Ses yolundaki I2C kaldırıldı

`hoparlor_yaz → etkin_seviye → guc_kaynak → vin_mv` zinciri eskiden
saniyede bir ses görevinden I2C okurdu. Sürücü bir register için üç
kez 100 ms bekleyebilir; VIN iki ayrı register olduğundan hata yolunda
600 ms artı tekrar araları mümkündü. Bunun brownout nedeni olduğu
kanıtlanmadı, ancak ses yolunu bloke edebilen somut bir kusurdu.

VIN ve VBAT açılışta ve mevcut güç gözcüsünde iki saniyede bir alınır.
Ses ve panel aynı atomik örnekleri okur. VIN okuma hatası eski USB
kararını korumaz; gözcü dört saniye örnek üretemezse de karar bilinmiyor
olur ve pil sınırı uygulanır. Kablo geçişi iki saniyeye kadar geç fark edilebilir;
ölçüme başlamadan kaynak alanının `pil` olduğunu doğrula.

### Elenen iddialar ve açık kalanlar

- `cokme_mv`, `cokme_say()` içinde yeni açılışta okunur. Önceki
  belgelerdeki “çökme anındaki gerilim” yorumu yanlıştı. Gerçek
  geçici düşüşü bu alanla veya iki saniyelik panel yoklamasıyla ölçemeyiz.
- Flash ve PSRAM yerel yapılandırmada da 40 MHz; “defaults uygulanmadı”
  bu yerel derleme için geçerli değil. Cihazdaki sürüm ayrıca okunmalı.
- Ses yeniden örnekleyicisinde bu incelemede bir taşma kusuru bulunmadı;
  süre, perde, parça sınırı ve uzun akış testleri geçti. 1.30× değişmedi.
- Konuşma ses paketinin normal işleme yolunda NVS yazısı yok. Kopma,
  uyku veya kullanıcı ayarı ayrı yollardır; bunlar normal ses paketiyle
  karıştırılmamalı.
- Pil sınırı 0.70 geri getirildi. Geçici 0.60 sürümü de resetin bittiğini
  kanıtlamadı. Bu geri dönüş bir güç iyileştirmesi iddiası değildir.

Üretici pilde yüksek sesin reset yapabildiğini açıkça belirtiyor:
[StickS3 ürün belgesi](https://docs.m5stack.com/en/core/StickS3).
Ancak [M5Unified Speaker_Class](https://github.com/m5stack/M5Unified/blob/master/src/utility/Speaker_Class.cpp)
ana ve kanal sesini karesel ölçekler; Pati PCM'i doğrusal çarpar.
Dolayısıyla üreticinin %75 önerisini Pati'nin 0.70'iyle eşitlemek
ve bu sayıya “garantili güvenli eşik” demek doğru değildir.

### Kartta kabul ölçümü

1. Önce çalışan sürüm, kaynak, `cokme`, `acilis` ve pil mV kaydedilir.
2. Düzeltme yüklendikten sonra kaynak `pil` olmalı. Normal sohbet
   sırasında `goz_fps=5`, sessizlikte `10` görülebilmeli.
3. En az 10 dakika aynı konumda, benzer pil aralığında sohbet edilir;
   sonra daha uzun ve daha düşük pil seviyesinde tekrar edilir.
4. Sayaç artarsa yeniden açılıştaki `acilis` kaydı alınır. Brownout
   sürüyorsa besleme sorunu çözülmüş değildir. Panic/bekçi ise yazılım
   hata yolu ayrı incelenir. Tek temiz koşu bütün pil ömrü garantisi olmaz.
5. Sonuç alınmadan bu değişiklik “pilde çökme çözüldü” diye yayımlanmaz.

### İlk kart denemesi başarısız: 3.0.10

USB ile yüklenen 3.0.10'da panel konuşurken 5 fps, sessizken 10 fps
bildirdi; buna rağmen 09:25:41–09:30:27 pil aralığında sayaç 17'den
22'ye çıktı. Beş yeni açılış da `brownout` bildirdi. İlk yeni açılış
09:26:52'de görüldü. Kaydedilen pil örneklerinin en düşüğü 3590 mV;
bu sayı ani gerilim çöküşünün minimumu değildir. Kayıt:
`prototype/olcumler/pil-2026-09-05-sonra.jsonl` (yerel, depoya girmez).
İfade ve I2C kusurları gerçekti, fakat bunları gidermek reseti çözmedi.

### İkinci aday: AW8737A'nın kendi çıkış sınırlaması

[K150 V0.6 şeması](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/K150_Stick_S3_PRJ_V0.6_20251111_2025_11_17_16_10_24.pdf)
sayfa 3'te U19 **AW8737A** ve beslemesi **VBUS_L0**. Önceki
“hoparlör amfisi L3B'den beslenir” ifadesi yanlıştı; L3B ses kodeğini
besler. Amfinin SHDN girişi M5PM1 PYG3'e bağlıdır.

[AW8737A veri sayfası](https://doc.awinic.com/doc/20230609wm/ba30d80d-55b2-46f8-8f8a-505cd74a8826.pdf)
sayfa 8, 19 ve 20: SHDN'yi yalnızca yükseltmek Mode1'i seçer;
8 ohm yükte nominal NCN sınırı 1,2 W'tır. Mode4'te bu değer 0,6 W'tır.
Küçük sinyal kazancı kipler arasında değişmez. Bunlar veri sayfası
değerleridir; kartta watt veya darbe gerilimi ölçülmedi. PCM çarpanı
0.70 ile amfinin 0,6 W sınırlaması farklı şeylerdir. Eski yazılım
yalnızca SHDN'yi yükseltiyordu; tepe seslerde bu daha yüksek güç
talebine yol açabilir. Resetin ana nedeni olduğu henüz kanıtlanmadı.

3.0.11 adayı, SHDN'yi 20 ms düşük tutup Mode4'ü M5PM1'in darbe
üreteciyle seçer. I2C'den mikro saniyelik darbeler üretmeye çalışmaz.
[M5PM1 sürücüsünün](https://github.com/m5stack/M5PM1/blob/main/src/M5PM1.cpp)
`setAw8737aMode` yoluyla aynı NUM=3, GPIO=3, REFRESH komutunu kullanır.
Register geri okunamaz veya uyuşmazsa hata döner; yüksek güç kipine
sessiz bir geri dönüş yoktur. Panelde `amfi_kipi=4` komut ayarının
geri okunduğunu gösterir, analog gücün ölçüldüğünü değil.

Ayar USB'de de aynıdır; kaynak geçişinde ses döngüsüne I2C eklenmez.
Yeniden örnekleme 1.30× ve pil PCM sınırı 0.70 korunur; yüksek ses
tepelerinde sıkıştırma duyulabilir.

Kartta 3.0.11 ve `amfi_kipi=4` geri okuması doğrulandı. İlk kayıtta
09:44:22–09:49:39 pil aralığında sayaç 22'de kaldı; sonraki kontrolde
24 ve son açılış `brownout` görüldü. Aradaki iki resetin zamanı kayıt
dışında kaldığı için sıklık karşılaştırması yapılamaz. Reset sorunu
çözülmüş sayılmaz.

Kullanıcı daha sonra sessizlik bildirdi. USB'deki seri kayıtta mikrofon
ve Gemini ses çözümü çalışıyordu; kullanıcı önce USB'de, ardından pilde
ses geldiğini doğruladı. Bu arada firmware değişmedi. Sessizliğin
nedenini amfiye bağlayan kanıt yok. 14:34 kontrolünde kaynak pil,
sayaç 24; yeni koşu `prototype/olcumler/pil-2026-09-05-sessizlik.jsonl`
dosyasına kaydediliyor. Kısa süreli ses dönüşü reset çözümü değildir.

## 09–10.09.2026 — USB bağlıyken konuşmada brownout

Kullanıcı pilde resetlerin çok azaldığını, bilgisayar USB'sinde ise
sıklaştığını bildirdi. Motor/servo bağlı olmadığını doğruladı.
Seri kayıtta çalışan sürüm 3.5.0 ve arızalı açılış sayısı 48;
reset nedeni brownout. Kullanıcının yan tuşla yaptığı ayrı kapanma
arıza sayılmadı. Sonraki ağ kayıtlarında sayaç 49 → 50 oldu.
Kaynak USB; açılışta VIN 4942 mV, ses tavanı 1.00. Bu VIN değeri
reset anındaki minimum değildir.

USB'de pilin 0.70 sınırı kalkıyor, ekran %100 ve 20 fps oluyor.
Panelden 0.70 isteği kabul edildi ve hemen geri okundu; sonra reset
oldu ve ayar 1.00'a döndü. Ses seçimi yalnız RAM'de tutuluyor.
Bu kısa deneme, kesintisiz ve doğrulanmış uzun bir 0.70 A/B koşusu
değildir; yalnız ses kısmanın yeterli olduğu kanıtlanmadı.

Ham kayıtlar depoya girmeyen prototype/olcumler altında:
usb-2026-09-09-reset.log ve usb-ses070-2026-09-10.jsonl.
Google TLS zaman aşımı da görüldü; brownout ile karıştırılmamalı.
Sıradaki ayrım aynı kabloyla bilgisayar portu / şarj adaptörü;
sonra gerekirse USB'deki ekran yükü ayrı sınanacak. Henüz düzeltildi
diye bir sonuç yok.

3.5.1 test adayı: USB'de de ekran %35, gözler 10/5 fps ve ses tavanı
0.70. Paneldeki ses_tavani doğrudan ses sürücüsünün kullandığı işlevden
okunur; USB var diye sınırsız güç varsayılmaz. Kaynak USB olarak kalır;
şarj göstergesi ve OTA korumaları değiştirilmedi. USB portu/adaptör
karşılaştırmasını kullanıcı istemedi; bu aday yükleri birlikte azaltır,
etkili olsa bile tek yükün kök neden olduğunu kanıtlamaz. Kart testi
sonucu aşağıda. Yeniden örnekleme ve Puck 1.30× değişmedi.

Sonuç: 3.5.1 kartta USB, 0.70 tavan ve konuşurken 5 fps olarak
doğrulandı. 00:04:42 başlangıcında sayaç 51; 00:06:38'de 52 ve
brownout görüldü. Kayıt: usb-tasarruf-2026-09-10.jsonl. Profil reseti
durdurmadı, belirgin kazanç kanıtlanmadı; kaynak değişikliği geri
alındı, yayımlanmadı. Kablo/port/şarj devresi karşılaştırması yapılmadan
bu parçalar arasında kök neden ayrımı yapılamıyor.

### Amfi kapalı ayrıştırma denemesi

10.09.2026 00:14:14–00:18:13, aynı USB bağlantısında yalnız
hoparlör amfisi açılışta kapalı tutuldu. Mikrofon, ağ, yeniden örnekleme,
I2S ve ekran çalışmaya devam etti; panel amfi=0, göz=20 fps,
ses tavanı=1.00 gösterdi. Kullanıcı 20–30 saniye arayla soru sormayı
kabul etti. 117 ağ örneğinde sayaç 54 → 54; 12 örnekte ifade
`konusuyor` idi. Örnekler cevap sayısı değildir; kısa cevaplar iki
saniyelik yoklamalar arasına düşebilir. Kayıt:
`prototype/olcumler/usb-amfi-kapali-2026-09-10.jsonl`.

Bu kısa koşuda reset görülmemesi, amfi/hoparlör güç yükü ihtimalini
destekler; arıza aralıklı olduğundan kök neden kanıtı değildir.
Amfi arızası, besleme zayıflığı ve kısa akım tepeleri ayrılmadı.
Geçici sessiz sürüm yayımlanmadı; resmi 3.5.0 imajı USB'den geri
yüklendi ve panelden doğrulandı (amfi=4, ses=1.00, Puck 1.30×).
İlk başarılı geri okumada sayaç 55 ve brownout; 00:20:08'de sayaç
56 idi. Kullanıcı sesin geldiğini ve konuşurken üç çökme gördüğünü
bildirdi. Bu bildirim, kayıtta sayılan iki artıştan ayrı tutulmalı;
geri yükleme ile ilk erişim arasındaki olayların zamanı bilinmiyor.
Normal sürüme dönüş kaydı:
`prototype/olcumler/usb-amfi-acik-donus-2026-09-10.jsonl`.
00:21:52'de konuşma örneğinden sonra 00:21:57'de sayaç 57;
00:22:18'de ayrı doğrulamada 58 ve brownout görüldü. Böylece normal
sürüme dönüşten sonraki toplam artış dört oldu (54 → 58).
Son doğrulama: `usb-amfi-geri-yukleme-dogrulama-2026-09-10.json`.
Dönüş izleme dosyasında sürüm alanı yanlış JSON yolundan okunduğu
için null; sürüm 3.5.0 ayrı doğrulamada `guncelleme.su_anki` alanından
okundu. Ham kayıttaki null değerler sonradan doldurulmadı.

AW8737A veri sayfası V1.2 s.8 ve s.16 yeniden kontrol edildi:
Mode4, 8 ohm + 33 µH test yükünde nominal 0.6 W ile en düşük
NCN güç seçeneğidir; dört kipte de küçük sinyal kazancı aynıdır.
NCN kazancının 13.5 dB azalması için belirtilen süre 40 ms'dir.
Bu yüzden 0.6 W seçimi anlık tepe akımını kesin sınırlıyor varsayımı
yanlıştır. Ancak bu kartta böyle bir tepe henüz ölçülmedi. Belgelenen
dört kip arasında daha düşük güç ya da ayarlanabilir saldırı süresi
yoktur. PMIC kip geri okuması fiziksel SHDN darbe ölçümü değildir.
Kaynak: https://doc.awinic.com/doc/20230609wm/ba30d80d-55b2-46f8-8f8a-505cd74a8826.pdf

### Bağlantı toparlamasında doğrulanan ayrı kusur

`usb-2026-09-09-reset.log` içinde 128223 ve 138403 ms'de yeniden
bağlanma girişimleri var. Her ikisi 22 ms sonra başarılı yazılıyor;
arada TLS zaman aşımı geliyor ve sonraki girişim yine deneme 1.
İstemcinin `start()` işlevi WebSocket görevini başlatıp hemen dönüyor;
sunucunun hazır oluşu daha sonraki `setupComplete` ile bildiriliyor.
Toparlama kodu erken başarıda sayacı sıfırladığı için asenkron bağlantı
hatalarında artan bekleme çalışmıyordu. Başarı artık güncel istemcinin
Listening durumunda doğrulanıyor; start sırasında gelen hata bayrağı
da dönüşte silinmiyor. Uzun kesintideki sınırsız bit kaydırma düzeltildi.

`firmware/test/yeniden_baglan_test.py` gerçek toparlama gövdesini sahte
istemciyle derleyip asenkron başarısızlık, 2/4/8/16/30 saniyelik bekleme,
eski hazır olayı, anında hata ve sayaç sınırlarını doğruluyor.
Firmware derlemesi ve dört mevcut konak testi geçti. Bu yazılım
kusuru, brownout'un kök nedeni olarak kanıtlanmadı. Derlenen düzeltme
ilk aşamada cihaza yüklenmedi; çalışan kart 3.5.0 olarak bırakıldı.

Kullanıcının yükleme isteğiyle 10.09.2026'da 3.5.1 USB'den yüklendi.
İlk yükleme kontrolünde amfi=0 görüldü: sessiz testten geri kopyalanan
kaynağın eski zaman damgası nedeniyle artımlı derleme eski nesneyi
kullanmıştı. `idf.py clean build` sonrası yeniden yüklendi; flash özeti
doğrulandı. 00:35:02 panel doğrulaması: sürüm 3.5.1, amfi=4,
ses=1.00, Puck 1.30×, göz=20 fps, sayaç=62, açılış=diger.
Kayıt: `prototype/olcumler/usb-3.5.1-baglanti-duzeltme-yukleme.json`.
Bu açılış kontrolü uzun süreli reset testinin yerine geçmez.

00:48:13 kontrolünde 3.5.1, USB, amfi=4, ses=1.00, göz=20 fps;
sayaç 67, son açılış brownout. 00:35:02'deki 62 başlangıcından
yaklaşık 13 dakika 12 saniyede beş arızalı açılış birikmiş.
Kullanıcı belirgin azalma bildirdi; bu aralıkta etkin konuşma süresi
ve bağlantı yenileme sayısı ölçülmediği için önceki koşuyla kontrollü
sıklık karşılaştırması yapılamaz. Bağlantı kusuru düzeltildi, fakat
brownout sürüyor; ortak kök neden veya tetikleyici bağı kanıtlanmadı.

## 3.5.2 — şarjda kullanıcı tarafından seçilen tasarruf

Şarjda gözler sessizken 20, gerçek ses/DMA penceresi boyunca 10 FPS;
pildeki 10/5 FPS korunuyor. İki kaynakta da parlama tek kat, cam
parlaması kapalı, ekran %35 ve etkin ses tavanı 0.70. Daha düşük ses
seçimi korunur. Panel tavanı doğrudan ses sürücüsünden alır. Puck 1.30×,
Wi-Fi gücü ve uyku ayarı değişmedi. Flash zaten 40 MHz; yeni kazanç
olarak sayılmaz. Kaynak ayrımı korunur; şarj göstergesi ve OTA pil
korumaları aynı kalır. Kullanıcı güncellemeyi panelden yükleyecek.
Bu profilin resetlere etkisi henüz kartta ölçülmedi.

## 11.09.2026 — 3,7 dakikalık kesintisiz kayıt (USB, konuşurken)

3.5.3 yüklü, USB takılı, kullanıcı konuşurken panelden 10 saniyede bir
`api/durum` çekildi. Ham kayıt: `usb-izleme.jsonl` (23 başarılı örnek,
22:06:44 → 22:10:28; ardından cihaz kapatıldığı için ulaşılamadı).

| Saat | pil_mv | vin_mv | çökme | cokme_mv | °C | RSSI |
|---|---|---|---|---|---|---|
| 22:06:44 | 3740 | 5002 | 19 | 3736 | 53,5 | **−70** |
| 22:07:04 | 3714 | 4932 | 19 | 3736 | 52,5 | **−69** |
| 22:07:14 | 3744 | 4992 | 19 | 3736 | 53,5 | **−71** |
| 22:07:24 | 3744 | 4988 | 20 | 3742 | 53,5 | **−49** |
| 22:07:44 | 3746 | 4984 | 21 | 3746 | 54,5 | −57 |
| 22:07:55 | 3752 | 4992 | 22 | 3750 | 54,5 | −54 |
| 22:08:07 | 3750 | 4996 | 23 | 3750 | 53,5 | −60 |
| 22:08:27 | 3752 | 4986 | 24 | 3750 | 54,5 | −53 |
| 22:08:37 | 3754 | 4990 | **25** | 3754 | 54,5 | −51 |
| 22:09:27 | 3758 | 4992 | 25 | 3754 | 54,5 | −52 |
| 22:10:28 | 3770 | 4992 | **25** | 3754 | 54,5 | −52 |

### ⚠️ Geri çekilen okuma: "gerilim düz, hiç şarj olmuyor"

İlk dört örneğe (3740 → 3714 mV) bakılıp *"pil bir dakikada hiç
yükselmedi, USB çalışırken hiç şarj etmiyor"* denmişti. **Yanlıştı.**
Tam pencerede gerilim 3740 → 3770 mV yükseliyor: yaklaşık 8 mV/dakika.
Yani çalışırken şarj **oluyor**, sadece yük karşısında çok yavaş. İlk
dakikadaki düşüş bir eğilim değil, anlık örneğin yükle salınmasıydı.

Ders eskisiyle aynı: **aralıklı bir sistemde bir dakikalık pencere bir
eğilim göstermez.**

### ⚠️ `pil_mv` ile `pil_yuzde` aynı büyüklük değil

Panelin iki alanı farklı şeyler veriyor ve bu koşuda karıştırıldı:

| Alan | Ne | Kaynak |
|---|---|---|
| `guc.pil_mv` | **anlık** son örnek, ham | `g_pil_mv` (`pati_guc.cpp` · `pil_mv()`) |
| `guc.pil_yuzde` | son 30 saniyenin **TEPE** değeri, eğriden çevrilmiş | `g_pencere[15]` · `pil_yuzde()` |

Kayıttaki iki tuhaflığı tam olarak bu açıklıyor:

- **22:08:07'de `pil_yuzde` = −1.** Bu bir hata değil, belgeli nöbetçi
  değer: `if (tepe <= 0) return -1` — pencere boş. Pencere kaynak
  değişince sıfırlanıyor ve o örnekte çökme sayacı 22 → 23 oldu, yani
  cihaz yeni açılmıştı.
- **22:09:27'de `pil_mv` 3758 iken `pil_yuzde` %46.** Eğriden 3758 mV
  doğrudan %32 verir; %46 için tepenin ~3825 mV olması gerekir. Yani
  pencerede, anlık örneklerin 67 mV üstünde bir şarj devresi okuması
  kalmıştı.

🔴 **Sonuç: tablodaki `pil_mv` sütunu tepe değil, 10 saniyede bir
alınmış anlık örnektir.** Konuşma sırasındaki gerçek milisaniyelik
çöküntüler bu örnekleme aralığıyla **hiç görülemez**. Brownout'un
gerçekleştiği gerilim bu sütunda yazmıyor ve `cokme_mv` de onu
vermiyor — o da aynı anlık okumadan geliyor.

Bu belge (§"Elenen iddialar", 05.09) ve `TESHIS.md` bunu zaten doğru
yazıyordu. **Geride kalan tek yanlış yer kaynak koddu:** `pati_guc.cpp`
içinde `cokme_mv` yazılırken duran yorum hâlâ *"düşük pil hipotezinin
sınandığı yer burası"* diyordu. 11.09'da düzeltildi — `app_main.cpp`
ile `pati_ses.hpp` arasında çıkan çelişkinin aynısı: belge geri
çekiliyor, kaynaktaki kopya kalıyor.

### 🔴 Asıl bulgu: çökmeler durdu ve sinyalle örtüşüyor

- 22:06:44 → 22:08:37 arası **113 saniyede 6 çökme** (19 → 25), yani
  ortalama 19 saniyede bir.
- 22:08:37 → 22:10:28 arası **111 saniyede 0 çökme.** Sayaç 25'te kaldı.
- Aynı anda RSSI **−70/−71'den −49/−52'ye** çıktı (22:07:24'te sıçradı).
  `cubuk()`'a göre bu 2 çubuktan 4 çubuğa geçiş.

Pil gerilimi iki yarı arasında neredeyse aynı (3754 → 3770 mV, 16 mV).
Sıcaklık aynı (53,5–54,5 °C ikisinde de). vin aynı (~4990 mV, tek
istisna 4932'lik bir örnek). **Değişen tek ölçülen büyüklük sinyal.**

Bu, bu belgenin kendi tablosuyla tutarlı: satır 83'te wifi vericisi
20 → 15 dBm denemesi *menzil çöktüğü için* geri alınmış ("modemin
dibinde bile zor çekiyor") ve aynı satırda telsizin gönderirken
250–350 mA çektiği yazılı. Yani Pati zaten menzilinin sınırında
çalışıyor; zayıf sinyalde verici tam güçte kalıp yeniden gönderim
yapıyor ve bu darbeler yarı boş bir hücrenin üstüne biniyor.
`WIFI_PS_NONE` (radyo hiç uyumuyor) bu tabloyu ağırlaştırıyor —
`pati_ag.cpp` yorumu bedelini zaten "~30 mA fazla ortalama" diye
yazmış, ama tepe akımı ayrı bir şey.

### Bu KANIT DEĞİL — neyin karıştığı

1. **Kullanıcının konuşmaya devam edip etmediği bilinmiyor.** Sessiz
   geçen iki dakika, çökmelerin durmasını tek başına açıklar.
2. RSSI 22:07:24'te düzeldi ama çökmeler **73 saniye daha sürdü.**
   Sebep-sonuç olsaydı daha keskin bir kesme beklenirdi (ya da
   yeniden bağlanma/kota sarmalının ataleti sayılmalı).
3. n = 1. Tek geçiş, tek pencere.
4. Hücre %30'da; bu koşulda alınan hiçbir ölçüm temiz değil.

### Sıradaki ayrım — pil dolduktan sonra

Şarj bitince iki soru **ayrı ayrı** ölçülecek, ikisi de kod
değişikliği gerektirmiyor:

1. **Dolu pille çökme sıklığı.** Hücre ~4,1 V iken aynı konuşma.
   Çökme kalmazsa gerilim payı; sürerse gerilim ana sebep değil.
2. **Sinyal A/B.** Aynı pil seviyesinde, önce modemin yanında sonra
   uzağında birer koşu. Ölçülen: dakikada çökme ve RSSI.

⚠️ İzleme betiği bundan sonra `ag.tx_ceyrek_dbm` ve `ag.tasarruf`
alanlarını da kaydetmeli; paneldeki JSON'da ikisi de zaten var
(`pati_panel.cpp`) ve bu koşuda kaydedilmedikleri için verici
gücünün gerçekten tavanda olup olmadığı söylenemiyor.

⚠️ **Önce ölçüm, sonra kod.** Wifi tarafında akla gelen iki kol
(verici gücünü kısmak, `WIFI_PS_MIN_MODEM`'e dönmek) ikisi de daha
önce bilinçli olarak reddedilmişti: birincisi menzili öldürdü,
ikincisi sesin ortasında boşluk yapıyor. Ölçüm olmadan ikisi de
açılmaz.

## 3.5.4 — pil/şarj profil ayrımı kaldırıldı

Kullanıcının kararı (11.09.2026): *"pil modu şarj modu ayrımını
kaldıralım, mod olmasın, her şey pildeki gibi çalışsın, ister şarjda
olsun ister pilde."*

### Ayrımın dayandığı varsayım ölçümle çöktü

Gerekçe *"USB'de akım bol, sınırları gevşetebiliriz"* idi. 11.09.2026
akşamı aynı kartta sırayla ölçüldü — **üçünde de konuşurken brownout**:

| Koşul | Pil | Sonuç |
|---|---|---|
| USB (bilgisayar portu) | **4048 mV** (neredeyse dolu) | çöktü, `cokme_mv` 4048 |
| Pil, gövde çıkarık | 3950 mV | çöktü, 23:09:28'de canlı yakalandı |
| Tam silme + temiz yükleme sonrası | 3950 mV | yine çöktü |

🔴 **Dolu pille çökme, "hücre düşük olduğu için" açıklamasını bitiriyor.**
USB fazladan pay vermiyor; yalnızca yazılım sınırlarını kaldırıyordu.

### Ne değişti

Tek gerçek fark göz kare hızıydı — ses tavanı 3.5.2'de zaten iki
kaynakta da 0.70'e eşitlenmişti.

| | Eskiden şarjda | Eskiden pilde | **3.5.4** |
|---|---|---|---|
| Sessizken | 20 FPS | 10 FPS | **10 FPS** |
| Konuşurken | 10 FPS | 5 FPS | **5 FPS** |

`gozler_pil_kipi(bool)` **kaldırıldı** — bir profil seçicisi bırakmak,
ileride birinin "şarjda hızlandıralım" diye geri açmasına davetiye
olurdu. Kare hızının artık güç kaynağı diye bir girdisi yok.

Konak testi bunu koruyor: 200 adımlık tarama, ses penceresi açık ya da
kapalı, saat nerede olursa olsun çıkan değerin **yalnızca 10 ya da 5**
olabildiğini doğruluyor. Eskiden USB'de 20 çıkıyordu; o sayının bir daha
görünmemesi gerekiyor.

### Yan bulgu: log kodun yaptığını değil eski hâlini anlatıyordu

`app_main.cpp`'deki güç kaynağı logu ses tavanını
`pilde ? min(seviye, TAVAN) : seviye` diye **kendisi hesaplıyordu** ve
3.5.2'den beri yanlıştı: tavan o sürümde iki kaynakta da geçerli oldu
ama log USB'de hâlâ sınırsız seviye basıyordu. Artık fiilen uygulanan
değeri kaynağından okuyor (`ses_etkin_seviye()`).

⚠️ Teşhiste en pahalı yanlış türü bu: log doğru görünüyor ama ölçtüğü
şey kodun yaptığı şey değil.

### ⚠️ Bu değişikliğin bu akşamki çökmeleri durdurması BEKLENMİYOR

Kullanıcı ölçüm sırasında **pildeydi**, yani 10/5 profili zaten
çalışıyordu. Bu sürüm yalnızca **şarjdaki** yükü pildekine indiriyor.
Kazanç varsa USB'de görünür ve henüz ölçülmedi.

Değişikliğin gerekçesi "çökmeyi çözer" değil: **kaynağa göre fazladan
yük vermenin dayanağı kalmadı.**

### 3.5.4'ün ikinci yarısı — konuşma başı rampası, parlaklık, CPU

Kullanıcının isteği: *"kaliteyi bozmamaya çalış, bir de ekran
parlaklığından da tasarruf edebiliriz, CPU'dan tasarrufun yanında."*

#### 🔴 Konuşma başında yumuşak başlangıç

Amfi açılışta bir kez açılıyor ve açık kalıyor; değişen tek şey çıkış
genliği ve o genlik **sessizlikten tam seviyeye bir anda** atlıyordu.
Ölçülen bütün brownout'lar tam o anda oldu.

Bu, kullanıcının motorlar için koyduğu kuralın aynısı — *"bir daha
motorları anlık %100'de yapma"* — ve gerekçesi de aynı fizik. Hoparlöre
hiç uygulanmamıştı.

| | Değer | Neden |
|---|---|---|
| Rampa süresi | **80 ms** | elektriksel taraf için fazlasıyla yeterli; bir hece 150–250 ms olduğu için duyulmuyor |
| Yeni cümle eşiği | 250 ms sessizlik | cümle içi paketler çok daha sık geliyor |

⚠️ **Rampa çıkış örneği başına ilerliyor, çağrı başına değil.** Çağrı
başına olsaydı aynı ses farklı dilim boyutlarında farklı çıkardı
(bu dosyanın 23.08.2026 dersi) ve blok sınırlarında kazanç sıçrayıp
**tık sesi** yapardı.

Konak testi yedi şeyi birden koruyor: rampasız yolun **birebir**
korunduğunu, sıfırdan başladığını, 80 ms boyunca bastırıldığını,
sonrasında **tam seviyeye döndüğünü** (3842 örnek sonrası birebir aynı),
`sifirla()`'nın rampayı başa aldığını, ve 🔴 **dilim boyutundan
bağımsızlığın rampayla da durduğunu.**

⚠️ **Bu bir tahmin ve henüz ölçülmedi.** Basamak değil de sürekli akım
çökertiyorsa rampa hiçbir şey yapmaz.

#### Ekran parlaklığı 0.35 → 0.25

Kademeli gidiliyor: 0.45 → 0.35 → 0.25, taban 0.15 (orası "kısık ekran"
değil "kapalı ekran" gibi görünüyor). Sürekli bir yük, yani taban akımı
düşürüyor; **tepe akımı düşürdüğü iddia edilmiyor.**

#### CPU 240 → 160 MHz

240'ın gerekçesi ölçülmüştü (01.09.2026): kare 45–49 ms, **bütçe 50 ms**,
5 saniyede ~90 kare atlanıyor. O bütçe artık **100 ms** — profil ayrımı
kalkınca gözler her kaynakta 10 FPS'e indi. 45–49 ms oraya rahat sığıyor.

Üstelik çizim o günden beri ucuzladı: 11.09.2026 açılış kaydında 240
MHz'de kare **13,5 ms** (bütçe 50 ms, atlanan 0). 160 MHz'de kabaca
20 ms eder.

`sdkconfig` silinip `sdkconfig.defaults`'tan yeniden üretildi ve eskisiyle
karşılaştırıldı: **yalnızca CPU satırları farklı**, başka hiçbir ayar
kaymamış.

🔴 **Seste bozulma olursa ilk şüpheli budur.** Yeniden örnekleyici için
risk düşük (çıkış örneği başına birkaç float işlemi, 48 kHz'de bir
çekirdeğin binde birkaçı) ama TLS ve websocket yavaşlar. Geri alma:
`sdkconfig.defaults`'ta `_160` → `_240` ve `sdkconfig`'i **sil**.

#### ⚠️ Üçü aynı sürümde — ayrıştırma bedeli

Üç değişiklik tek sürümde gidiyor, yani kazanç görülürse **hangisinden
geldiği bilinemeyecek.** Kullanıcı üçünü birlikte istedi ve arıza
aralıklı olduğu için tek tek ölçmek saatler alırdı. Kazanç çıkarsa
ayrıştırma sonraya bırakılacak; çıkmazsa üçü de zaten elenmiş olur.

---

# 🔴 11.09.2026 — ÇÖKMELER ~15 KAT SEYRELDİ (durmadı). Ne yaptıysak burada.

**Bu bölüm gelecekte çökmeler geri gelirse okunacak yer.** Kullanıcının
isteğiyle yazıldı: *"gelecekte çökmeler gelirse bizi ne kurtarmıştı
bilelim."*

## Ölçülen sonuç

### ⚠️ ÖNCE BİR DÜZELTME — bu bölüm ilk yazıldığında "durdu" diyordu

İlk 8 dakikalık pencerede 0 çökme görüldü ve buraya *"çökmeler durdu"*
diye yazıldı. **Yanlıştı.** Sayaç izlenmeye devam edilince:

| Saat | Sayaç | |
|---|---|---|
| 23:38 | 8 | 3.5.4 yüklendi |
| 23:46 | 8 | 8 dakikalık ölçüm, **0 çökme** |
| 23:59 | **12** | sonraki 13 dakikada **4 çökme** |

Gerçek oran: **21 dakikada 4 çökme** ≈ 5 dakikada bir. Öncesi
**19 saniyede bir**di. Yani kabaca **15 kat seyrelme — ama sıfır değil.**

🔴 **Ders, bu belgenin kendi kuralının tekrarı:** 8 dakikalık temiz bir
pencere bu arızayı elemiyor. Daha önce de 2 dakikalık sessiz aralıklar
görülmüştü (22:08–22:10). *Aralıklı bir arızada tek pencere sonuç
değildir.*

⚠️ 23:46 sonrasında konuşma yoğunluğu ölçülmedi, yani iki dönem
**tam olarak** karşılaştırılabilir değil. Oran yine de büyüklük
mertebesi olarak anlamlı.

### İlk ölçüm penceresi (temsili değil, ama kayda geçsin)

3.5.4 yüklendikten sonra, **USB'de (bilgisayar portu), gövde çıkarık**,
kullanıcı aktif konuşurken:

| | Öncesi (3.5.3 ve altı) | **3.5.4** |
|---|---|---|
| Süre | 113 sn | **8 dakika** (23:38:57 → 23:46:56) |
| Çökme | **6** | **0** |
| Ulaşılamadı | sık | **0** |
| Aktif konuşma | vardı | 95 örneğin 22'sinde |
| RSSI | −70 … −86 (1-2 çubuk) | **ort. −59,5** (en iyi −51) |
| `vin` en düşük | — | **4630 mV** (260 mV sarkma) ve yine çökmedi |

Kullanıcının kendi ifadesi: *"epey bir fark etti, cihaz rahatladı,
wifi artık tam çekiyor, hatırlarsan tek diş çekiyordu. Pati artık anında
cevap veriyor, eskiden çoğu zaman geç cevap veriyordu bazen vermiyordu.
Ses olarak da hiçbir sorun yok, fark etmedim bile."*

## Aynı gün NE YAPILDI — sırayla

Sıra önemli, çünkü ikisi arasında ölçüm var ve **hangisinin çözdüğünü
ayırmaya yarıyor.**

### 1. Tam flash silme + temiz yükleme (3.5.3) — ❌ ÇÖZMEDİ

`erase_flash` + `fullclean build` + kabloyla yükleme. RF kalibrasyonu
(`nvs.net80211` · `cal_data`) dahil her şey sıfırlandı.

**Sonuç: RSSI hâlâ −73/−75, çökmeler sürdü** (23:06'da ölçüldü, dolu
pille 4048 mV brownout). Yani bozuk kalibrasyon ya da bozuk NVS
**değildi.**

⚠️ Bu adım öncesinde tam 8 MB flash yedeği alındı. Gemini anahtarı ve
wifi şifresi o yedekten kurtarıldı — `erase_flash` `anahtar` bölümünü de
siliyor. **Bir daha silmeden önce yine yedek alın.**

### 2. 3.5.4 — üç değişiklik birden — ✅ ÇÖZDÜ

| # | Değişiklik | Nerede |
|---|---|---|
| a | **Konuşma başında 80 ms rampa** | `pati_ses.cpp` · `SES_RAMPA_MS`, `pati_ornekleyici.hpp` · `rampa_kur` |
| b | **CPU 240 → 160 MHz** | `sdkconfig.defaults` |
| c | Pil/şarj profil ayrımı kalktı: gözler her kaynakta 10/5 FPS | `pati_gozler.cpp` · `KARE_ARALIK_MS` |
| d | Ekran parlaklığı 0.35 → 0.25 | `app_main.cpp` · `guc_kipi_uygula` |

## 🔴 HANGİSİ ÇÖZDÜ — BİLİNMİYOR

Üçü aynı sürümde gitti. Kullanıcı böyle istedi ve arıza aralıklı olduğu
için tek tek ölçmek saatler alırdı. **Ayrıştırma yapılmadı.**

Ama mantıkla bir kısmı elenebiliyor:

| Düzelen şey | Rampa yapmış olabilir mi | Kalan şüpheli |
|---|---|---|
| Wifi 1 çubuk → 4 çubuk | **HAYIR** — rampa telsize dokunmuyor | CPU/ekran gürültüsü, ya da dış etken |
| Cevapların hızlanması | **HAYIR** — o ağ gecikmesi | aynı |
| Çökmelerin durması | evet | ikisi de |

### İki hipotez, ikisi de açık

**Kullanıcının hipotezi:** *"çöküşler kelime başında oluyordu, bir anda
hoparlöre abanıp akımı fırlatıyordu."* Ölçümle uyumlu — üç çökmenin üçü
de `ifade konusuyor` anındaydı.

**Karşı gözlem:** kelime başı, her şeyin **aynı milisaniyede** tepe
yaptığı an — telsiz veri alıp onaylıyor (zayıf sinyalde tekrar tekrar),
yeniden örnekleyici çalışıyor, amfi tam güce geçiyor, (eskiden) gözler
USB hızında çiziliyor. Yani "kelime başında çöküyor", hoparlörün tek
suçlu olduğunu **kanıtlamıyor.**

**Muhtemel doğru:** tek sebep yoktu, **toplam** vardı. Hangi bileşeni
çıkarırsan tepe eşiğin altına iniyor. Üçü birden çıkarıldı.

## 🔴 ÇÖKMELER GERİ GELİRSE — sırayla bunlara bak

Özellikle **gövde geri takıldığında** beklenir: motorlar ve kablolar hem
gürültü hem akım ekliyor.

1. **Önce ölç, değiştirme.** `api/durum` → `guc.cokme`, `guc.acilis`,
   `guc.pil_mv`, `guc.vin_mv`, `ag.rssi_dbm`, `ifade`. 5 saniyede bir,
   en az 5 dakika. Tek ölçüm bu arızayı elemiyor — 2 dakikalık sessiz
   aralıklar normal.
2. **RSSI'ye bak.** −70'in altındaysa bağlantı zayıf demektir ve bu
   tek başına bir etken: zayıf sinyalde telsiz tam güçte kalıp yeniden
   gönderiyor, her gönderim ayrı bir akım darbesi (bu belgede satır 83:
   telsiz gönderirken 250-350 mA).
3. **Gövdeyi çıkarıp aynı ölçümü tekrarla.** Aynı yer, aynı besleme,
   yalnızca gövde değişsin. 11.09'da bu test **konum da değiştiği için
   bozuldu** — tekrarlanmadı.
4. **CPU'yu 240'a alıp RSSI'ye bak.** Düşerse gürültü hipotezi
   doğrulanır. `sdkconfig.defaults`'ta `_160` → `_240` **ve
   `sdkconfig`'i sil** (dosya varken defaults okunmuyor).
5. **Rampayı uzat.** `SES_RAMPA_MS` 80 → 120/150. Bedeli ilk hecenin
   duyulur şekilde şişmesi; kullanıcının şartı kaliteyi bozmamak.
6. **Besleme kaynağını ayır.** Duvar adaptörü ~4990 mV, bilgisayar
   portu ~4880 ve konuşurken 4630'a sarkıyor. Adaptör her zaman daha
   iyi.

### ⚠️ Bunlar ELENDİ — tekrar denemeye değmez

| Aday | Nasıl elendi |
|---|---|
| Düşük pil | USB'de **4048 mV** ile çöktü |
| Şarj devresi arızası | kapalıyken 10 dakikada 3770 → 3938 mV, normal |
| Bozuk NVS / ayar | tam silme sonrası yine çöktü |
| Bozuk RF kalibrasyonu | tam silme sonrası RSSI değişmedi (−73/−75) |
| Wifi verici gücünü kısmak | 02.09'da denendi, menzil çöktü, geri alındı |
| `cokme_mv` ile gerilim-çökme ilişkisi kurmak | o değer **açılışta** okunuyor, çökme anında değil |
