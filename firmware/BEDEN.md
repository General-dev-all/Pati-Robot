# Pati'nin bedeni

İki tekerlek, iki kol, kendi pili. StickS3 bedene takılınca Pati bunu
kendi anlıyor; takılı değilken bedenle ilgili hiçbir şey görünmüyor.

**Durum (06.09.2026):** algılama, panel kumandası ve **motorlar
çalışıyor** — motorlar 3.1.1 ile panelden sürülüyor. **Servolar açık iş**
(aşağıda ayrı bölüm). Ölü bölge hâlâ ölçülmedi ve öyle işaretli.
Bir sayı iddia ediliyorsa nereden geldiği yazılı.

📐 **Kabloyu takacak kişi için tek sayfa:
[`Pati_Tek_Bakis_Kablolama_Diyagrami.png`](Pati_Tek_Bakis_Kablolama_Diyagrami.png)** — pin haritası,
L9110 bağlantıları, ortak ekler ve ilk çalıştırma sırası tek bakışta.
Kablo renkleri gerçek montaja göre. Aşağıdaki metin onun gerekçesi.

⚠️ Diyagram başlığı **ÜST/ALT** diye konumluyor (cihazı elde tutarken
görünen hal); bu belge ve `pati_pinler.h` M5Stack'in tablosundaki gibi
**SOL/SAĞ** diyor. Aynı şey: M5Stack'in "sol" sütunu (tek numaralar)
diyagramda ALT sırada. Pin numaraları ikisinde de aynı — şüphede
kalırsan **pin numarasına** güven.

---

## 🔴 Tek pazarlıksız kural

> **AA pilin (+) ucu StickS3'e hiçbir şekilde gitmiyor.
> Gövdeden Stick'e giden sekiz kablonun hiçbiri 6 V taşımıyor.**

Sebebi Hat2-Bus'ın dizilişi: sinyal pinlerinin arasında **BAT** (lityum
hücrenin kendi ucu), **5V_IN**, **3V3_L2** ve **EXT_5V** duruyor. Oraya
6 V girerse hücre ve M5PM1 gider.

Bu bir uyarı değil, **pin seçiminin sebebi**. Kural böyle kurulunca bir
kablo yanlış pine kaysa bile en kötü ihtimal "motor sürekli dönüyor"
oluyor — yakmıyor.

Ayrı besleme ayrıca pazarlık konusu değil: 250 mAh'lik hücre
01.09.2026'da **yalnızca hoparlör akımıyla** brownout yaşadı
(`TESHIS.md`).

---

## Bağlantı

Kaynak: docs.m5stack.com/en/core/StickS3 → PinMap → Hat2-Bus
(06.09.2026). Pin numaraları `main/pati_pinler.h`'de, tek kaynak orası.

```
        SOL              SAĞ
  GND    1  ────────  2   G5     sol motor ileri   (L9110 A-1A)
  EXT_5V 3  ────────  4   G4     sol motor geri    (L9110 A-1B)
  Boot   5  ────────  6   G6     sağ motor ileri   (L9110 B-1A)
  G1     7  ────────  8   G7     sağ motor geri    (L9110 B-1B)
  G8     9  ──────── 10   G43
  BAT   11  ──────── 12   G44
  3V3_L2 13 ──────── 14   G2     beden algılama
  5V_IN 15  ──────── 16   G3

  pin 1  → AA (−)                toprak
  pin 7  → servo 1 sinyal        sol kol
  pin 9  → servo 2 sinyal        sağ kol
  pin 14 → AA (−), ikinci uç     beden algılama
```

Sıralamanın üç gerekçesi var:

1. **Motorlar hep sağ sütunda.** Sağ sütunun tamamı GPIO — orada tek bir
   güç pini yok. Motor kablosunun yanlışlıkla BAT'a düşmesi imkânsız.
2. **Servolar sol sütunda.** Bir servo sinyali kayıp BAT'a düşerse servo
   geçersiz darbe görür ve durur. Motor kablosu kaysaydı motor sürekli
   dönerdi — yani Pati masadan düşerdi. Riski, sonucu daha zararsız olan
   uca kaydırdık.
3. **G43 / G44 bilerek boş.** ESP32-S3'ün ROM UART0'ı orada ve her
   açılışta G43'ten önyükleyici çöpü çıkıyor. Motor girişine bağlı
   olsaydı Pati her sıfırlamada seğirirdi — üstelik Pati brownout
   yüzünden sık sıfırlanıyor (`PIL.md`).

G0 (Boot) ve G3 strapping pini olduğu için kullanılmıyor.

### Gövdenin içi

```
AA (+) ──┬── L9110 VCC
         ├── servo 1 kırmızı
         └── servo 2 kırmızı

AA (−) ──┬── L9110 GND
         ├── servo 1 kahve/siyah
         ├── servo 2 kahve/siyah
         ├── Stick Hat pin 1   (toprak)
         └── Stick Hat pin 14  (beden algılama)

L9110 MOTOR-A vidalı klemens ── sol motor
L9110 MOTOR-B vidalı klemens ── sağ motor
```

Lehim yok. ⚠️ Motor durduğunda (stall) 1 A'in üstünde akım geçiyor:
gevşek bir ek ısınır ve zamanla kopar.

L9110 lojik girişleri 2,5 V üstünü yüksek sayıyor (ESP32'nin 3,3 V'u
yetiyor), besleme aralığı 2,5–12 V.

---

## Beden algılama

G2 içeri çekmeli giriş. Beden yokken pin havada → `1`; takılınca gövde
toprağına bağlanıp `0`. Sıfır ek parça, tek kablo.

Karar **1 saniye kararlı** kalınca değişiyor: takıp çıkarırken kontak
sekiyor ve sekmeyi durum değişikliği saymak, panelin kumanda kartını
açıp kapatması demek olurdu.

⚠️ **Bunun bilmediği şey: AA pil anahtarı açık mı.** "Beden takılı"
diyor, "beden çalışıyor" demiyor. Panel bunu yazıyor. Ölçmenin yolu var
ama parça istiyor (AA gerilimini bölücüyle ADC'ye vermek, iki direnç);
yapılmadı.

Kaç kez takıldığı sayılıyor (`/api/durum` → `beden.takma`). Kablo
temassızsa bu sayı hızla artıyor — "beden ara ara kayboluyor"
şikâyetinin sayısal karşılığı.

---

## Yazılım

| Dosya | Ne yapıyor |
|---|---|
| `main/pati_beden.hpp/.cpp` | katmanın tamamı: algılama, servolar, motorlar, jest motoru, ölü adam |
| `main/pati_beden_matematik.hpp` | donanıma dokunmayan matematik — konak testinde sınanıyor |
| `test/beden_karsilastir.cpp` | 4375 kontrol; `derle.bat`'ın 4. testi |

### Sıcak döngü kuralı

Beden görevi **I2C yapmıyor, kilit almıyor, NVS'e dokunmuyor.** İçindeki
tek donanım teması LEDC yazmaçları ve bir GPIO okuması.

**20 ms'lik döngü yalnızca gerçekten bir şey hareket ederken çalışıyor.**
Boşta görev uyuyor (200 ms) ve komut gelince bildirimle uyandırılıyor.
Yani tuzağa düşecek döngü, zamanın çoğunda hiç var olmuyor.

Bu, `CLAUDE.md`'deki 02.09.2026 gerilemesinin dersi: bir servo döngüsü
doğası gereği 20-50 ms'de bir dönmek istiyor ve bu dosya o tuzağa en
yakın duran yer.

### LEDC dağılımı

| İş | Zamanlayıcı | Kanal | Frekans |
|---|---|---|---|
| Arka ışık *(var olan)* | TIMER_1 | CH1 | — |
| Kollar | TIMER_2 | CH2, CH3 | 50 Hz, 14 bit |
| Motorlar | TIMER_3 | CH4–CH7 | **5 kHz**, 10 bit |

TIMER_0 ve CH0 boş. ESP32-S3'te 4 zamanlayıcı, 8 kanal var (yalnızca
düşük hız kipi).

⚠️ Motor frekansı **ölçülerek** 20 kHz'den 5 kHz'e indi — 20 kHz'de
motorlar hiç dönmüyordu. Gerekçe D adımında.

### Servo susturma

Kol hedefe varıp 250 ms geçince **darbe kesiliyor**. Darbe yoksa servo
tutma torku uygulamıyor ve susuyor — mikrofon 10 cm ötede ve sürekli
açık. Yan faydası: boştaki akım sıfır.

⚠️ Hafif plastik bir kol kendi ağırlığıyla düşmüyorsa bu bedava.
Düşüyorsa dinlenme açısı yerçekimine yaslanacak şekilde seçilmeli
(`KOL_DINLENME_DERECE`).

### Yumuşak kalkış — motorlar anlık tam güce geçmiyor

Kullanıcının açık isteği (06.09.2026). Teknik karşılığı: bir DC motorun
en yüksek akımı **kalkış anında** oluyor (rotor dururken sargı direnci
dışında akımı sınırlayan bir şey yok). O tepe AA hattında gerilim
çöküşü yapıyor ve aynı hatta duran servolar çöküşü görüyor.

0 → %100 **400 ms** (20 ms'lik tikte 5 birim). Çocuğun kumandasında
hissedilmiyor, akım tepesini düşürmeye yetiyor.

⚠️ **Durmak rampadan geçmiyor.** Sıfıra iniş anında; ölü adam
zamanlayıcısı ya da parmağın kalkması kademeli olamaz — o gecikme masa
kenarında santimetre demek.

Yön değiştirme rampadan geçtiği için değer önce 0'ı ziyaret ediyor:
sert ters çevirme (en kötü akım tepesi) kendiliğinden ortadan kalkıyor.
Konak testi üçünü de koruyor (`beden_karsilastir.cpp` · 5. bölüm).

### 🔴 Kalkmak ile gitmeye devam etmek ayrı iki sayı

Bu, motor tarafındaki en önemli fikir ve 06.09.2026'da gerçek kartta
ölçülerek öğrenildi.

Motor **dururken** yüksek güç istiyor (durgun rotor + redüktör
sürtünmesi), ama bir kez döndüğünde çok daha azı yetiyor. İkisi tek bir
sayıya bağlandığında robot ya **ötüyor** ya **fırlıyor**; arada
kullanılabilir yer kalmıyor. Kullanıcının şikâyeti buydu:
*"çok hızlı dönüyorlar, bu kadar hız olmaz çocuk için."*

| Sayı | Değer | Ne yapıyor |
|---|---|---|
| `MOTOR_KALKIS_DUTY` | %85 | Yalnızca dururken harekete geçerken |
| `MOTOR_KALKIS_MS` | 180 ms | Darbe penceresi; 100 ms'de tepeye çıkıyor |
| `MOTOR_EN_AZ_DUTY` | %17 | Ondan sonraki **gitme** tabanı |
| `hiz_egrisi` | karesel | Yarım itişte hızın dörtte biri |

Darbenin kendisi de **rampalı** (`MOTOR_KALKIS_ADIM`, 100 ms'de tepeye)
— anlık sıçrama yok. Bu, kullanıcının "anlık tam güç yapma" kuralına
bilinçli bir istisna ve **onayı alındı**. Farkı büyüklükte: kuralı
doğuran olay bitmiş pille 5 saniye kesintisiz tam güçtü.

Rampa adımı üç ayrı değer, çünkü üç durumun riski aynı değil:

| Durum | Adım/tik | Neden |
|---|---|---|
| Hızlanma | 5 | Akım tepesini düşüren yer burası, en yavaş bu olmalı |
| Yavaşlama | 15 | Akım zaten düşüyor; hızlı inmek istenen yavaş hıza çabuk oturtuyor |
| Kalkış darbesi | 20 | Sürtünmeyi kıracak kadar hızlı, yine de rampalı |

⚠️ %17 hâlâ **ölçülmedi** — ama artık bir kalkış eşiği değil bir gitme
tabanı ve o çok daha bağışlayıcı. Ölçmek için ek koda gerek yok:
tekerlekler takılı, Pati yerde, panelden hız sınırını düşür; motorun
dönmeye devam edemediği (kalkıp hemen durduğu) değer tabandır.

### Ölü adam zamanlayıcısı

Komut gelmeden **600 ms** geçerse motorlar duruyor. Panel dokunma
sürerken 150 ms'de bir gönderiyor; dört paket üst üste kaybolursa
duruyor.

Panelin sıfır göndermesine **güvenmiyoruz**: unutan bir panel, kaçan bir
robot demek. Panel ayrıca sekme arka plana atılınca da durduruyor
(`pointerup` o durumda hiç gelmiyor), ama güvenlik ona bağlı değil.

### 🔴 Özerk hareket — Pati döner, ilerlemez

Pati konuşurken kendiliğinden kıpırdıyor: konuşma başlayınca bir jest,
sonra **3–7 saniye rastgele** bekleme, konuşma sürüyorsa yeni jest.
Sabit aralık iki cümlede fark ediliyor ve mekanik görünüyor.

06.09.2026'da tekerlekler de bu jestlere katıldı. Ondan önce tekerlek
tamamen panele bağlıydı ve gerekçesi şuydu: **Pati'de uçurum sensörü
yok**, masanın kenarını görebileceği hiçbir yol yok, kendi kararıyla
ilerleyen bir Pati eninde sonunda düşer.

**Karar değişti, gerekçe değişmedi.** Eski kural yanlış yerde
duruyordu: yasaklanması gereken *tekerlek* değil, *yer değiştirme*ydi.

Şimdi özerkliğin tamamı tek bir sayıyla anlatılıyor:

```
sol = +teker        sag = -teker
```

İki tekerlek her zaman ters yönde → Pati yerinde döner, yer
değiştirmez. **Bu bir yorumdaki uyarı değil, tipin kendisi:** jest
karesinin tek bir `teker` alanı var, dolayısıyla ileri giden bir jest
*yazılamıyor*. Kuralı bilmeyen biri de bozamıyor.

#### Jest tablosunun üç kuralı

Üçü de `firmware/test/beden_karsilastir.cpp` §7'de zorlanıyor.

| # | Kural | Bozulursa ne olur |
|---|---|---|
| 1 | Tekerlek dönen kare **kol oynatmaz** | Motor kalkışı AA hattında çöküntü yapıyor; o anda hareket eden servo titriyor ya da sıfırlanıyor (06.09.2026'da yaşandı) |
| 2 | Yön değiştirmeden önce **sıfır karesi** var | `motor_rampa` yön değişimini bilerek yavaş geçiyor (5/tik = 400 ms). Araya sıfır konmazsa 150 ms'lik kare boyunca motor yalnızca yavaşlar, hiç dönmez — belirtisi "jest çalışmıyor" olur ve sebebi rampada aranmaz |
| 3 | **Net dönüş sıfır** | Pati her jestten sonra biraz daha başka yöne bakar; çocuğun "ileri" sandığı yön kayar ve kumanda öğrenilemez olur |

Ayrıca bir jestin toplam tekerlek süresi **900 ms**'yi aşamıyor.

🔴 **Dördüncü bir kural 12.09.2026'da eklendi: her tekerlek karesi en az
300 ms.** Gerekçesi ve ölçümü aşağıda ("İleri kayma").

Dönüş hızının üst sınırı **%75** (`JEST_DONUS_EN_COK`); çocuğun
joystick'i hâlâ %100'e çıkabiliyor. Yükselen şey darbe değil,
darbeden *sonraki* sürekli dönüş — tepe akımı değişmiyor.

| Jest | Tekerlek | Kalkış | Ne anlatıyor |
|---|---|---|---|
| `dinlen` `selam` `iki_kol` `alkis` `dusun` | — | — | yalnızca kol |
| `zipla` | — | — | kollarla hızlı zıplama — **kayma maliyeti sıfır** |
| `sevin` | 4 × 320 ms | 4 | dört salınım, sonra kollarla kutlama |
| `titre` | 4 × 280 ms | 4 | tablonun en hızlı dört salınımı — kıkırdama |
| `hayir` | 4 × 300 ms | 4 | sağ-sol: **kafa sallayıp "hayır" demek** |
| `bak_etrafina` | 2 × 440 ms | 2 | yavaş dön, **dur ve bak** (400 ms), geri dön |
| `firildak` | 2 × 600 ms | 2 | tablonun en büyük savurması |
| `dans` | 4 × 320 ms | 4 | hazırlık, dört salınım üst üste, kol koreografisi |

Yön değiştirme boşluğu **40 ms** — sıfırlanamaz (motoru doğrudan ters
çevirmek en kötü akım tepesi) ama en küçük makul değer.

🔴 **Kol karesi iki dönüşün ARASINA konmaz.** Kullanıcının sözü
(13.09.2026): *"1. ve 2. dc motor arası mesafe çok fazla"*. Sebebi
tabloda duruyordu: `dans`'ta iki dönüşün arasında bir kol karesi vardı
ve **kol kareleri kolun varmasını bekliyor** (~180 ms yol + bekleme),
yani iki dönüş arası 40 ms değil **~350 ms** oluyordu. Kollar artık
dönüşlerin önünde ve arkasında.

Yalnızca `hayir` **kendiliğinden seçilmiyor**: rastgele "hayır" demek
tuhaf olurdu, anlam taşıyor. `dans` 13.09.2026'da kendiliğinden
seçilebilir oldu — "uzun" diye dışarıda bırakılmıştı, ama istenen
canlılık tam olarak o.

⚠️ **`hayir` dört kalkışla tek istisna.** İki salınım "hayır" değil
"etrafına baktı" demek olur; anlam salınım *sayısında*. Kendiliğinden
seçilmediği için sürünme birikecek sıklıkta değil.

#### Sıklık — seyreklik bir süs değil

Sırası gelen jestin tekerlekli olma ihtimali **yarı yarıya**, ve iki
tekerlekli jest arasında **en az 12 saniye** var. Yerinde dönüş yer
değiştirmiyor ama tekerlek kayması **ikinci dereceden** bir sürünme
bırakıyor; boşluk o milimetrelerin birikmesine izin vermiyor. Sürekli
kıpırdayan bir robot ayrıca sevimli değil, huzursuz görünüyor.

⚠️ **Biriken şey süre değil, kalkış sayısı** — sürünme kalkış
darbesinde oluyor, dönüşün kendisinde değil. 12.09.2026'da `titre` ve
`dans` dört kalkıştan ikiye indiği için kura 1/3 → 1/2'ye çekilebildi:

| | Jest arası | Kalkış/jest | Kalkış/sn | Dönüş/sn |
|---|---|---|---|---|
| eski (12 sn, 1/3) | ~24,6 sn | 2,67 | 0,108 | 18 ms |
| yeni (12 sn, 1/2) | ~19,6 sn | 2,00 | **0,102** | **35 ms** |

Pati ~%28 daha sık dönüyor, dönüş süresi neredeyse iki katına çıkıyor,
ama dakikadaki kalkış sayısı biraz düşüyor.

🔴 **12 saniye duruyor ve bilerek duruyor.** İlk hesapta 9 saniyeye
indirilmişti; sayılar yeniden yapılınca 9 sn ile kalkış/sn'nin
*düşmediği*, ~%10 **arttığı** çıktı — çünkü ilk hesap "eski: jest
başına 4 kalkış" varsaymıştı, oysa kendiliğinden seçilen üç tekerlekli
jestin ortalaması 2,67'ydi. Bir güvenlik sınırı ölçülmemiş bir
gerekçeyle gevşetilmez.

Sayılar tablodan hesap (boşluk + kalan bekleme ortalaması 2,6 sn +
(kura−1) × 5 sn), gerçek kartta ölçülmedi. Ölçülmesi gereken şey bu
sayılar değil, **sürünmenin kendisi** — aşağıdaki yöntem.

Konuşma **başında** tekerlek yok, bilerek: her cümlenin başında dönmek
hem sıkıcı hem gereksiz motor kalkışı olurdu.

#### 🔴 Uzuv kipleri — tekerlekler ve kollar ayrı ayrı

Kullanıcının isteği (06.09.2026): *"DC motorların sesli komutlar dahil
tamamen kapatacak bir ayar… ayrı şekilde kollar yani servolar için de
koy. Belki çocuk motorların veya servoların konuşma boyunca hiç hareket
etmesini istemez."*

**Açma-kapama yetmiyordu, çünkü üç hâl var ve üçü de isteniyor:**

| Kip | Pati kendi | Sesli komut | Panel | Kullanım |
|---|---|---|---|---|
| `KIP_ACIK` **(varsayılan)** | ✓ | ✓ | ✓ | normal |
| `KIP_KUMANDA` | — | — | ✓ | "kendi kendine kıpırdamasın ama çocuk oynatabilsin" |
| `KIP_KAPALI` | — | — | — | gürültü istemiyorum / pil / Pati rafta |

*Kapalı* ile *sadece kumandadan* arasındaki fark, çocuğun elinden
kumandayı alıp almamak — iki ayrı açma-kapama ile anlatılamazdı.

**Sesli komut `KIP_KUMANDA`'da çalışmıyor**, çünkü üstünde parmak yok.
Bu, ölü adam zamanlayıcısının gerekçesinin aynısı: sürekli onay yoksa
bu Pati'nin kendi hareketidir.

⚠️ **İstek ile KAYNAĞI tek atomikte taşınıyor** (`no + kaynak * 256`).
İki ayrı değişken olsaydı panel bir jest yazarken sohbet görevi kaynağı
değiştirebilir ve Pati'nin kendi isteği "kumandadan geldi" diye
geçerdi — yani ebeveynin kipi sessizce delinirdi.

**Kollar `KIP_KAPALI` iken dinlenmeye inip susuyor.** Olduğu yerde
dondurmak yanlış olurdu: havada kalmış bir kol "bozuldu" görünür.
Dinlenme açısına inip orada darbe kesiliyor, yani servo sessiz ve
akımsız.

#### Tek boğaz

Pati'nin kendi kararıyla dönen tekerlek **tek bir yerden** geçiyor
(`pati_beden.cpp`, "OZERK HAREKET — TEK BOGAZ"). Özerk jest, sesli
komut, panelin jest düğmesi — hepsi. Üç kural, sırasıyla:

1. **Çocuğun parmağı her şeyi yener.** Joystick'ten komut geldiyse akan
   jest iptal ve tekerlek onun.
2. **Kip izin vermiyorsa tekerlek yok** — isteğin kaynağına bakarak.
3. **Hız tavanı özerk harekete de uygulanıyor.** Kaydırıcıyı kısan
   ebeveyn Pati'nin kendi hareketlerini de kısmış oluyor; iki ayrı sayı
   olsaydı panel yalan söylerdi.

#### Kol ile tekerlek hiç aynı anda hareket etmiyor

Üç ayrı önlem, hepsi aynı ölçülmüş sebep için (aynı AA hattı, motor
kalkışında gerilim çöküntüsü):

- Jest tablosu aynı karede ikisini birden komut edemiyor (kural 1)
- Tekerlek dönerken **servo darbesi kesiliyor** — hedefe yeni varmış
  bir servo 250 ms daha tutma torku uyguluyor ve asıl akım orada
- Tekerlek dönerken **kol duruyor** (hedefini unutmadan; dönüş bitince
  kaldığı yerden devam ediyor)

⚠️ **Tekerlek karesi kolun varmasını beklemiyor.** Beklemek tekerleği
kola bağlardı: kol yolda takılırsa kare hiç ilerlemez ve tekerlek
**sınırsız** dönerdi. Bir motorun durma koşulu asla başka bir şeyin
varması olmamalı — yer değiştirme sıfır olsa bile.

### 🔴 Dönüş yönü ters çıktı — sebebi ayırt edilmedi

06.09.2026, gerçek kartta: çocuk çubuğu **sağa** itince Pati **sola**
dönüyordu. İleri ve geri doğruydu — kullanıcı Pati'yi hem yüzü hem
sırtı dönükken denedi, ikisinde de Pati kendi ileri yönüne gitti. Yani
iki motorun ileri yönü doğru; ters olan yalnızca dönüş.

⚠️ **İki sebep de bu belirtiyi birebir veriyor ve hangisi olduğu
ölçülmedi:**

| | Ne olurdu | Neden ileri/geri etkilenmez |
|---|---|---|
| **(a) Kablo** | Sol ve sağ motor L9110'un A/B kanallarına ters bağlanmış | İkisi de aynı yöne gider, hangisinin "sol" sayıldığı fark etmez |
| **(b) Ayna** | Pati'nin yüzü çocuğa dönükse robotun kendi "sağı" çocuğun "solu"dur | Yön değişmiyor, sadece bakış açısı |

**Nasıl ayırt edilir:** Pati'nin **sırtı** çocuğa dönükken sür.
(a) ise o yönde de ters görünür; (b) ise doğru görünür.

Düzeltme iki hâlde de aynı olduğu için **karıştırmada** duruyor
(`SURUS_X_YONU = -1`): kumandanın hissini belirleyen yer orası ve
konak testinin koruyabildiği tek yer orası. Pin haritasına koymak,
ayırt edilmemiş bir iddiayı donanım belgesine yazmak olurdu —
`pati_pinler.h`'de yalnızca ölçülmüş şeyler var.

🔴 **(a) olduğu bir gün kanıtlanırsa düzeltme pin haritasına
taşınmalı.** O zaman `/api/durum`'daki `beden.sol` fiziksel **sağ**
tekerleği anlatıyor demektir ve `TESHIS.md` insanları o alana bakmaya
gönderiyor — bir arıza ararken saatler yer.

`jest_donus` de aynı işareti kullanıyor, yani jest tablosundaki
"sağa dön" ile kumandanın "sağa dön"ü aynı yön. Jestlerin net dönüşü
sıfır olduğu için ayrışsalar kimse fark etmezdi; kod okuyanı yanlış
bilgilendirirdi.

### 🔴 Pati bedeninin farkında — dört ayrı hâl

Bu bir süs değil, **ölçülmüş bir hatadan çıktı.** Gerçek kullanımda
çocuk *"kolunu kaldır"* dedi ve Pati *"benim kolum yok ki"* dedi.
Yanlıştı: Pati'nin takılıp çıkarılabilen bir gövdesi **var**, o an
takılı değildi.

Modelin bunu kendiliğinden bilmesi mümkün değildi. Ana promptta
*"Kucucuk bir robotsun. Gozlerin ekranda"* yazıyor ve model oradan
**doğru** bir çıkarım yapıyor. Eksik olan bilgi, promptta olmayan bilgi.

Ana prompta dokunulmadı — o Aşama 1'de ölçüldü (5412 karakter, uyum
%9 → %86) ve değişirse sayılar karşılaştırılamaz olur. Bunun yerine
`yuz.PROMPT_EKI`'nin deseni izlendi: duruma göre **ek**.

| Durum | Gönderilen | Pati ne diyor |
|---|---|---|
| Beden takılı | `BEDEN_PROMPT_EKI` + araç şemasında `hareket` alanı | "Baksana, kaldırdım!" |
| Beden yok | `BEDENSIZ_PROMPT_EKI`, `hareket` alanı **şemada hiç yok** | "Şu an bedenim takılı değil! Takarsan kolumu kaldırabilirim. Şimdilik sadece gözlerimle anlatıyorum." |
| Az önce takıldı | yukarıdakine + `BEDEN_YENI_EKI` (bir kez) | "Bedenim geldi! Bak, kolumu kaldırabiliyorum!" |
| Tekerlekler Pati'ye kapalı | yukarıdakine + `BEDEN_TEKERLEK_KAPALI_EKI` | "Tekerleklerim şu an kapalı ama sana kollarımla dans edeyim!" |
| Kollar Pati'ye kapalı | yukarıdakine + `BEDEN_KOL_KAPALI_EKI` | "Kollarım şu an kapalı ama sana dönerek sevincimi gösterebilirim!" |
| **İkisi de kapalı** | `BEDEN_HAREKETSIZ_EKI`, `hareket` alanı **şemada yok** | "Kollarım ve tekerleklerim şu an kapalı. Annene sorarsan açabilir!" |

⚠️ İkisi de kapalıyken söylenen şey **bedensiz hâlden farklı**: kolu
*var*, sadece kapalı. Aynı metni kullanmak, ebeveyni olmayan bir
kabloyu aramaya gönderirdi.

⚠️ **"Az önce takıldı" eskiyorsa düşürülüyor.** Oturum tazelemesi ilk
doğal boşlukta oluyor; o boşluk gecikirse "az önce" yalan olurdu —
çocuk bedeni çok önce takmış, Pati bir anda sevinmeye başlamış
görünürdü. 90 saniyeden eskiyse bayrak sessizce düşüyor
(`beden_yeni_takildi_al`).

⚠️ **Tekerlek anahtarı MODELE de söyleniyor, sadece motora değil.**
Söylenmezse Pati "dans ediyorum!" der, tekerlekler dönmez ve çocuk
robotun bozulduğunu düşünür. Bu yüzden `ayar_beden_hareket_yaz` oturum
tazelemesi istiyor — tekerlek tarafı anında geçerli, model tarafı tur
sonunda.

**İki listenin ayrışması sessiz bir hata olurdu:** prompt "şunları
seçme" derken `HAREKET_TEKERLEKLI`'yi sayıyor, cihaz ise jest
tablosunun `teker_var` alanına bakıyor. Konak testi ikisini
karşılaştırıyor (§7).

#### 🔴 Kolun mekanik aralığı

Kolların ucuna gerçek kol takılınca ikisi aynı yerlere gidemiyor
(09.09.2026, kullanıcının ölçümü):

| Kol | Aralık | Neden |
|---|---|---|
| **Sol** | %10 – %70 | aşağıda **tekerleğe**, yukarıda **üstteki kabloya** çarpıyor |
| **Sağ** | %0 – %100 | önünde bir şey yok |

Aşılırsa kol bir yere dayanır, servo dönmeye çalışıp durur, ısınır ve
akım çekmeye devam eder. **Belirtisi sürekli bir vızıltıdır** ve kimse
onu bir sınır hatası diye okumaz.

Sınır **yüzdeye** uygulanıyor, açıya değil — jest tablosu da panel de
yüzde konuşuyor (aynı gerekçe `KOL_SAG_AYNA`'da da var). Açıya
uygulansaydı aynalanmış sağ kolda ters ucu kırpardı.

**Kol hedefi tek bir yerden yazılıyor** (`kol_hedef_yaz`). Hedefi yazan
beş ayrı yer var — panel düğmeleri, jest kareleri, konuşma sonu, beden
takılması, kol kipinin kapatılması — ve kırpmayı her birine ayrı
koymak birini unutmaya davetiye olurdu. Unutulan yer kolu bir kez fazla
gönderip servoyu bitirebilir.

⚠️ **Kırpma yazarken yapılıyor, okurken değil.** Jest ilerletme "kol
hedefe vardı mı" diye aynı değeri okuyor; kırpma okuma tarafında
olsaydı hedef ile gerçek konum hiçbir zaman eşitlenmez ve jest hiç
ilerlemezdi.

Jestler sınırı deliyor mu? Hayır — konak testi tabloyu tarayıp
bakıyor. `iki_kol` %95 istiyor, sol kolda %70'e kırpılıyor: iki kol
asimetrik kalkıyor ama hiçbir şeye çarpmıyor. `selam` zaten sağ kolla
yapılıyor.

🔴 **Bu dört sayı `ayar_sifirla()` ile SİLİNMİYOR.** Ayrı flash
bölümünde (`anahtar`) duruyorlar — `partitions.csv` o bölümü tam bu iş
için tanımlamış. Kullanıcının gerekçesi: *"tekrar ayarlanması
unutulursa bir yerlere çarpıp servo bozulabilir."*

Panelde dört çubuk var ve **çubuğu bırakınca kol oraya gidiyor**, yani
ebeveyn çarpıp çarpmadığını görerek ayarlıyor. İki çubuk **tek
istekte** gidiyor (`kol_sol_araligi` / `kol_sag_araligi`): ayrı ayrı
gönderilseydi arada `en_az > en_cok` olan bir an oluşur ve kol o an
yanlış yere giderdi.

#### Tek kol jestleri

`sag_kol` ve `sol_kol` doğrudan *"kolunu kaldır"* için var: kaldırıp
**900 ms tutuyor**, sonra indiriyor. Tutmanın bedeli yok — servo
hedefe varınca darbe kesiliyor ve hafif plastik kol kendi ağırlığıyla
düşmüyor.

Sıfırıncı kare öteki kolu açıkça **indiriyor** (`-1` değil `0`):
"tek kolunu kaldır" denince önceki jestten kalan diğer kolun havada
kalması, hareketi okunmaz yapardı.

⚠️ Sağ/sol **Pati'nin kendi tarafı**, çocuğun değil. Karşılıklı duran
iki kişide bu her zaman böyle.

### Sesli komut — asıl risk yanlış anlama değil

Çocuk "dans et" deyince Pati dans ediyor. Bunu model, zaten açık olan
`yuz_ifadesi` aracına eklenen `hareket` alanıyla istiyor.

**İkinci bir araç eklenmedi ve bu ölçüme dayanıyor:** araç çağrısı
medyanı ~682 ms artırıyor ve cihazda araçlar **sıralı** çalışıyor
(istemci `NON_BLOCKING` alanını setup'a yazmıyor). İkinci bir araç,
modele durup beklemek için ikinci bir sebep olurdu; oysa model bu aracı
duygusu değiştiğinde nasılsa çağırıyor.

Alan ve bedeni anlatan prompt eki **yalnızca beden takılıyken**
gönderiliyor — olmayan bir bedeni anlatmak, Pati'ye yapamayacağı bir
şey vaat ettirirdi. Beden takılıp çıkarıldığında beden katmanı oturum
tazelemesi istiyor (`ayar_yenileme_iste`), yani karar bayatlamıyor.

⚠️ **"Nasılsın" deyince ileri gider mi?** Sorunun kendisi yanlış yerde
duruyor. Yanlış anlama ihtimali zaten düşük — bu ASR'de kelime yakalama
değil, dil modelinin tanımlı bir aracı seçmesi. Ama önemli olan o
değil: **"ileri git" DOĞRU anlaşılırsa da Pati masadan düşer.** Üstelik
joystick'in aksine sesli komutun üstünde parmak yok, yani ölü adam
zamanlayıcısı onu koruyamaz — onay sürekli değil, tek seferlik.

Bu yüzden çözüm "model daha iyi anlasın" değil, **sözlükte ilerlemenin
hiç olmaması**. Yanlış anlamanın en kötü sonucu: Pati garip bir anda
sevimli bir dönüş yapar.

Pati yürüyemediğini ayrıca **söylüyor**: *"Ben kendim yürüyemem, gözüm
yok, masadan düşerim! Ama telefondaki düğmelerden beni sen
sürebilirsin."* Bu doğru, ve karakterin parçası.

---

## Panel

Kumanda kartı **yalnızca beden takılıyken** görünüyor. Joystick, kol
düğmeleri (her dokunuşta çeyrek adım), hazır jestler, hız sınırı
(varsayılan panelde %50 = cihazda %70) ve **iki kip seçimi**
(Tekerlekler / Kollar, ikisi de varsayılan AÇIK).

**Kapalı uzvun düğmeleri sönüyor**, gizlenmiyor: düğmenin varlığı
seçimi değiştirince ne kazanılacağını gösteriyor. Basıp hiçbir şey
olmaması ise çocuğa düğmenin bozuk olduğunu düşündürürdü — mavi tuş
için de aynı karar verilmişti.

| Kapalı olan | Sönen |
|---|---|
| Tekerlekler | joystick + `data-teker` jest düğmeleri |
| Kollar | kol ▲▼ düğmeleri + kol jestleri |

⚠️ Joystick'te `pointer-events: none` da var. Yalnızca soluklaştırmak
dokunmayı engellemezdi ve çocuk sönük bir çubuğu sürüklemeye
çalışırdı.

`POST /api/beden` — sıcak yol, dokunma sürerken 150 ms'de bir çağrılıyor.
İçinde NVS, I2C ve kilit yok.

```json
{ "x": 40, "y": 80 }          joystick konumu, -100..100
{ "kol_sol": 75 }             kaldırma yüzdesi 0..100
{ "jest": "selam" }           yalnızca kol: dinlen · selam · iki_kol
                              · alkis · dusun
                              tekerlekli:   sevin · titre · hayir
                              · bak_etrafina · dans
```

Karıştırma (`sol = y−x`, `sağ = y+x`) ve hız tavanı **cihazda**. Panel
yalnızca parmağın nerede olduğunu söylüyor. İki sebep: tavan cihazda
dursun (panel gönderse bile aşılamasın), ve karıştırmanın işareti konak
testinde yakalanabilsin — JS'te olsaydı "sola bas, sağa gitsin" hatası
ancak robot masadayken fark edilirdi.

Hız sınırı ve kipler `/api/ayar` üzerinden NVS'e yazılıyor
(`beden_hiz`, `tekerlek`, `kol` — sonraki ikisi 0/1/2).

**Eski anahtarlardan geçiş yazılı.** 3.2.x'te tek bir `hareket`
açma-kapaması vardı; kapalı demek "tekerlek yalnızca joystick'ten
dönsün", yani tam olarak `KIP_KUMANDA`. `tekerlek` anahtarı yoksa ve
`hareket` varsa o eşleme uygulanıyor. Geçiş olmasaydı anahtarı
kapatmış bir ebeveyn güncellemeden sonra Pati'yi yine kıpırdar bulur
ve ayarının sessizce kaybolduğunu fark etmezdi.

⚠️ Her yeni ayar için **yeni NVS anahtarı** kullanılıyor. Aynı adı
yeni bir anlamla kullanmak, eski değeri yeni özelliğin varsayılanı
yapar — yani "varsayılan açık" o cihazda hiç gerçekleşmez.

### 🔴 Hız sınırı: panel 0–100 gösterir, cihaz 40–100 saklar

%40'ın altında tekerlek dönmüyor, yalnızca ötüyor (06.09.2026'da gerçek
kartta ölçüldü). O aralığı kaydırıcıda göstermek, çocuğa **hiçbir şey
yapmayan bir yer** bırakmak olurdu.

Çubuk yine de alışıldık 0–100 görünümünde kalıyor; dönüşüm panelde
(`pati.js` · `gosterilendenGercege`):

| Panelde görünen | Cihazda | |
|---|---|---|
| %0 | %40 | en yavaş çalışan değer |
| **%50** | **%70** | **varsayılan** |
| %100 | %100 | tam güç |

⚠️ **Bedeli: panelde yazan sayı ile motora giden sayı aynı değil.**
Bir arıza ararken karıştırmamak için iki önlem var:

- Panel gerçek değeri de küçük puntoyla yazıyor (*"Motora giden: %70"*)
- `/api/durum` → `kumanda.hiz` **her zaman gerçek değeri** döndürüyor,
  gösterileni değil

Sınırlar tek kaynakta: `pati_beden_matematik.hpp` → `HIZ_TAVAN_EN_AZ` /
`HIZ_TAVAN_EN_COK`. Karıştırma da, ayar katmanı da aynı sabitleri
kullanıyor; konak testi alt sınırın altına düşülemediğini koruyor.

---

## Devreye alma — sıra önemli

### A. Açılış seğirmesi testi — **masada değil, elde**

Beden bağlı, **Pati havada, tekerlekler serbest**.

🔴 **SIRA ÖNEMLİ — önce Stick, sonra AA:**

1. AA anahtarı **kapalı**
2. StickS3'ü aç
3. **2-3 saniye bekle**
4. AA anahtarını aç

Bu sıra, aşağıdaki "açılış seğirmesi" penceresini **tamamen kapatıyor**:
yazılım pinleri kurmadan sürücüye hiç güç gitmiyor. Kablolama
diyagramından alındı; önceki hali (AA açıkken Stick'i çalıştır) o
pencereyi açık bırakıyordu.

> **Ne arıyoruz:** açılışın ilk yarım saniyesinde tekerlekler dönüyor mu?
> Yazılım pinleri kurana kadar ESP32 çıkışları havada kalıyor ve
> L9110'un girişleri belirsiz. `beden_baslat()` `app_main`'in en
> başında, `guc_baslat()`'ın hemen ardında — pencere önyükleyici
> süresine inmiş durumda ama sıfır değil.
>
> **Dönerse:** iki adet 10 kΩ direnç, her motor girişinden toprağa.
> **Dönmezse:** modülün kendi direnç ağı işi görüyor, parça gerekmiyor.

**Ölçüm sonucu: _______________**

### B. Algılama

Stick'i takıp çıkar. Seri portta `BEDEN TAKILDI` / `beden cikarildi`,
panelde kumanda kartının gelip gitmesi. Hızlı takıp çıkarmada seğiriyor
mu (1 saniyelik kararlılık penceresi yeterli mi).

**Ölçüm sonucu: _______________**

### C. Kollar

Jest düğmeleri, konuşurken kendiliğinden hareket, darbe kesme.

- Jest sırasında `gozler_kare_us` değişiyor mu, atlanan kare çıkıyor mu?
  (Sağlıklı: kare 24–30 ms, bütçe 50 ms — `TESHIS.md`)
- Kol durduğunda servo gerçekten susuyor mu?
- Kol ters yöne gidiyorsa: `pati_beden_matematik.hpp` → `KOL_SAG_AYNA`
- Kol yeterince kalkmıyor / dayanıyorsa: `KOL_DINLENME_DERECE`,
  `KOL_TAVAN_DERECE`

**Ölçüm sonucu: _______________**

### D. Tekerlekler ve kumanda

#### ✅ 06.09.2026 — PWM frekansı: 20 kHz çalışmıyor, 5 kHz'e indi

İlk denemede **servolar oynadı, motorlar hiç oynamadı.** Belirti tam bir
kablo hatası gibi görünüyordu; ölçüm başka yeri gösterdi.

Ölçüm zinciri (beden takılı, tekerlekler sökülü, pil %41):

| Katman | Nasıl bakıldı | Sonuç |
|---|---|---|
| Panel → `/api/beden` | doğrudan POST | ✅ `{"tamam":true}` |
| Karıştırma + hız tavanı | sürerken `/api/durum` → `beden` | ✅ `sol=60 sag=-60` |
| Firmware → LEDC | yukarıdaki sayı zaten oradan geliyor | ✅ |
| %60 duty @ 20 kHz | gözle | ❌ **dönmedi** |
| %100 duty @ 20 kHz | hız sınırı %100 yapılıp | ✅ **döndü** |

🔴 **Ayırt eden şey %100.** Orada duty 1023/1024, yani **anahtarlama
neredeyse hiç yok** — çıkış sürekli DC. Kırpılmış her duty'de L9110'un
kenarları yetişmiyor. Sürücü bipolar, üzerinde ~1 V düşüyor ve rahat
çalıştığı aralık 1–10 kHz.

20 kHz "mikrofon duymasın" diye seçilmişti. Gerekçe doğruydu ama
**öncelik yanlıştı**: motor dönerken TT redüktörünün mekanik sesi PWM
cıvıltısından zaten yüksek, ve motorlar yalnızca çocuk sürerken dönüyor.

⚠️ **Bir daha aynı belirti görülürse sıra şu:** "servolar oynuyor,
motorlar oynamıyor" kabloya baktırıyor. Kabloya bakmadan önce panelden
`beden.sol/sag` oku — orada doğru sayı varsa sorun kabloda **değil**,
sinyalin sürücü çıkışına dönüşmesinde.

#### Ölçülmeyi bekleyenler

- **En düşük dönen duty kaç?** `MOTOR_EN_AZ_DUTY` hâlâ %35 ve **hâlâ bir
  tahmin** — yukarıdaki ölçüm frekansı ölçtü, ölü bölgeyi değil.
  Ölçmek için ek koda gerek yok: **panelin hız sınırı kaydırıcısı**
  joystick sonuna kadar itildiğinde uygulanan duty'nin ta kendisi.
  Tekerlekler **takılı** ve Pati **yerdeyken** (yük gerçekçi olsun)
  %20'den başla, beşer artır; ilk dönen değer ölü bölgedir.
- **L9110 elle dokunulacak kadar soğuk mu?** (5 kHz'de olmalı.)
- **Sürüş sırasında `sohbet_mik_tepe` yükseliyor mu?**
- **Ölü adam çalışıyor mu?** Parmağı joystick'te tutarken telefonun wifi
  bağlantısını kes — Pati 600 ms içinde durmalı.
- ⚠️ **Motor dönerken çökme oluyor mu?** Yukarıdaki testte `guc.cokme`
  26 → 27 oldu. **Tek artış kanıt değil** — Pati pilde zaten kendi
  başına çöküyor (`PIL.md`, pil %41'di). Ama motor akımı ortak topraktan
  dönüyorsa bu gerçek bir etken olabilir. Uzun sürüşte sayacı izle.

---

## 🔴 Motor gürültüsü — belirtisi tanıdık olacak

Motor akımı I2S ve I2C hatlarına biniyor. Belirtileri **`TESHIS.md`'deki
"yanlış kart" tablosunun aynısı**:

- ses cızırdıyor
- ES8311 cevap vermiyor
- M5PM1 NACK veriyor / L3B açılmıyor
- ekran siyah kalıyor

**Motor eklendikten sonra bu belirtiler görülürse önce gürültüye
bakılacak, yazılıma değil.** Çare: 100 nF seramikleri motor uçlarına
takmak (üç tane: uçlar arası, ve her uçtan gövdeye). Aldığın
kondansatörler tam bunun için.

---

## Sıradaki: bedeni Pati'yi de beslesin

`PIL.md`'deki brownout hâlâ çözülmedi — 250 mAh'lik hücre Pati konuşmaya
başlarken çöküyor. Bedende **4 AA pil** var.

M5Stack'in belgesi: Hat2-Bus'ın **EXT_5V** pini varsayılan olarak
**giriş** kipinde ve oradan 5 V beslemek destekleniyor. 6 V → 5 V küçük
bir çevirici (MP1584 / LM2596 / 5 V UBEC) AA pilden Stick'i besleyebilir.

Olacaklar, hepsi kendiliğinden:

- brownout biter — akım artık AA'dan geliyor
- `guc_kaynak()` "USB" görür → ses tavanı ve göz kare hızı kendiliğinden
  yükselir (kod zaten böyle yazılmış, hiçbir şey değişmiyor)
- lityum hücre boşalmak yerine şarj olur

⚠️ Ama bu yukarıdaki **tek pazarlıksız kuralı deler**: o zaman
konnektörde gerçekten bir güç kablosu olur. Ayrı bir aşama, ayrı bir
karar; o kablo diğer yedisinden fiziksel olarak ayrılmalı (farklı renk,
farklı konnektör).

---

## ⚠️ AÇIK İŞ — servolar

06.09.2026 akşamı: servolar önce çalışıyordu, **iki motoru %100'de
5 saniye döndüren testten sonra** oynamaz oldu. Motorlar aynı anda
çalışmaya devam ediyor.

### Ölçülenler

| Gözlem | Ne kanıtlıyor |
|---|---|
| Motorlar panelden dönüyor *(o an)* | AA hattı o anda amper verebiliyordu — **kalıcı sonuç DEĞİL, aşağıya bak** |
| Açılışta **sağ** servo oynadı | Servolara gerilim **ve** sinyal ulaşıyor; pin 9 sağlam; sağ servo canlı |
| Cihaz komutu uyguluyor (`beden.kol_*` değişiyor) | Panel → firmware → LEDC yolu sağlam |
| Servo ve motor kanalları aynı görevde, aynı kurulumda | Motorlar çalışıyorsa servo darbeleri de üretiliyor |

🔴 **Yazılım elendi, tahminle değil zaman çizelgesiyle:** servolar 3.1.0
üzerinde, firmware'de hiçbir şey değişmeden öldü (değişen tek şey
`beden_hiz` ayarıydı ve o yalnızca motor duty'sine giriyor). Sonrasında
cihaz defalarca yeniden başladı; `beden_baslat()` her açılışta LEDC'yi
sıfırdan kuruyor ve **servo durumu NVS'e hiç yazılmıyor.** Yeniden
başlatmayı atlatan bir yazılım kilitlenmesi mümkün değil.

Zamanlayıcı çakışması da yok: arka ışık TIMER_1, kollar TIMER_2,
motorlar TIMER_3.

### 🔴 En muhtemel sebep: BESLEME ARALIKLI

Aynı akşam biraz sonra **motorlar da durdu** — dönmek yerine PWM
frekansında ötmeye başladılar. Sonra yine döndüler, sonra yine öttüler.

Bu, tek tek bakıldığında yanıltıcı olan bütün gözlemleri tek bir şeyle
açıklıyor: **AA hattı aralıklı.** Dinlenince toparlıyor, yük binince
saniyeler içinde çöküyor.

⚠️ **BURADA BİR YANLIŞ TEŞHİS YAPILDI ve tekrar edilebilir:** "motorlar
dönüyor, demek hat sağlam, pil elendi" denildi. O an doğruydu ama
**kalıcı sonuç çıkarmak yanlıştı.** Aralıklı bir arızada tek bir başarılı
ölçüm hiçbir şeyi elemiyor — arıza zaten aralıklarla kayboluyor.
Doğrusu: aynı testi dinlenmiş ve yorulmuş halde tekrarlamak, ya da
multimetreyle **yük altında** ölçmek.

Bitmiş alkalin pilin ders kitabı davranışı bu. Aynı davranışı ısınan
yüksek dirençli bir ek de yapar — ikisi ancak ölçümle ayrılır.

İkinci sebep (elenmedi): bastırmasız hatta motor anahtarlama
sıçramaları. Servolar motorlarla
aynı AA hattında ve **hiçbir bastırma yok** — ne 100 nF, ne büyük
kondansatör. Kondansatörler planda "belirti çıkarsa tak" diye isteğe
bağlı bırakılmıştı; **motorların ilk gerçek denemesinden önce takılmalıydı.**

### Devam edilecek yer

1. Sinyal uçlarını **pin 7 ↔ pin 9** yer değiştir. Arıza takip ederse
   kablo, etmezse o servo gitmiş.
2. Servo güç uçlarını doğrudan **L9110'un `VCC`/`GND`** pinlerine al —
   motorların dönmesi o pinlerde sağlam gerilim olduğunu kanıtlıyor,
   bu hamle şüpheli demeti devre dışı bırakıyor.
3. Orta nokta testi (%40 ↔ %60): uçlara hiç gitmeden. Ortada oynayıp
   uçlarda oynamıyorsa kol **mekanik olarak sıkışıyor** ve
   `KOL_DINLENME_DERECE` / `KOL_TAVAN_DERECE` (20°–150°) daraltılmalı.
   O iki sayı ölçülmedi, seçildi.

### Yeni servo takmadan önce

**İki adet 100 nF, L9110'un yeşil klemensine** — her motorun iki
vidasına birer tane. Lehim gerekmiyor, kondansatörün bacakları motor
kablolarıyla aynı vidaya giriyor. Yoksa aynı şey tekrarlar.

Hattaki gerilim çöküşü ayrı bir iş ve 100 nF onu çözmüyor: AA uçlarına
paralel **470–1000 µF elektrolitik** gerekiyor (henüz alınmadı).

---

## 🔴 Powerbank'li gövde — 12.09.2026

İkinci bir gövde yapıldı. Eskisiyle **aynı**: 2 servo, 2 DC motor, aynı
sürücüler. Tek fark besleme: eski gövde 4'lü kalem pil, yeni gövde
**powerbank** (Thull 5000 mAh).

⚠️ **Eski gövde duruyor ve kullanılıyor.** Çözüm önce panelden açılıp
kapanan bir ayardı; kullanıcı 12.09.2026'da *"panelde olmalarına gerek
yok, varsayılan olarak açık olsunlar"* dedi ve ayar kaldırıldı.
**Hareket artık her gövdede çalışıyor.**

Kalem pilli gövdedeki bedeli küçük ama sıfır değil: 13 saniyede bir
5 puanlık iki kol hareketi, yani hafif bir pil tüketimi ve göze
çarpabilen bir kıpırdanma. Bilinerek kabul edildi. Geri alınacak yer
`pati_beden.cpp`'de tek bir koşul.

### Sorun

Powerbank akım çekilmeyince **15 saniyede** kendini kapatıyor. Bir kere
uyuduğunda kendiliğinden uyanmıyor: kabloyu çıkarıp takmak ya da
powerbank'in düğmesine basmak gerekiyor.

⚠️ **Powerbank her çıkışı AYRI izliyor.** Kullanıcı Type-C'yi Stick'e,
USB-A'yı gövdeye bağlamış. Stick sürekli 100-200 mA çekiyor ama USB-A
kendi başına uyuyor. Yani *"Stick'i powerbank'e bağla"* çözümü zaten
devrede ve yetmiyor.

### Çözüm: 13 saniyede bir minik kol hareketi

Pati boştayken iki kol da **5 puan** oynayıp geri dönüyor. Yön her
seferinde ters çevriliyor, yani kol bulunduğu yerin etrafında salınıp
aynı noktaya dönüyor — zamanla kaymıyor.

Servo hareket ederken **150-250 mA** çekiyor; powerbank'in sayacı
sıfırlanıyor.

⚠️ Hedef yine `kol_hedef_yaz`'dan geçiyor, yani mekanik aralığa
kırpılıyor. **Kol hiçbir şeye çarpamaz.** Aralık ucundaysa o yöndeki
hareket kırpılır; bir sonraki turda ters yöne gideceği için hareket
yine olur.

Kol kipi kapalıysa hareket de gönderilmiyor — "kapalı" kapalı demek.
Tek kapatma yolu bu.

### Denenip ELENEN iki yol

| Deneme | Sonuç |
|---|---|
| **Motor darbesi** — %12 duty, 80 ms, 8 sn arayla | powerbank yine uyudu. %12 duty stall akımını eşiğin üstüne çıkarmaya yetmiyor |
| **Servoları sürekli enerjili bırakmak** | yine uyudu. Kollar hafif, tutma akımı 10-20 mA civarında kalıyor. Ayrıca sürekli vızıltı yapıyor ve mikrofon 10 cm ötede |

### 🔴 Yol üstünde yapılan muhakeme hatası — kayda geçsin

İki başarısız denemeden sonra şöyle denildi: *"powerbank ORTALAMA akıma
bakar, kısa darbe hiçbir zaman yetmez."*

**Yanlıştı.** Kullanıcı itiraz etti ve haklıydı: powerbank'lerin çoğu
ortalama değil **zamanlayıcı** kullanıyor — *"eşiğin altında 15 saniye
geçerse kapan"*. O modelde eşiği aşan **herhangi** bir kısa yük sayacı
sıfırlıyor.

Yani sorun sürenin kısalığı değil, motor darbesinin **gücüydü**. Doğru
teşhis konunca çözüm ilk denemede çalıştı.

**Ders:** bir mekanizmanın nasıl çalıştığı varsayılmadan önce, elde iki
başarısız ölçüm varken bile o ölçümlerin *neyi* elediği ayrı ayrı
sorulmalı. "Süre yetersiz" ile "güç yetersiz" farklı şeyler ve ikisi
aynı belirtiyi veriyordu.

### 🔴 Kol yönü — ikinci gövdede servolar ters takılı

Kullanıcının şikâyeti (12.09.2026): powerbank'li gövdede *"kolunu
kaldır"* deyince kol **aşağı** iniyor. Servolar ters monte edilmiş.

Panelde **"Kol yönünü ters çevir"** anahtarı var, **varsayılan kapalı**
(ilk gövdenin davranışı).

⚠️ **Çevirme MANTIKSAL YÜZDEDE yapılıyor**, açı ya da darbe
genişliğinde değil — `pati_beden.cpp` · `kol_aci()`, tek boğaz.

Sebep önemli: kol aralıkları da yüzde uzayında tanımlı ve
`kol_hedef_yaz` orada kırpıyor. Çevirme çıkış tarafında olsaydı,
**kırpma bir uca bakarken servo öbür uca giderdi** — yani aralık
koruması ters çalışır ve kol tam da çarpmaması gereken yere giderdi.

Böylece yüzde her zaman aynı şeyi anlatıyor (0 = aşağı, 100 = yukarı)
ve fiziksel yön tek satırda dönüyor.

⚠️ `kol_aci()` **bütün** `kol_derece10` çağrılarının yerini aldı. Biri
atlanırsa "hedefe vardı mı" karşılaştırması çevrilmiş değerle
çevrilmemişi kıyaslar ve **jest hiç ilerlemez**.

🔴 **Ayar `ayar_sifirla()` ile SİLİNMİYOR** — kol aralıklarıyla aynı
gerekçe, kullanıcının kendi sözleriyle: *"çocuk fabrika ayarlarına
dön'e basarsa ve yönleri değiştirmeyi unutsa kollar bir yere
çarpabilir."*

İki bağımsız koruma var:

| | |
|---|---|
| `ayar_sifirla()` | adı sayılan anahtarları siliyor; `kol_ters` o listede **yok** |
| Depo | `kalici_sayi_yaz` **ayrı flash bölümüne** yazıyor (`nvs_open_from_partition`) |

⚠️ **Fabrika sıfırlama gerçek kartta ÇALIŞTIRILMADI**, çünkü
`POST /api/fabrika` içinde `ag_unut()` var ve wifi bilgisini siler.
Koruma kod okunarak doğrulandı, ölçümle değil. İkisi de yapısal
olduğu için güvenilir, ama not düşülüyor.

### ⚠️ Kol aralıkları iki gövde arasında PAYLAŞILIYOR

Pati hangi gövdede olduğunu bilmiyor; aralıklar tek kopya ve kalıcı
depoda. İkinci gövdenin mekanik sınırları farklıysa (kol farklı yere
çarpıyorsa) gövde değiştirirken **aralıkların da ayarlanması gerekir.**
Bu ölçülmedi — ikinci gövdede kolun neye çarptığı henüz bakılmadı.

### Panelin kol düğmeleri sınırı aşıyor gösteriyordu — 3.5.15

Kullanıcının bildirdiği kusur (12.09.2026): *"sol için %70 yaptım ama
servo kontrol şeyinde onu 100'e kadar çıkarabiliyorum."*

Panel kendi sayacını **0–100** arasında kırpıyordu, ebeveynin
ayarladığı aralıkta değil:

```js
K.kol[hangi] = Math.max(0, Math.min(100, K.kol[hangi] + yon * 25));
```

Cihaz doğru davranıyordu — `kol_hedef_yaz` 70'e kırpıyor ve kol 70'te
duruyor. **Yanlış olan gösterge**: panel "%100" yazıyordu.

⚠️ Bedeli görünenden büyük: ebeveyn ayarladığı sınırı paneldeki sayıya
bakarak **doğrulayamıyordu**. "Sınır çalışmıyor" diye okunabilirdi ve o
yanlış okuma, aralığı hiç kullanmamaya götürürdü — yani servoyu
koruyan tek şeyi.

Düzeltme `kolSinirla()`: sınırları çubuklardan okuyor (onlar da
cihazdan geliyor), yani tek kaynak kalıyor. "Dinlen" jesti de artık
0'a değil, ayarlanan tabana iniyor.

🔴 **Asıl koruma hâlâ cihazda** (`kol_hedef_yaz`). Paneldeki kırpma
onun yerine geçmiyor, yalnızca panelin doğru söylemesini sağlıyor.

**Aynı tuzak bu depoda üçüncü kez:** ses tavanı (3.5.5), cevap hızı
kutusu (3.5.10), şimdi kol düğmeleri. Hepsinin dersi aynı — *bir sınırı
iki yerde tutuyorsan, ayrıştıklarında kimse fark etmez.*

### Dinlenme konumu = aralığın ortası — 3.5.16

Kullanıcının isteği (12.09.2026): *"dinlenme derecesi min ile maks
toplamı bölü 2 olsun — çocuk sol kolu 0-70 ayarlarsa dinlenme 35, sağ
0-100 ise 50 olacak."*

Öncesinde dinlenme sabit `%0` idi ve `kol_hedef_yaz` onu aralığın **alt
ucuna** kırpıyordu. Yani dinlenme ile mekanik alt sınır aynı şeydi ve
birbirinden ayrılamıyordu.

Orta nokta ayrıca **en güvenli yer**: kol iki uçta da bir şeye
değebiliyor (sol kol aşağıda tekerleğe, yukarıda kabloya), ortada
ikisinden de en uzakta duruyor.

Tek kaynak `pati_beden.cpp` · `kol_dinlenme()`.

⚠️ **Jest tablosundaki `0` artık "dinlenme" demek**, "en aşağı" değil.
Her jestin son karesi `{0, 0}` ve anlamı "kolları bırak"; `JEST_DINLEN`
zaten tek kare: `{0, 0, 0, 0}`. Bağlanmasaydı tutarsızlık çıkardı —
konuşma sonunda kol ortaya, jest sonunda alt uca giderdi.

Tablo **değiştirilmedi**: sabitler konak testinin taradığı veri ve
orada `0` hâlâ `0`. Çeviri yalnızca uygulama anında.

### 🔴 ÇÖZÜLEMEYEN: Pati kapalıyken servo oynuyor

Kullanıcının gözlemi (12.09.2026): StickS3 tamamen kapalıyken ama gövde
beslemedeyken servolardan biri aralığın dışına (~%-20) gidip hareket
ediyor.

**Sebep:** Pati kapanınca servo sinyal pinleri (`GPIO1`, `GPIO8`)
havada kalıyor. Servo besleme almaya devam ediyor ve havadaki hat
gürültü topluyor; servo bunu geçerli darbe sanıp rastgele bir yere
gidiyor. Oraya dayanırsa ısınır ve yanar.

| Durum | Yazılımla çözülür mü |
|---|---|
| **Derin uyku** (mavi tuş) | **evet** — çip beslemede, `gpio_hold_en` pinleri aşağıda tutabilir |
| **Tam kapatma** (yan düğme) | **hayır** — çip beslemesiz, hiçbir yazılım çalışmıyor |

🔴 **Tam kapatma için tek çözüm donanım**: her servo sinyal hattından
toprağa 10 kΩ. Hat aşağıda kalır, servo geçerli darbe görmez, sessizce
gevşer. Pati açıkken etkisi yok (0,3 mA).

⚠️ **Kullanıcı direnç eklemek istemiyor** (12.09.2026) ve yazılımla
çözülemediği için **bu sorun açık bırakıldı.** Gövde beslemesini Pati
kapalıyken açık bırakmamak tek pratik önlem.

Derin uyku tarafı yapılmadı: kullanıcının tarif ettiği durum tam
kapatma ve o yol zaten kapalı. Yapılacaksa dikkat — uyanışta
`gpio_hold_dis()` unutulursa LEDC pinleri süremez ve **gövde sessizce
ölür.**

---

## ✅ İleri kayma — ÇÖZÜLDÜ (13.09.2026, mekanik)

**Kullanıcı ön tekerlekleri dönebilen (caster / "sarhoş") tekerlekle
değiştirdi ve kayma bitti.**

🔴 **Bu aynı zamanda teşhisin doğrulanması.** Sebep aşağıda fotoğraftan
çıkarılmıştı: sabit akslı ön tekerlekler gövdeyi bir rayın üstüne
koyuyor — ileri-geri serbest, dönmeye karşı katı — ve dönüşe
çevrilemeyen kuvvet direnç görmediği tek eksene kaçıyor. Dönebilen bir
tekerlek o rayı ortadan kaldırıyor.

⚠️ **Yazılımın yapamayacağı şey gerçekten yapılamazdı** ve bu bölümün
asıl değeri orada: üç sürüm boyunca jest tablosu küçültülüp büyütüldü,
hiçbiri kaymayı değiştirmedi. Doğru teşhis mekaniğe yol gösterdi;
yazılımda aranmaya devam edilse hâlâ açık olurdu.

**Sonucu:** tekerlek jestlerinin sıklığı 3.5.25'te rahatça artırıldı
(`TEKER_KALKIS_ARA_US`). Boşluk yine de sıfırlanmadı — Pati'de uçurum
sensörü yok ve sürekli kıpırdayan bir robot huzursuz görünüyor.

Aşağıdaki bölüm teşhisin nasıl yapıldığını tutuyor; benzer bir belirti
çıkarsa yöntem hâlâ geçerli.

---

## 🔴 İleri kayma — "dans et" ve özellikle "kıkırda" (12.09.2026)

Kullanıcının şikâyeti: *"eski patide yoktu ama bu pati örneğin dans et
de biraz ileri gidiyor (özellikle kıkırdada), olduğu yerde kalmıyor."*
İkinci (powerbank'li) gövdede, tekerlekler de farklı.

### Neyin elendiği — sırayla

| Şüpheli | Sonuç |
|---|---|
| Jest tablosunun net dönüşü sıfır değil | **elendi** — konak testi her jestte `net_donus == 0` sayıyor, beşi de sıfır |
| `jest_donus` iki tekerleğe eşit dağıtmıyor | **elendi** — test her girdide `sol + sag == 0` tarıyor |
| `motor_rampa` bir yönde farklı davranıyor | **elendi** — `yavasliyor` kararı iki işaret için simetrik, test ±'da aynı |
| `motor_duty` işareti kaybediyor ama büyüklüğü değil | **elendi** — test `motor_duty(-h) == motor_duty(h)` |
| Bir motor diğerinden güçlü | **elendi, ama düşünmeyi gerektiriyor** — aşağıda |

**Bir motorun diğerinden güçlü olması kaymaya sebep olamaz**, çünkü her
jest yönü değiştiriyor ve bu, her motorun eşit süre ileri ve geri
gitmesi demek. A karesinde sol ileri/sağ geri, B karesinde tersi;
motorlar arası fark iki karede ters işaretle çıkıp **kendiliğinden
sadeleşiyor**. Tablodaki "net dönüş sıfır" kuralı bunu zaten garanti
ediyor.

Geriye **tek bir açıklama** kalıyor: kayma, `teker`'in işaretinden
bağımsız. Yani gövde **ileri yöne geri yönden daha kolay kayıyor** —
motorların değil, **şasinin** bir özelliği. Yeni gövdede tekerlekler ve
muhtemelen destek noktası değiştiği için eski gövdede görünmüyordu.

🔴 **Bunun sonucu önemli: jest tablosunun elindeki tek serbestlik
(`teker`'in işareti) bu kaymayı iptal edemez.** Yazılım kaymayı
*sıfırlayamaz*, yalnızca **azaltabilir**.

### Kaymanın nerede oluştuğu

Her tekerlek karesi motor DURURKEN başlıyor, yani her kare bir **kalkış
darbesi** (%85, 180 ms) demek. Darbe bitince rampa tablodaki değere
iniyor — `MOTOR_INIS_ADIM` 15/tik ve tik 20 ms:

| `teker` | hız tavanı | hedef | iniş |
|---|---|---|---|
| 60 | %100 | %60 | 40 ms |
| 60 | %70 (varsayılan) | %42 | 60 ms |
| 60 | %40 (en düşük) | %24 | 100 ms |

Yani bir karenin **ilk 220–280 ms'si** darbe ve iniştir; üst uç,
ebeveyn hız kaydırıcısını en dibe çektiğinde görülüyor. Kural **300 ms**
çünkü en kötü hâl 280 ve arada pay kalmalı.

Eski kareler 120–220 ms'ydi: **hiçbiri o eşiği görmüyordu.** Robot daha
dönmeye başlamadan kare bitiyor, geriye yalnızca darbenin sarsıntısı
kalıyor — ve jest başına dört kare, dört sarsıntı.

Bu, kullanıcının *"özellikle kıkırdada"* gözlemiyle birebir uyuşuyor:
`titre`'nin kareleri 120 ms ile tablonun **en kısası**ydı, yani dönüş
oranı en düşük, sarsıntı oranı en yüksek jest oydu.

### Yapılan

1. **Her tekerlek karesi ≥ 300 ms** (fiilen 320–360 ms). Karenin son
   bölümünde artık gerçek dönüş var, üstelik tablodaki sayı ilk kez
   motora ulaşıyor. ⚠️ `hayir`'in 200–240 ms'lik kareleri bu kuralın
   altında kalıyor — bilerek, çünkü orada salınım sayısı anlamın
   kendisi.
2. **Jest başına kalkış 4 → 2** (`hayir` hariç). Toplam dönüş süresi
   neredeyse aynı kaldı, sarsıntı sayısı yarıya indi.
3. Boşalan görünürlük bütçesi **sıklığa** gitti (kura 1/3 → 1/2; 12
   saniyelik boşluk **değişmedi**), ama dakikadaki kalkış sayısı yine
   de biraz düştü.

### 🔴 Ölçülecek — tahmin değil

Yukarıdaki teşhis **bir hipotez**: "kayma kalkış başına oluşuyor".
Değişiklik onu sınanabilir yapıyor, çünkü `dans`'ın toplam dönüş süresi
değişmedi (720 ms) ama kalkış sayısı yarıya indi.

| Gözlem | Ne demek |
|---|---|
| `dans`'ın kayması **kabaca yarıya** indi | hipotez doğru, kayma kalkış başına |
| `dans`'ın kayması **değişmedi** | kayma dönüş *süresine* bağlı — çözüm burada değil, toplam süreyi kısaltmak gerekir |
| Kayma **arttı** | uzun karede sürekli dönüş de kaydırıyor; kareler kısaltılmalı ve kayma kaçınılmaz demektir |

**Nasıl ölçülür** — ek koda gerek yok: Pati'yi düz bir masada bir
bant çizgisinin üstüne koy, aynı jesti panelden **beş kez** çalıştır,
çizgiye olan mesafeyi ölç. Beş jest sonunda kaç mm ilerlediği tek
sayıdır ve iki sürüm arasında karşılaştırılabilir.

⚠️ Eski değerler geri gerekirse: kareler 120–220 ms, `TEKER_KURA = 3`,
`TEKER_ARA_EN_AZ_US = 12 sn`.

### Yazılımın yapamayacağı

Kaymayı gerçekten **sıfırlamak** için Pati'nin dönüşe bir miktar geri
hareketi karıştırması gerekirdi — yani `sol + sag != 0`. **Bu yapılamaz
ve yapılmayacak:** o eşitlik Pati'nin masadan düşmemesinin tek yapısal
garantisi ve konak testi her girdide tarıyor. Ölçülmemiş bir telafi
sayısı uğruna o garantiyi delmek, kaymadan çok daha pahalı bir hata
olur.

### 🔴 13.09.2026 — fotoğraf: gövdede DÖRT tekerlek var

Kullanıcı 3.5.18'de *"hâlen biraz öne doğru kayıyor"* dedi ve gövdenin
fotoğrafını gönderdi. Fotoğraf belgede olmayan bir şey gösterdi:

| | |
|---|---|
| **2 tahrikli** | siyah lastik, sarı göbek — TT redüktörlü motorlar |
| **2 serbest** | beyaz, 3B basılmış, **dişli sırtlı**, sabit akslı |

⚠️ **Serbest tekerlekler döner tabla (caster) DEĞİL** — sabit akslı ve
sırtlı. Bu, kaymanın en güçlü açıklaması:

**Sabit bir tekerlek kendi ekseninde neredeyse dirençsiz yuvarlanır,
yana kaymaya ise direnir.** İki tanesi birden, gövdeyi bir **rayın**
üstüne koymak demek: ileri-geri serbest, dönmeye karşı katı.

Pati yerinde dönerken dört tekerleğin dördü de yana sürtünmek zorunda.
Dönmeye çeviremediği her kuvvet, direnç görmediği tek eksene — **ileri
ya da geriye** — çıkıyor. Yana giden hareket sürtünmeyle hemen sönüyor,
ileri giden hareket ise yuvarlanıp gidiyor.

🔴 **Kullanıcının kendi sorusunun cevabı burada:** *"tekerler eskiye
göre daha kalın ve yol tutuşu daha yüksek, ondan mı?"* — evet, çok
muhtemel. Ama asıl suçlu tahrikli tekerlekler değil, **serbest
olanlar**: tutuş arttıkça yana sürtünme direnci artıyor, yani gövde
dönmeye daha çok direniyor ve artan kuvvet ileri eksene kaçıyor.

#### Üç ölçüm — hiçbiri alet istemiyor

| Deney | Ne gösterir |
|---|---|
| **Elle çevir:** Pati'yi masada tutup yerinde döndürmeye çalış | Direniyor ve öne yürümek istiyorsa sebep gövdenin kendisi, motorlar değil |
| **Kaygan zemin:** "dans et"i cam / kitap kapağı / plastik dosya üstünde çalıştır | Kayma kaybolursa sebep **tutuş** |
| **Sadece serbest tekerleri kaydır:** o ikisinin altına bant/koli bandı şeridi koy, tekrar dene | Kayma azalırsa sebep **serbest tekerlekler** |

Üçüncüsü en ayırt edici olanı, çünkü yalnızca tek bir değişkeni
değiştiriyor.

#### ✅ Mekanik çözüm — UYGULANDI VE İŞE YARADI

Serbest tekerleklerin yerine **dönebilen** bir destek: küçük bir
bilyeli caster, mobilya kaydırıcısı ya da yuvarlak pürüzsüz bir ayak.
Dönemeyen bir tekerlek yerinde dönüşe katılamaz — ne kadar iyi
yapılmış olursa olsun.

**13.09.2026: kullanıcı bunu yaptı ("sarhoş teker") ve kayma bitti.**

#### 🔴 Yazılım önce YANLIŞ değişkeni büyüttü — 3.5.19

Kaymayı sıfırlayamadığı için "hareket/kayma oranını" en büyük yapmaya
çalıştı: aynı dönüş süresi, **yarısı kadar kalkış**. Jest başına hareket
sayısı 4'ten 2'ye indi, kareler uzadı.

**Kullanıcının cevabı bunu eledi** (13.09.2026): *"daha kötü hale mi
getirdin, dans et diyorum 2 tane dc motor hareketi yapıyor, seri değil,
eğlenceli değil, eskiden bu kadar değildi."*

⚠️ **Ders: canlılığın ölçüsü toplam dönüş süresi değil, ayrı ayrı
hareket sayısı.** İzleyici için "dört kere kıpırdadı" ile "iki kere
geniş döndü" aynı şey değil; ikincisi daha cansız — toplam dönüş açısı
daha büyük olsa bile. Sayıyı büyütmek doğruydu, *hangi* sayıyı
büyüteceğim yanlıştı.

#### Yazılım şimdi ne yapıyor — 3.5.21

Dördü birden alınıyor ve bedeli açıkça yazılı:

- Jest başına **4 hareket**, her kare **≥ 280 ms** (darbe bitince gerçek
  dönüş var)
- Kol karesi dönüşlerin arasından çıktı
- `TEKER_ARA_EN_AZ_US` 14 → **10 sn**
- `JEST_TEKER_EN_COK_MS` 900 → **1400**

| | Jest arası | Kalkış/jest | Kalkış/sn |
|---|---|---|---|
| 3.5.16 | ~24,6 sn | 2,67 | 0,108 |
| 3.5.18 | ~19,6 sn | 2,00 | 0,102 |
| 3.5.19 | ~19,4 sn | 2,00 | 0,103 |
| **3.5.21** | **~15,4 sn** | **4,00** | **0,260** |

🔴 **İleri kayma hızı kabaca iki buçuk katına çıkıyor.** Kullanıcı bunu
bilerek istedi ve kendisine sayıyla söylendi. Kaymanın gerçek çözümü
zaten yukarıda: **dönebilen bir destek tekerleği.**

**Geri alma yolu tek satır:** `TEKER_ARA_EN_AZ_US`'u büyüt. Kayma
rahatsız ederse önce oraya bakılacak, jest tablosuna değil — tablo
kullanıcının istediği canlılığı taşıyor.
