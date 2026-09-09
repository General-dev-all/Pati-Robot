# Teşhis — kesilme, donma, çökme

Pati "cevap vermiyor", "ses kesiliyor", "kendiliğinden yeniden başlıyor"
dendiğinde bakılacak yer burası.

Bu belge 01.09.2026'da yaşanan uzun bir teşhis oturumundan çıktı. O gün
**dört ayrı belirti tek bir kök nedene** bağlandı ve yolda birkaç yanlış
teşhis yapıldı. Yanlış teşhisler de yazılı: aynı tuzağa tekrar düşülmesin.

---

## Önce: nereden bakılır

İki pencere var ve **ikisi de gerekli.**

### Seri port (USB)

```powershell
. "$env:IDF_PATH\export.ps1"
cd firmware
python -m esp_idf_monitor --port COM6 --baud 115200 --no-reset build\pati.elf
```

⚠️ Seri portu açmak DTR/RTS kapalı olsa bile cihazı sıfırlayabilir.
`--no-reset` çalışan cihazı bozmadan dinleme garantisi değildir.
Pildeki koşuyu ağdan izle. DTR/RTS'e dokunmak kartı indirme moduna sokuyor
(`boot:0x0 DOWNLOAD`, yeşil ışık yanıp söner) ve uygulama hiç açılmıyor.

⚠️ Monitörden önce kalan python süreçlerini öldür, yoksa port "erişim
engellendi" veriyor.

Backtrace çözümlenmesi için `export.ps1` ÖNCE çalıştırılmalı — yoksa
`xtensa-esp32-elf-addr2line` bulunamıyor ve çökme adresleri ham sayı
olarak kalıyor.

### Panel (ağ) — pilde TEK pencere

```powershell
(Invoke-WebRequest http://pati.local/api/durum -UseBasicParsing).Content
```

🔴 **USB'yi çekince seri kablo da gidiyor.** Yani "pilde ne oluyor"
sorusunun seri porttan cevabı YOK. 01.09.2026'da tam bu duvara çarpıldı:
pilde çökme yaşandı ve ses tavanının devreye girip girmediği
ölçülemedi. Panel `guc` alanı bu yüzden eklendi.

`api/durum` → `guc`: `kaynak` (usb/pil), `pil_mv`, `vin_mv`,
`sicaklik_c`, `ses_tavani`, `acilis`.

Uzun gözlem için 2 saniyede bir örnekleyip dosyaya yazmak işe yarıyor;
çökme anları `ULASILAMIYOR` satırı olarak görünüyor ve `acilis` alanı
sebebini söylüyor.

---

## 🔴 Beden takılıyken — önce gürültüye bak, yazılıma değil

Bu bölüm gövde (2 DC motor + 2 servo) takılıyken geçerli. Kablo şeması
ve devreye alma sırası `BEDEN.md`'de.

Motor akımı I2S ve I2C hatlarına biniyor ve **belirtileri bu belgedeki
"01.09.2026 — dört belirti, tek kök neden" tablosunun aynısı**:

| Belirti | Bedensiz Pati'de sebebi | Beden takılıyken ÖNCE bakılacak |
|---|---|---|
| Ses cızırdıyor | ses yolu / seviye | motor gürültüsü |
| ES8311 cevap vermiyor | I2C 400 kHz, L3B kapalı | motor gürültüsü |
| M5PM1 NACK veriyor | I2C hızı | motor gürültüsü |
| Ekran siyah kalıyor | L3B açılmadı | motor gürültüsü |

🔴 **Sıra tersine döndü.** Motor eklenmeden önce bu belirtilerin sebebi
neredeyse kesin olarak I2C/L3B idi ve orada aranırdı. Motor eklendikten
sonra aynı belirtiler **önce gürültü** demek — çünkü yazılım tarafı
değişmedi, elektriksel ortam değişti.

**Ayırt etme yolu bir dakika sürüyor:** AA pil yuvasının anahtarını
kapat. Belirti kayboluyorsa sebep gürültü; sürüyorsa yazılım.

**Çare:** 100 nF seramikleri motor uçlarına takmak — üçer tane: iki uç
arasına, ve her uçtan motor gövdesine.

### Beden takılı görünmüyor

`api/durum` → `beden.takili` false, panelde kumanda kartı yok.

1. Seri portta açılış özetine bak: `beden : takili` yazıyor mu.
2. `beden.takma` sayısına bak. **Hızla artıyorsa kablo temassız** —
   algılama pini bağlanıp kopuyor demektir. Sabit kalıyorsa kablo hiç
   bağlı değil.
3. Algılama pini Hat2-Bus **pin 14** (G2) ve gövde tarafında **AA (−)**
   hattına gidiyor. Pin 1'deki toprak kablosuyla karıştırılmış olabilir.

⚠️ **"Beden takılı" ile "beden çalışıyor" ayrı şeyler.** Algılama AA pil
yuvasının anahtarını göremiyor. Kart görünüyor ama hiçbir şey oynamıyorsa
ilk bakılacak yer o anahtar — panel de bunu yazıyor.

### Kol ters yöne gidiyor / dayanıyor

İki servo karşılıklı monte edilmiş ve aynalama tek bir sabitte:
`pati_beden_matematik.hpp` → `KOL_SAG_AYNA`. Aralık uçları aynı
dosyada (`KOL_DINLENME_DERECE`, `KOL_TAVAN_DERECE`).

⚠️ Servo dayanmaya binerse **durmuyor, akım çekmeye devam ediyor**:
ısınır, pil yer ve sürekli vızıldar. Belirtisi "kol biraz eksik
kalkıyor" + bitmeyen vızıltı. Konak testi darbenin 500-2500 µs dışına
çıkmasını yakalıyor (`derle.bat`, 4. test) ama mekanik dayanmayı
yakalayamaz — o gözle görülüyor.

### 🔴 06.09.2026 — "servolar oynuyor, motorlar oynamıyor"

**Belirti tam bir kablo hatası gibi görünüyor ve değil.**

Sebep PWM frekansıydı: motorlar 20 kHz'de sürülüyordu ve L9110'un
anahtarlama kenarları yetişmiyordu. %60 duty'de tekerlek hiç dönmedi,
%100'de döndü — çünkü %100'de duty 1023/1024, yani **anahtarlama
neredeyse hiç yok**, çıkış sürekli DC. Frekans 5 kHz'e indirildi
(`pati_beden.cpp` · `MOTOR_HZ`).

**Ayırt etme sırası — kabloya EN SON bakılır:**

1. Panelden oku: `api/durum` → `beden.sol` / `beden.sag`.
   **Joystick itilirken** okunmalı; bırakınca ölü adam sıfırlıyor.

   ```powershell
   $h=@{'Content-Type'='application/json'}
   1..16 | % {
     try { Invoke-WebRequest http://pati.local/api/beden -Method POST -Headers $h `
             -Body '{"x":100,"y":0}' -UseBasicParsing -TimeoutSec 5 | Out-Null } catch {}
     if ($_ -eq 6) { ((Invoke-WebRequest http://pati.local/api/durum -UseBasicParsing).Content `
                      | ConvertFrom-Json).beden }
     Start-Sleep -Milliseconds 150
   }
   ```

   `x=100, y=0` **yerinde dönüş** — masada dururken ileri komut vermek
   robotu kenara götürür.

2. Sayı doğruysa (tavan neyse o, ters işaretli) **sorun kabloda DEĞİL.**
   Komut panelden LEDC'ye kadar sağlam gelmiş demektir.

3. Sonra hız sınırını **%100** yap ve tekrar dene. Dönüyorsa sebep
   anahtarlama: frekansı indir. Dönmüyorsa sıra kabloda ve sürücüde.

**Sayı sıfır geliyorsa** sorun paneldedir: kumanda kartı görünüyor mu,
`beden.takili` true mu, joystick `pointerdown` alıyor mu.

### Tekerlek dönmüyor, sadece vızıldıyor

Ölü bölgenin altında kalıyor. `MOTOR_EN_AZ_DUTY` şu an **%35 ve bu bir
tahmin, ölçüm değil** (`pati_beden_matematik.hpp`). Ölçüp yaz.

İkinci ihtimal PWM frekansı — ama o bir kez ölçüldü ve düzeltildi
(yukarıdaki bölüm). 5 kHz'de hâlâ dönmüyorsa ölü bölge gerçekten
düşüktür.

### Pati kendiliğinden ilerliyor

**Olmaması gereken şey.** Tekerlekler yalnızca panelden sürülüyor ve
komut kesilirse 600 ms içinde duruyor.

1. Açılışta mı oluyor? Yazılım pinleri kurana kadar L9110 girişleri
   havada — `BEDEN.md` adım A. Çare iki adet 10 kΩ direnç.
2. Sürerken bırakınca mı sürüyor? Ölü adam çalışmıyor demektir; seri
   portta `olu adam: komut kesildi` satırı çıkmalı.
3. Ara ara kısa dönüyorsa: **özerk hareket** açık (panel → Kumanda →
   "Konuşurken kıpırdasın", varsayılan açık). Bu **yerinde döner,
   ilerlemez**.

🔴 **Özerk hareket İLERLİYORSA bir tekerlek ters bağlıdır.** Yazılımda
aranacak bir şey yok: `jest_donus` iki tekerleğe her zaman ters işaret
veriyor ve konak testi bunu her girdide tarıyor (`sol + sag == 0`).
Sol ve sağ aynı yöne dönüyorsa L9110'un o kanalının iki kablosu yer
değiştirmiş demektir. Ayırt etme: paneldeki joystick'i **tam sağa**
it — Pati yerinde dönmeli. Dönmeyip ilerliyorsa kablo.

### Pati konuşurken hiç kıpırdamıyor

Sırayla:

1. Panel → Kumanda → **Tekerlekler** ve **Kollar** seçimleri "Açık" mı?
   "Sadece kumandadan" seçiliyse Pati kendi kararıyla oynatmıyor ve
   sesli komutu da dinlemiyor — **bu doğru davranış.**
2. Kollar oynuyor ama tekerlek dönmüyorsa: bu **normal olabilir.**
   Sırası gelen jestin tekerlekli olma ihtimali üçte bir ve iki
   tekerlekli jest arasında en az **12 saniye** var. Kısa cevaplarda
   hiç görünmeyebilir — "dans et" diyerek zorla.
3. Hiçbir şey oynamıyorsa beden algılamasına bak: `/api/durum` →
   `beden.takili`.
4. AA pil anahtarı açık mı? Pati bunu **göremiyor** — bedeni görüyor
   ama pilin açık olup olmadığını göremiyor.

### Pati "benim kolum yok" diyor

Beden takılı değil — ya da cihaz öyle sanıyor. Pati bedensizken
"kolum yok" **demez**, "şu an takılı değil, takarsan kaldırırım" der.
Düpedüz "kolum yok" diyorsa promptun bedensiz eki gitmemiş demektir.

1. `/api/durum` → `beden.takili` ne diyor?
2. `false` ise doğru cevap **budur** ve beden gerçekten takılı değil.
3. `true` ama Pati hâlâ "kolum yok" diyorsa: beden konuşma
   **başladıktan sonra** takılmış olabilir. Prompt setup mesajında
   gidiyor; beden takılınca oturum tazelemesi isteniyor ama tazeleme
   ilk doğal boşlukta oluyor. Bir tur bekle.
4. Panel → Konuşma → **"Gözleriyle duygusunu göstersin"** kapalı mı?
   Beden ekleri o araçla birlikte gidiyor; kapalıyken Pati bedenini de
   bilmiyor.

### Joystick'te sağa basınca sola dönüyor

**3.3.1'de düzeltildi.** Daha eski bir sürümdeyse güncelle.

Güncelledikten sonra **hâlâ** tersse, muhtemelen bakış açısı: Pati'nin
yüzü sana dönükken robotun kendi "sağı" senin "solun". Sırtı sana
dönükken sür — orada doğru görünüyorsa bu ayna etkisi, arıza değil.

İkisinde de tersse `pati_beden_matematik.hpp` → `SURUS_X_YONU`
işaretini çevir. O satırın başındaki açıklama sebebini ve nasıl ayırt
edileceğini yazıyor.

⚠️ İleri/geri de tersse bu **başka bir arıza**: iki motorun da
kutupları ters bağlı demektir, dönüşle ilgisi yok.

### 🔴 "Panel bazen çıkmıyor" — üç belirti, tek sebep

**3.4.1'de düzeltildi.** Daha eskiyse güncelle; aşağısı sebebin kaydı.

Kurulum kipinde çip APSTA'da: AP telefon için, STA tarama için. STA
tarafı bir IP alırsa `g_durum` sessizce `Bagli` oluyor — **AP hâlâ
ayaktayken.** `AgDurumu::Kurulum` diye soran her yer o anda kör kalıyordu:

1. Captive portal'ın 302'si susuyor → telefon bağlanıyor, sayfa
   kendiliğinden açılmıyor
2. Kurulum perdesi ekranda hiç görünmüyor
3. Bilgi sayfası kendi AP adını "bağlı olduğum ağ" diye yazıyor

**Ayırt etme:** `/api/durum` → `ag.kurulum` artık AP'nin kendisini
söylüyor (eskiden durum makinesini söylüyordu). `ag.kurulum` true ise
AP ayakta ve `192.168.4.1` geçerli — `ag.durum` ne derse desin.

⚠️ **Yeni kod "AP açık mı" diye soracaksa `ag_kurulum_agi_acik()`
kullansın**, `ag_durumu()` değil.

### Kurulum sayfası açılmıyor / ebeveyn panele giremiyor

Telefon `Pati-XXXX` ağına bağlanınca yakalama sayfası **her zaman
kendiliğinden açılmıyor** (telefon markasına ve Android sürümüne göre
değişiyor). 3.4.0'dan itibaren cihaz bunu kendi anlatıyor:

1. Pati'nin ekranında ağın adı yazıyor mu? Yazmıyorsa kurulum kipinde
   değildir — `/api/durum` yoksa seri porttan `ag_durumu` bak.
2. Telefonu o ağa bağla. **Bağlanır bağlanmaz ekranda QR çıkmalı**;
   çıkmıyorsa telefon aslında bağlanmamıştır (Android "internet yok"
   deyip sessizce başka ağa dönebiliyor).
3. QR çıktıysa kamerayla okut. Okumuyorsa ekrandaki adresi elle yaz:
   `192.168.4.1`.

⚠️ **`pati.local` kurulum kipinde çalışmaz** ve QR da onu göstermez —
o adres yalnızca Pati bir ağa bağlıyken var. Kurulum kipinde tek geçerli
adres `192.168.4.1`.

### QR ekranda çıkıyor ama telefon okumuyor

Sırayla:

1. Ekran parlaklığı düşükse (pil kipi) telefonu yaklaştır.
2. **Ters QR değil** — beyaz kart üzerine siyah çiziliyor; öyle
   görünmüyorsa `qr_ciz`'in renkleri değişmiş demektir.
3. Matris bozulmuş olabilir: `firmware/qr_uret.py` yeniden koşturulup
   `pati_qr_uretilmis.h` üretilmeli. Betik yazmadan önce çıktıyı
   bağımsız bir çözücüyle okuyup doğruluyor, yani geçerse matris
   sağlamdır. (Gereken paketler: `qrcode`, `pillow`, `zxing-cpp`.)

### Panelde joystick ya da düğmeler sönük

Bozuk değil: o uzvun kipi **Kapalı**. Panel → Kumanda → Tekerlekler /
Kollar. Sönük bırakmak bilinçli — düğme çalışır görünüp hiçbir şey
yapmasa çocuk düğmenin bozuk olduğunu düşünürdü.

### "Dans et" dedim, Pati "tamam!" dedi ama hiçbir şey olmadı

Model hareketi istedi, cihaz adı tanımadı. Seri portta:

```
I (…) sohbet: hareket: dans
W (…) beden: bilinmeyen jest: dans
```

Sebep tek: `prototype/yuz.py` → `HAREKETLER` listesi ile
`pati_beden_matematik.hpp` → `JESTLER` tablosu ayrışmış.
**Konak testi bunu yakalıyor** (`beden_karsilastir.cpp` §7, "modelin N
hareket adının hepsi tabloda var"), yani bu belirti görülüyorsa test
çalıştırılmadan yayınlanmış demektir.

Ters yönü de var: model `hareket` alanını hiç kullanmıyorsa beden
takılı değildi. Alan **yalnızca beden takılıyken** semaya giriyor;
beden takılınca oturum tazeleniyor (`ayar_yenileme_iste`) ama tazeleme
ilk doğal boşlukta oluyor — konuşmanın ortasında takarsan bir tur
gecikebilir.

### Dönerken kol titriyor ya da kol bir yere sıçrıyor

İkisi aynı AA hattında ve motor kalkışı gerilimde çöküntü yapıyor.
Yazılımda üç önlem zaten var (jest tablosu aynı karede ikisini komut
edemiyor, dönerken servo darbesi kesiliyor, dönerken kol duruyor —
`BEDEN.md`). Üçü de varken hâlâ oluyorsa **yazılıma bakma, besleme ve
gürültüye bak**: 100 nF'lar takılı mı, piller taze mi.

---

## Sayılar ve sağlıklı değerleri

Beş saniyede bir basılan rapordan:

| Satır | Sağlıklı | Bozuksa ne demek |
|---|---|---|
| `mikrofon tepe genlik` | sessiz 400-2000, konuşma 8000-27000 | 0 → mikrofon ölü. 32768 → **kırpıyor**, kazanç yüksek |
| `dahili SRAM: en dusuk` | > 40.000 | < 10.000 → TLS ayırması çökecek |
| `dahili SRAM: en buyuk blok` | > 25.000 | < 10.000 → **parçalanma**, boş alan bol olsa bile |
| `ATLANAN KARE` | 0-2 | sürekli → çizim bütçesi aşılıyor |
| `GONDERILEMEYEN SES PARCASI` | 0 | > 0 → ses Gemini'ye hiç gitmiyor |
| `BAGLANTI KOPMASI` | sabit | dakikada artıyorsa ağ zayıf |
| `wifi_guc` (panelde `ag.guc`) | 3-4 / 4 | 1-2 → TCP takılmaları başlıyor |
| `acilis` | `guc` / `dis` | `brownout`, `cokme`, `*_bekcisi` → aşağıya bak |

`acilis` değerleri ve anlamları:

- `brownout` — besleme gerilimi çöktü. Ses seviyesi ya da güç kontağı.
- `cokme` — yazılım paniği. Seri portta backtrace vardır.
- `gorev_bekcisi` / `kesme_bekcisi` — bir görev takıldı.
- `guc` — düğmeyle açıldı. `dis` — kabloyla yükleme sonrası.

---

## 🔴 01.09.2026 — dört belirti, tek kök neden

Belirtiler birbirine hiç benzemiyordu:

1. Pati konuşurken **cihaz sallanınca** çöküyordu
2. Bazen kendiliğinden **cevap vermez oluyordu**, bir süre sonra düzeliyordu
3. Hem pilde hem USB'de oluyordu
4. Bazen "burdayım Mert Mert Mert" gibi **ses takılması** duyuluyordu

Zincir şu çıktı:

```
zayıf wifi (1/4)
  → TCP gönderim tamponu boşalmıyor
  → websocket yazması 10 sn bekleyip zaman aşımına düşüyor
     (network_timeout_ms = 10000, gemini_live_client.cpp)
  → bağlantı kopuyor, yeniden bağlanılıyor
  → HER TLS oturumu dahili SRAM'de delik bırakıyor
  → dokuz dakikada en büyük blok 29696 → 10240 bayt
  → mbedTLS 4437 bayt isteyip bulamıyor
  → E Dynamic Impl: alloc(4437 bytes) failed
  → bağlantı ölüyor / panic
```

Sallamak **doğrudan** çöktürmüyordu: el, küçük PCB antenin önünde
zayıflama yapıp zaten sınırda olan bağlantının kopma sıklığını
artırıyor, yani belleği daha hızlı tüketiyordu.

### Ölçülen düzelme

| | Başlangıç | wifi/lwIP → PSRAM | mbedTLS → PSRAM |
|---|---|---|---|
| Boş dahili SRAM | 27.447 | 46.227 | **54.087** |
| **Dip nokta** | **1.903** | 30.475 → 2.611 | **49.067** |
| En büyük blok | 7.680 | 10.240'a düşüyordu | **29.696** |

Dip nokta 1903 bayttı — yani sistem tamamen tükenmenin 2 KB uzağında
çalışıyordu.

Devreye alınan ayarlar (`sdkconfig.defaults`, gerekçeleri orada):
`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`,
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`,
`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`.

---

## 🔴 02.09.2026 — sallayınca çökme: PSRAM 80 MHz

Cihaz konuşurken elle sallanınca yeniden başlıyordu. Pilde daha kolay,
USB'de daha zor — ama ikisinde de. **İki kartta da aynı**, yani ünite
kusuru değil.

Bütün gün backtrace alınamadı ve sebebi şuydu: çökme anında CPU kod
çalıştırmıyor, **veri yolunda bekliyor.** Kaydedilmiş program sayacı
sonunda söyledi:

```
rst:0x8 (TG1WDT_SYS_RST)   PC -> panic_enable_cache (panic_handler.c:285)
rst:0x3 (RTC_SW_SYS_RST)   PC -> rtc_brownout_isr_handler
```

Birincisi anahtar: panic işleyicisi çalışmış ama **önbelleği yeniden
açarken donmuş.** Yani flash/PSRAM veri yolu cevap vermiyor. İşleyici
bile kurtaramayınca donanım bekçisi (TG1) kartı sıfırlıyor — bu yüzden
hiçbir yığın izi basılamıyor.

**Mekanizma.** Sarsıntı beslemede kısa bir dalgalanma yapıyor. Derin
olursa brownout dedektörü yakalıyor (ikinci sebep). Sığ olursa dedektör
görmüyor, ama 80 MHz'lik oktal PSRAM o dalgalanmayı kaldıramayıp veri
yolunu kilitliyor. Pilde besleme zaten zayıf olduğu için orada daha
kolay oluyor.

**Çözüm: `CONFIG_SPIRAM_SPEED_40M`.** Zamanlama payı iki katına çıkıyor.
Sonrasında `panic_enable_cache` donması ve bekçi sıfırlaması BİR DAHA
GÖRÜLMEDİ; pilde konuşma da çökmeden sürdü.

**Kalan.** Aşırı sallamada hâlâ tek tük `brownout` oluyor — o gerçek bir
gerilim çöküşü ve fiziksel. Bekçi 5 saniyede toparlıyor. Bekçiyi
kapatmak çözüm DEĞİL: kilitlenme aynen olur, ama Pati sonsuza kadar
donar ve fiş çekmek gerekir. Hastalık kilitlenme, sıfırlama ilaç.

⚠️ Flash da 80 MHz (`CONFIG_ESPTOOLPY_FLASHFREQ_80M`). Bu sınıftan yeni
bir belirti çıkarsa sıradaki deneme orayı 40 MHz'e indirmek.

---

## Aynı gün bulunan diğer üç şey

**Kopan oturumu toparlayan kod yoktu.** `gemini_live_client.cpp`
`disable_auto_reconnect = true` diyor — taşıma katmanı bilerek
toparlanmıyor, işi uygulamaya bırakıyor. `pati_sohbet.cpp`'de
`case Error:` sadece log yazıyordu. Sonuç zombi robot: mikrofon
çalışıyor, ses ölü sokete gidiyor, **kullanım dakikası işlemeye devam
ediyor** (14,0 → 17,6 dk ölçüldü) ve yalnızca fişi çekmek düzeltiyor.
Toparlanma + sessiz-sunucu bekçisi eklendi.

**Kurulum modu tek yönlü bir kapıydı.** Girince kayıtlı ağ bir daha hiç
denenmiyordu (`portMAX_DELAY`). Bir brownout sonrası Pati kurulum
moduna düşüp orada kaldı; ağ oradaydı, şifre doğruydu. Artık 3 dakikada
bir yeniden yokluyor.

**Mikrofon görevi yığıtı taşıyordu.** Uyandırma bu görevden çağrılıyor
ve 5400 karakterlik promptu kurup oturum açıyor; yığıt 3 KB'ydi.
6 KB'ye çıkarıldı. Belirtisi yanıltıcıydı: çocuk konuştuğu an çöküyordu,
yani "Pati'ye seslenince kapanıyor" gibi görünüyordu.

**Mikrofon kırpıyordu.** Kazanç 30 dB'ydi ve tahmine dayanıyordu; tepe
genlik 32768'e oturuyordu. Gemini bozuk sesi yanlış döküyordu — Türkçe
konuşulurken Portekizce ve İtalyanca cümleler geliyordu. 18 dB'de sıfır
kırpma.

---

## 🔴 02.09.2026 — "kapanıyor" sanılan şey aslında çökmeydi

Mavi tuşa uzun basınca Pati'nin derin uykuya girmesi eklendi. Ekranda
görülen: ekran söndü, cihaz yeniden başladı. "Kapanma çalışıyor ama
hemen açılıyor" diye okundu.

Seri log başka bir şey söyledi:

```
W tus: TUS 1 (GPIO 11) UZUN basildi
W pati.guc: derin uykuya giriliyor
***ERROR*** A stack overflow in task pati_tus has been detected.
Rebooting...
```

**Derin uyku hiç çalışmadı.** `esp_deep_sleep_start()` tuş görevinin
yığınında koşuyor ve o göreve 2048 bayt verilmişti — gerekçesi de
yazılıydı: *"iki GPIO okuması ve bir geri çağırım, derin bir çağrı
zinciri inmiyor."* Kısa basış için doğruydu; uzun basışın arkasındaki
işi (RTC hazırlığı, uyku kaynakları, önbellek kapatma) saymıyordu.

**Ders:** yığın hesabı görevin *kodunu* değil, çağırdığı en derin şeyi
sayar. "Sadece bir bayrak bırakıyor" muhakemesi, bayrağın arkasındaki
işi görmediği için yanlıştı.

**Dışarıdan görünüşü çökmenin kapanmayla aynı olmasıydı** — ikisi de
"ekran söndü". Seri log olmadan ayırt edilemezdi. 4096'ya çıkarıldı,
düzeldi.

---

## 02.09.2026 — açılıştaki renkli şeritler ve kenardaki ince çizgi

İki ayrı şey ama aynı kökten.

**Renkli şeritler:** `ekran_test_deseni()` — altı dikey çubuk çizip
2,5 saniye bekliyordu. İşi bayt sırası ve renk terslığini tek bakışta
çözmekti; 01.09.2026'da ikisi de doğrulandı, yani o günden beri
sadece bir gecikmeydi. Kaldırıldı.

**Kenardaki ince renkli çizgi:** aynı desenden kalan **tek bir piksel
satırı**. Sebebi yarım piksellik bir kayma:

```
(240 - 135) / 2 = 52,5     <- tam bolunmuyor
kodda EKR_KAYMA_Y = 52
```

`ekran_doldur()` 0..134 yazıyor, bellekte 52..186. Panel 53..187
görüyorsa **187 hiç yazılmıyor** ve orada açılıştan kalan ne varsa
ekranda kalıyor. X ekseninde bu sorun yok: (320−240)/2 = 40, tam
bölünüyor.

Çözüm, panelin gerçekte 52'den mi 53'ten mi başladığı ölçülmediği için
**bir satır taşırarak silmek**. İkisini de kapsıyor ve fonksiyon
açılışta bir kez çağrıldığı için bedeli yok.

---

## 02.09.2026 — pil geriliminde saçma okuma

M5PM1 bir kez **4336 mV** bildirdi. Tek hücreli lityum için imkânsız
(tavan 4,2 V); öncesi ve sonrası 4090 civarıydı, yani geçici bir I2C
bozulması.

Tek başına zararsız görünüyor **ama pil yüzdesi pencerenin TEPESİNDEN
hesaplanıyor** (yük altındaki sarkmayı yok saymak için, bkz. PIL.md).
Bir tane çöp okuma yüzdeyi 30 saniye boyunca %100'e kilitliyordu —
ekranda pil 4074 mV iken sayfa %100 yazıyordu.

3000–4250 mV dışı okumalar artık atılıyor (`pati_guc.cpp`,
`pil_ornekle`).

---

## 🔴 02.09.2026 — sıcak döngüye konan "küçük" çağrılar

Gözlerin pilde 10 fps'e inmesi ölçülmüş bir kazançtı: 9 dakikada 2
çökme. Aynı gece birkaç özellik daha eklendi (tuşlar, perdeler,
güncelleme sayfası) ve pil ölçümü **kötüleşti**:

| Yapı | Süre | Çökme | Ortalama | Gerilim |
|---|---|---|---|---|
| 10 fps | 9 dk | 2 | 4,5 dk'da bir | 3,83 → 3,68 V |
| + yeni özellikler | 4 dk | 6 | **0,7 dk'da bir** | 4,09 → 4,04 V |
| + düzeltme | 8 dk | 3 | 2,7 dk'da bir | 4,08 → 4,00 V |

Dikkat: kötü ölçüm **daha yüksek gerilimde** yapıldı. Yani sebep pil
değildi.

**İki tane vardı, ikisi de aynı hata:**

1. **Göz görevi her karede (50 ms) `guncelleme_durumu()` ve
   `ag_durumu()` çağırıyordu.** İkisi de kilit alıyor, ve aynı kilidi
   panel de alıyor (`/api/durum` → `guncelleme_json`).
2. **Tuş görevi 100 ms'de bir I2C okuyordu.** O hat ES8311 ile
   paylaşılıyor (`pati_pinler.h`: tek hat, üç aygıt).

İkisi de yazılırken "küçük bir çağrı" görünüyordu. Yorumları bile
vardı ve yorumlar yanlış değildi — sadece **çağrının kaç kez
yapıldığını** saymıyorlardı.

### Teşhisi getiren gözlem

Kullanıcı: *"bir ara gözler dondu hep aynı kaldı ama gözler donunca
hiç çökmedi."*

İki bilgi birden: donma, göz görevinin kilidi beklemesiydi. Ve göz
görevi durunca çökmenin durması, yükün oradan geldiğini söylüyordu.

Ayrıca çökmelerden biri `kesme_bekcisi` idi — brownout **değil**.
Kesme watchdog'u kesmelerin fazla uzun kapalı kaldığını söyler, yani
donanım değil zamanlama. O tek satır "bu bir akım sorunu değil"
diyordu.

### Kural

**Sıcak döngüye I2C, kilit ya da NVS koyma.** Bir çağrının ucuz olup
olmadığı tek başına anlamlı değil; saniyede kaç kez yapıldığıyla
çarpılmalı. Periyodik görev yazarken sorulacak soru:

> Bu döngü saniyede kaç kez dönüyor, ve içindeki her çağrı
> flash'a, I2C'ye ya da bir kilide dokunuyor mu?

Çare ikisinde de aynıydı: **seyrek yokla, sonucu sakla.** Durumlar
saniyede bir, tuş 500 ms'de bir ve anlık durum yerine M5PM1'in
biriken **bayrağı** okunuyor (böylece seyreltmek tık kaçırmıyor).

---

## 🔴 05.09.2026 — "Pati duyuyor ama konuşmuyor"

Belirti çocuk açısından basit: Pati cevap vermiyor. Cihaz tarafında
ise **her şey sağlıklı görünüyor** ve bu teşhisi saatlerce yanlış yere
çekti.

```
mikrofon        calisiyor        (tepe genlik yukseliyor)
ses Gemini'ye   ulasiyor
Gemini anliyor  "cocuk: Beni duyuyor musun?"   <- transkript DOGRU
websocket       acik, HATA YOK
hoparlor        saglam            (sentez ses testiyle kanitlandi)
cevap           HIC GELMIYOR
```

### Asıl sebep: Live API kotası

```
ws close: You exceeded your current quota
```

**Kota dakikalık** — bir süre beklenince kendiliğinden açılıyor
(ölçüldü: 16:46 dolu, 16:47:34 açık).

🔴 **Ve kendi kendini besliyor.** Kota dolunca bağlantı kopuyor, Pati
hemen yeniden bağlanıyor, **her yeniden bağlanma kotadan yiyor**, kota
bir türlü toparlanamıyor. Logdaki "bağlantı koptu (2. kez), (3. kez)"
zinciri bu.

⚠️ **`1011 'Internal error occurred.'` büyük olasılıkla kotanın örtülü
hâli.** Açık bir kota mesajı yerine sunucu iç hatası dönüyor. Bu koda
bakıp "Google'da geçici arıza" demek yanıltıcı oldu.

### Kotayı yanlış eleme yolları (ikisi de denendi, ikisi de yanlış)

**1. "Kota dolsaydı transkript de gelmezdi."** Yanlış. Girdi
transkripsiyonu çalışmaya devam ediyor; duran şey yalnızca yanıt
üretimi.

**2. REST ile `generateContent` denemek.** `gemini-3.1-flash-lite`'a
istek atıldı, 200 ve gerçek cevap döndü, "kota var" sonucu çıkarıldı.
**Live API'nin kotası ayrı bir havuz** — o test yanlış soruyu
cevaplıyor.

### Doğru sınama

Cihaza hiç dokunmadan, bilgisayardan ham WebSocket ile:

```
wss://generativelanguage.googleapis.com/ws/
  google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent?key=...
```

setup gönder → `setupComplete` bekle → bir metin turu gönder → cevap
gelirse Live API sağlıklı. Kota doluysa kapanış mesajında açıkça
yazıyor. Betik: `scratchpad/setup_test.ps1` biçiminde, birkaç satır.

### Aynı gün ayrıca bulunanlar (kotadan bağımsız, gerçek eksikler)

- **VAD ayarları Gemini'ye hiç gönderilmiyordu.**
  `ConversationConfig` bunları taşıyor, panel gösteriyor, ama
  `send_setup()` iletmiyordu. Sunucu varsayılanıyla çalışıyorduk.
- **`commit_audio()` boştu.** Gövdesi `return {}` idi ve gerekçesi
  "sunucu VAD'i turu kendisi bulur" diyordu. Bu gövdede sunucu VAD'i
  tetiklenemiyor: hoparlörle mikrofon 5 cm arayla, ortam sesi hiç
  kesilmiyor, beklenen 500 ms sessizlik hiç oluşmuyor.
- **Mikrofon aralıksız gönderiyordu** — sessizken de, gürültü dahil.

⚠️ **Bu üç düzeltmenin hiçbiri doğrulanamadı**, çünkü doğrulama
sırasında kota doluydu. Gerekçeleri sağlam ama ölçümleri eksik.

### Sırayla eleme (bir dahakine)

1. **Hoparlör** — açılışta sentez bir ses çal. Duyuluyorsa ses yolu
   tamamen elenir. (`ses_testi_cal`, geçiciydi, kaldırıldı.)
2. **Kota** — bilgisayardan ham WebSocket. Cihaza dokunmadan.
3. **Model** — aynı anahtarla başka bir Live modeli dene.
4. Ancak bundan sonra cihaz tarafı.

---

## Yolda yapılan yanlış teşhisler

Hepsi o an makul görünüyordu; tekrarlanmasın diye yazılı.

**"Pilde çöküyor, USB'de çökmüyor."** Yanlış. Sonra USB'de de çöktü.
Kullanıcı doğrusunu buldu: *hareket + akım çekimi birlikte*. O da tam
doğru değildi — asıl sebep bellekti.

**"Pil kontağı mekanik olarak kesintili."** Dolu pil (4,0 V), kısılmış
ses ve "sallayınca çöküyor" gözlemine dayanıyordu. Belirti gerçekti,
sebep değil.

**"Isınma olabilir."** Ölçüldü: 64,5 °C. Bu yonga için normal, PMIC'in
ısıl koruması bu sıcaklıkta devreye girmez. Sensör bu yüzden eklendi —
soru tekrar sorulursa cevap sayı olsun.

**"Yeniden bağlanma çalışmıyor."** Yanlış; grep satırları kaçırmıştı.
Çalışıyordu, 25-26 ms'de toparlıyordu.

---

## Bu belgeyi yazarken düşülen tuzaklar

**`sdkconfig` varsa `sdkconfig.defaults` OKUNMAZ.** `idf.py reconfigure`
de yetmiyor — dosyayı SİLMEK gerekiyor. `CONFIG_IDF_TARGET` zaten bu
yüzden defaults'ta duruyor, silmek güvenli.

**`CONFIG_MBEDTLS_DYNAMIC_BUFFER` "PSRAM'den al" demek DEĞİL.** O ayar
tamponları kullanılmadığında serbest bırakıyor, nereden alındığını
değiştirmiyor. PSRAM için `MBEDTLS_EXTERNAL_MEM_ALLOC` gerekiyor.
Yorumda yıllarca "PSRAM'den alınsın" yazıyordu ve alınmıyordu.

**Sıcaklık sensörünün aralığı keyfi seçilemiyor.** ESP32-S3 yalnızca
belirli aralıkları destekliyor (-10~80, -30~50, 20~100, 50~125);
eşleşmeyen aralık sessizce reddediliyor. `(-10, 110)` yazılmıştı ve
sensör hiç kurulmuyordu.

**Hata kodu basılmayan uyarı işe yaramıyor.** "sicaklik sensoru
kurulamadi" tek başına sebebi göstermiyordu; üstelik tek sefer basıldığı
için sonradan bakıldığında günlükte hiç yoktu.

**M5PM1'in sürücü başlığı bu yongada birebir tutmuyor.** `VBAT` üst
baytı için "high 4 bits" yazıyor; maskelenince pil 4130 yerine 34 mV
çıkıyor. İki bayt da tam kullanılıyor. `PWR_SRC` (0x04) şeması da
tutmuyor — USB takılıyken 0x05 okunuyor. Bu yüzden güç kaynağı kararı
**VIN gerilimine** bakılarak veriliyor: ölçülebilir ve anlamı kendinden
belli.


## 05.09.2026 — konuşma tasarrufu ve yanıltıcı gerilim alanı

Gemini, ilk ses paketinden önce Speaking olayı gönderiyor. Bu bayrak,
paket yolundaki göz geçişini atlatıyordu; yüz adına bağlanan 5 fps
kararı çalışmayabiliyordu. Tasarruf artık ses sürücüsünün DMA süresine
bağlı ve panel aynı etkin hedefi gösteriyor. Ayrıntı ve kartta kabul
ölçümü `PIL.md` içinde.

`guc.cokme_mv` çökme sırasında alınmıyor: **sonraki açılışta** okunup
NVS'e yazılıyor. Alanın eski açıklaması yanlıştı. Bu değerden rayın
anlık minimumu hesaplanamaz. `cokme_mv_ani=yeniden_acilis` anlamı açıklar.
Ayrıca ESP32 sıcaklık sensörü, PMIC/regülatör sıcaklığını ölçmez;
yonga sıcaklığını kullanarak onların ısıl koruması elenemez.

## 05.09.2026 — bağlantı ve gecikme perdesi

Wi-Fi yoksa mevcut “WiFi aranıyor” görünür. Wi-Fi varken sohbet
bağlantısı iki saniyeden uzun kuruluyorsa “Bağlanıyorum” görünür.
Cevap bekleme beş saniyeyi aşarsa “Cevap gecikiyor” görünür; bu
internet arızası teşhisi değildir, sunucu da gecikebilir. Gemini
konuşma sonunu geç bildirdiğinden mevcut yerel ses bekçisi de
800 ms sessizlikten sonra beklemeyi başlatır; bu yolda uyarı yaklaşık
5,8 saniyede çıkar. Ses gelince, tur bitince veya uykuya girince kalkar.

Durum atomik bildirilir; göz döngüsüne ağ sorgusu veya I2C eklenmez.
Wi-Fi, bilgi ve güncelleme perdeleri önceliklidir. 3.0.11 pil koşusunda
sayaç 24 → 25 ve brownout kaydedildi; gecikme uyarısı bu besleme
sorununun düzeltildiği anlamına gelmez.

3.0.13: gecikme/yeniden bağlantı uyarısında sinyal 1–2 çubuksa
“Wi-Fi zayıf / Modeme yaklaş” gösterilir. Sinyal daha iyi veya
bilinmiyorsa gecikmede “İnternet yavaş / olabilir, bekle”, yeniden
bağlantıda “Bağlanıyorum / Biraz bekle” görünür. RSSI internet hızı
ölçümü değildir; sıfır/bilinmiyor zayıf sinyal diye yorumlanmaz.
Sinyal mevcut iki saniyelik gözcüden gelir; göz döngüsü ağ sorgulamaz.

### 3.0.14 — sürekli uyarı gerilemesi

3.0.12–13 yerel ses bekçisinden ekran uyarısı başlatıyordu. Ortam
sesi gerçek bir soru olmadığı hâlde uyarı açabiliyor; sorun sürdükçe
perde gözleri kapatıyordu. Bu ekran tetiklemesi kaldırıldı; mevcut
bağlantı kurtarma bekçisi değişmedi. Yalnızca sohbet olayları uyarı
açar: 8 saniye beklenir, 3 saniye gösterilir, en az 60 saniye yeni
uyarı bastırılır. Wi-Fi tamamen yoksa eski Wi-Fi perdesi geçerlidir.
İki çubuk artık modeme yaklaş önerisi vermez; tek çubukta verilir.
Gecikme metni “Cevap gecikti / Biraz bekle”; internet hızı ölçülmedi.

Panelde ag.rssi_dbm (0=okunamadı), ag.tx_ceyrek_dbm (-1=okunamadı)
ve ag.tasarruf (-1=okunamadı, 0=uyku tasarrufu kapalı) eklendi. TX
değeri sürücü sınırıdır, anlık yayılan güç ölçümü değildir. Mevcut
derleme PHY üst sınırı 20 dBm; güç azaltma ve modem uykusu kapalı.
