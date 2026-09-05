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
