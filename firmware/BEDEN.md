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

| Jest | Tekerlek | Ne anlatıyor |
|---|---|---|
| `dinlen` `selam` `iki_kol` `alkis` `dusun` | — | yalnızca kol |
| `sevin` | 400 ms | sağ dön, sol dön, iki kol yukarı |
| `titre` | 480 ms | çok kısa hızlı titreşim — kıkırdama |
| `hayir` | 680 ms | küçük sağ-sol: **kafa sallayıp "hayır" demek** |
| `bak_etrafina` | 440 ms | yavaş dön, dur ve bak, geri dön |
| `dans` | 720 ms | kol ve dönüş sırayla, koreografi |

`hayir` ve `dans` **kendiliğinden seçilmiyor** — yalnızca istenince.
Biri anlam taşıyor (rastgele "hayır" demek tuhaf olurdu), diğeri uzun.

#### Sıklık — seyreklik bir süs değil

Sırası gelen jestin tekerlekli olma ihtimali **üçte bir**, ve iki
tekerlekli jest arasında **en az 12 saniye** var. Yerinde dönüş yer
değiştirmiyor ama tekerlek kayması her dönüşte birkaç milimetrelik
**ikinci dereceden** bir sürünme bırakıyor; 12 saniye o milimetrelerin
birikmesine izin vermiyor. Sürekli kıpırdayan bir robot ayrıca sevimli
değil, huzursuz görünüyor.

Konuşma **başında** tekerlek yok, bilerek: her cümlenin başında dönmek
hem sıkıcı hem gereksiz motor kalkışı olurdu.

#### Tek boğaz

Pati'nin kendi kararıyla dönen tekerlek **tek bir yerden** geçiyor
(`pati_beden.cpp`, "OZERK HAREKET — TEK BOGAZ"). Özerk jest, sesli
komut, panelin jest düğmesi — hepsi. Üç kural, sırasıyla:

1. **Çocuğun parmağı her şeyi yener.** Joystick'ten komut geldiyse akan
   jest iptal ve tekerlek onun.
2. **Anahtar kapalıysa tekerlek yok.** Kollar çalışmaya devam ediyor.
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
| Tekerlek anahtarı kapalı | yukarıdakine + `BEDEN_TEKERLEK_KAPALI_EKI` | "Tekerleklerim şu an kapalı ama sana kollarımla dans edeyim!" |

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
(varsayılan panelde %50 = cihazda %70) ve **"Konuşurken kıpırdasın"**
anahtarı (varsayılan AÇIK).

Tekerlek kullanan jest düğmeleri `data-teker` işaretli ve anahtar
kapalıyken **sönüyor**. Gizlenmiyor: düğmenin varlığı anahtarı açınca
ne kazanılacağını gösteriyor. Basıp hiçbir şey olmaması ise çocuğa
düğmenin bozuk olduğunu düşündürürdü — mavi tuş için de aynı karar
verilmişti.

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

Karıştırma (`sol = y+x`, `sağ = y−x`) ve hız tavanı **cihazda**. Panel
yalnızca parmağın nerede olduğunu söylüyor. İki sebep: tavan cihazda
dursun (panel gönderse bile aşılamasın), ve karıştırmanın işareti konak
testinde yakalanabilsin — JS'te olsaydı "sola bas, sağa gitsin" hatası
ancak robot masadayken fark edilirdi.

Hız sınırı ve hareket anahtarı `/api/ayar` üzerinden NVS'e yazılıyor
(`beden_hiz`, `hareket`).

⚠️ NVS anahtarı `sevinc` değil `hareket` — bilerek yeni. Aynı anahtar
kullanılsaydı, eski "sevinince yerinde dönsün" anahtarını bir kez
kapatmış bir cihaz yeni özelliği **kapalı** görürdü; yani "varsayılan
açık" o cihazda hiç gerçekleşmezdi. Eski anahtar `ayar_sifirla`'da
hâlâ siliniyor.

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
