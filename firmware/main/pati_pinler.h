// Pati donanim tanimlari — M5Stack StickS3 (SKU K150)
//
// BU DOSYA TEK KAYNAK. Pin numarasi baska hicbir yere yazilmayacak;
// iki yerde iki numara olursa hangisinin dogru oldugu anlasilmiyor.
//
// Tablo docs.m5stack.com/en/core/StickS3 icindeki PinMap bolumunden
// satir satir alindi (23.08.2026). Onceki karttan (ESP32-S3-DevKitC
// klonu + INMP441 + MAX98357) farkli olarak bu kartta hicbir sey
// lehimlenmiyor: mikrofon, hoparlor, ekran, pil ve butonlar govdenin
// icinde ve sabit.
//
// ⚠️ ONCEKI DONANIMIN PIN HARITASI ICIN: git show v2.2.8-devkit:firmware/main/pati_pinler.h

#pragma once

#include <driver/gpio.h>

// ---------------------------------------------------------------------------
// 🔴 EN ONEMLI SEY: L3B GUC KATMANI
// ---------------------------------------------------------------------------
//
// Mikrofon, ES8311 kodek ve LCD arka isigi dogrudan ESP32'den DEGIL, M5PM1
// guc yonetim yongasinin actigi "L3B" katmanindan besleniyor. Ve L3B
// acilista KENDILIGINDEN GELMIYOR.
// Hoparlor amfisi AW8737A ise VBUS_L0'dan beslenir (K150 V0.6, s.3).
// Kodek kapaliyken ses gelmemesi, amfinin de L3B'de oldugu anlamina gelmez.
//
// M5Stack'in belgesi (arduino/m5sticks3/m5pm1, "Multi-Level Power
// Switch Design"): "After the M5PM1 starts up and powers on, L0, L1 and
// L2 will be automatically enabled. During the M5Unified initialization
// process, L3A and L3B will be further enabled."
//
// Yani L3B'yi Arduino tarafinda M5Unified aciyor. Biz ciplak ESP-IDF
// kullaniyoruz, M5Unified yok — ACMAZSAK KENDIMIZ ACMAK ZORUNDAYIZ.
//
// Acmazsak ne olur: ekran siyah, mikrofon sagir, hoparlor sessiz. Ucu
// birden. Ve hicbiri hata vermez — I2S kanali sorunsuz acilir, SPI
// sorunsuz yazar, kod her yerde ESP_OK doner. Yani tipatip bir YAZILIM
// hatasi gibi gorunur ve gunlerce orada aranir.
//
// Onceki kartta olu ekran icin harcanan gunlerin sebebi buna cok
// benziyordu; bu sefer sebep belgelenmis halde duruyor.
//
// Kod: pati_guc.cpp · guc_baslat()

// ---------------------------------------------------------------------------
// I2C — UC AYGIT AYNI HATTI PAYLASIYOR
// ---------------------------------------------------------------------------
//
// Tek hat, uc adres. Hepsi ayni iki pinde:
//
//   0x18  ES8311   ses kodegi (mikrofon + hoparlor)
//   0x68  BMI270   6 eksen IMU        (Pati kullanmiyor)
//   0x6E  M5PM1    guc yonetimi       (L3B ve amfi anahtari)
//
// ES8311'in burada olmasi onemli: ses yolunun YAPILANDIRMASI I2C'den,
// VERISI I2S'ten gidiyor. Ikisi ayri hat, ikisi de kurulmali.

#define PATI_I2C_SCL GPIO_NUM_48
#define PATI_I2C_SDA GPIO_NUM_47

// 🔴 100 kHz, 400 DEGIL — GERCEK KARTTA OLCULDU (23.08.2026).
//
// Once 400 kHz yazilmisti ("400 kHz standart, herkes kullaniyor") ve
// kart gelince su cikti: M5PM1 ADRESINE cevap veriyor (yoklama geciyor)
// ama ilk register okumasi NACK yiyor. IDF'in hatasi
// ESP_ERR_INVALID_STATE — "islem tamamlanmadi", yani NACK
// (esp_driver_i2c/i2c_master.c:727).
//
// Sonuc zinciri sessizdi ve tam da korktugumuz gibiydi:
//   M5PM1 yaziamiyor -> L3B acilmiyor -> mikrofon, hoparlor ve ekran
//   arka isigi beslenmiyor -> ES8311 0x18'de cevap vermiyor -> "yanlis
//   donanim" sanildi. Ekran simsiyahti.
//
// Sebep M5PM1'in kendi surucusunde yaziyor (github.com/m5stack/M5PM1,
// M5PM1.cpp): `_requestedSpeed = M5PM1_I2C_FREQ_100K` ve
// `_i2cConfig.speed400k = false`. Yani yonga 100 kHz'de baslıyor;
// 400 kHz AYRICA ACILMASI gereken bir kip.
//
// ES8311 zaten 100 kHz'i destekliyor ve hattaki tek is acilistaki
// register kurulumu — birkac milisaniye fark eder, hicbir sey
// kaybetmiyoruz.
#define PATI_I2C_HZ (100 * 1000)

#define PATI_ADR_ES8311 0x18
#define PATI_ADR_M5PM1  0x6E

// M5PM1 register haritasi — M5PM1 surucu kutuphanesinin basligindan
// (github.com/m5stack/M5PM1, src/M5PM1.h). Kutuphaneyi bagimlilik
// olarak almiyoruz: dort register yazmak icin Arduino katmani ve eski
// I2C surucusu (espressif/i2c_bus) getirmeye degmez.
#define PATI_PM1_GPIO_MODE  0x10  // [4:0] yon      1 = cikis
#define PATI_PM1_GPIO_OUT   0x11  // [4:0] seviye   1 = yuksek
#define PATI_PM1_GPIO_DRV   0x13  // [4:0] surus    0 = push-pull
#define PATI_PM1_GPIO_FUNC0 0x16  // pin basina 2 bit, 0b00 = GPIO

// M5PM1'in kendi GPIO'lari (ESP32'nin degil).
// Guc kaynagi ve pil gerilimi — ayni surucu basligindan
// (github.com/m5stack/M5PM1, src/M5PM1.h).
//
// PWR_SRC'nin [2:0] biti su an sistemi NEYIN besledigini soyluyor:
//   0 = 5VIN (USB/DC)   1 = 5VINOUT   2 = pil   3 = bilinmiyor
//
// 🔴 "USB TAKILI MI" ILE "SISTEM USB'DEN MI BESLENIYOR" AYNI SEY DEGIL.
// M5PM1 pil doluyken sistemi pilden besleyip USB'yi yalnizca sarja
// ayirabiliyor. Ses seviyesi karari icin onemli olan IKINCISI, cunku
// akimi cekecek olan kaynak o. Bu yuzden VBUS pinine degil bu
// register'a bakiyoruz.
// GERCEK KARTTA OLCULEREK DOGRULANDI (01.09.2026, USB takiliyken):
//     0x20/21 VREF = 0x0CED = 3309 mV   -> 3,3 V rayi
//     0x22/23 VBAT = 0x1022 = 4130 mV   -> dolu lityum hucre
//     0x24/25 VIN  = 0x1314 = 4884 mV   -> USB 5 V (kablo dususuyle)
//
// ⚠️ IKI BAYT DA TAM KULLANILIYOR, 16 bit kucuk-sonlu, birimi mV.
// Surucu basligi ust bayt icin "high 4 bits" diyor ve ONA GUVENILDI:
// maskelenince pil 4130 yerine 34 mV cikti. Once oyle yazildi, dokum
// alininca duzeltildi — baslik bu yongada birebir tutmuyor.
//
// PWR_SRC (0x04) KULLANILMIYOR. Basliga gore [2:0] biti 0=5VIN, 1=
// 5VINOUT, 2=pil olmali; gercek kartta USB takiliyken 0x05 okundu, yani
// o sema da tutmuyor. VIN gerilimine bakmak hem olculebilir hem de
// anlami kendinden belli: haricî 5 V var mi, yok mu.
#define PATI_PM1_VBAT_L  0x22  // pil gerilimi, mV, kucuk-sonlu 16 bit
#define PATI_PM1_VBAT_H  0x23
#define PATI_PM1_VIN_L   0x24  // haricî 5 V girisi, mV, kucuk-sonlu 16 bit
#define PATI_PM1_VIN_H   0x25

// Sistem komutu. Ust dort bit ANAHTAR ve 0xA olmak zorunda; alt iki bit
// komut: 01 = kapat, 10 = sifirla, 11 = indirme modu.
// Yani kapatmak icin yazilacak deger 0xA1.
#define PATI_PM1_SYS_CMD 0x0C
#define PATI_PM1_KAPAT   0xA1

// ---------------------------------------------------------------------------
// UYANMA SEBEBI — "kapaliyken sarja takinca kendi kendine aciliyor"
// ---------------------------------------------------------------------------
//
// Kullanicinin sikayeti (12.09.2026): yan dugmeyle TAMAMEN kapatilan
// Pati, USB takilinca kendiliginden aciliyor.
//
// 🔴 BUNU KAPATAN BIR AYAR YOK. M5PM1'in yazmac haritasinin tamami
// tarandi (github.com/m5stack/M5PM1 · src/M5PM1.h):
//   - IRQ_STATUS2 [0] "5VIN insertion" olayi yalnizca BILDIRIYOR
//   - IRQ_MASK2 kesmeyi susturuyor, guc dizisini durdurmuyor
//   - HOLD_CFG kapanista zaten 0x00'a donuyor
//   - WAKE_SRC bir DURUM yazmaci ("write 0 to clear"), izin maskesi degil
// Yani acilma M5PM1'in donanim davranisi ve yazilimdan engellenemiyor.
//
// GERIYE KALAN YOL: acilista "beni ne uyandirdi" diye sorup, cevap
// "VIN takildi" ise hemen geri kapanmak. Once bu yazmacin gercek
// kartta ne dondugu OLCULECEK — bit yanlis okunursa Pati hic
// acilamaz hale gelir.
//
// [6] 5VINOUT takildi   [5] haricî GPIO   [4] komutla sifirlama
// [3] sifirlama dugmesi [2] GUC DUGMESI   [1] VIN TAKILDI
// [0] zamanlayici
#define PATI_PM1_WAKE_SRC 0x05
#define PATI_PM1_WAKE_VIN 0x02  // [1]
#define PATI_PM1_WAKE_BTN 0x04  // [2]

// Yan guc dugmesi.
#define PATI_PM1_BTN_STATUS 0x48  // [0] basili mi, [7] basildi bayragi
#define PATI_PM1_BTN_CFG_1  0x49  // [7] indirme kilidi, [4:3] uzun basma
                                  // esigi (00=1s..11=4s), [0] tek tik
                                  // sifirlamayi kapat

#define PATI_PM1_L3B 2  // PYG2 — mikrofon + kodek + LCD arka isik gucu
#define PATI_PM1_AMF 3  // PYG3 — AW8737A hoparlor amfisi anahtari

// StickS3 şeması U19: AW8737A. M5PM1 darbeleri kendi üretir; I2C'den
// 0,75–10 us aralıkla pin çevirmek mümkün değildir. Üç ek darbe ve
// son yükselen kenar Mode4'ü seçer (8 ohm için nominal 0,6 W NCN).
#define PATI_PM1_AMFI_DARBE 0x53  // [7] uygula, [6:5] ek darbe, [4:0] PYG
#define PATI_AMFI_KIP 4

// ---------------------------------------------------------------------------
// Ses — ES8311 kodek, MEMS mikrofon, AW8737A amfi
// ---------------------------------------------------------------------------
//
// 🔴 DIN/DOUT — BURADA BIR KEZ YANLIS OKUNDU, DIKKAT.
//
// M5Stack'in tablosu:
//
//     ESP32-S3 |  G18 |  G14 |  G17 |  G15 |  G16
//     ES8311   | MCLK | DOUT | BCLK | LRCK | DIN
//
// Satir "ES8311" diye etiketli, bu yuzden ilk okuyusta "DOUT kodegin
// cikisi, yani ESP32'nin GIRISI" diye alindi: din=G14, dout=G16.
// 01.09.2026'da gercek kartta mikrofon TAM SIFIR okudu ve hoparlorden
// hic ses cikmadi — iki yon birden olu.
//
// Dogru okuyus ayni sayfanin LCD satirindan anlasiliyor:
//
//     ST7789P3 | MOSI | SCK | RS | CS | RST | BL
//
// Oradaki "MOSI" ekranin cikisi degil, ESP32'nin cikisi. Yani M5Stack
// satiri cevre birimiyle etiketliyor ama SINYAL ADLARINI ESP32'nin
// agzindan yaziyor. Ayni kural I2S satirinda da gecerli:
//
//     G14 = ESP32'nin DOUT'u  -> hoparlor
//     G16 = ESP32'nin DIN'i   -> mikrofon
//
// Asagidaki isimler ESP32'nin agzindan.

#define PATI_SES_MCLK GPIO_NUM_18
#define PATI_SES_BCLK GPIO_NUM_17
#define PATI_SES_LRCK GPIO_NUM_15
#define PATI_SES_DIN  GPIO_NUM_16  // ESP32 girisi  <- kodek (mikrofon)
#define PATI_SES_DOUT GPIO_NUM_14  // ESP32 cikisi  -> kodek (hoparlor)

// 🔴 TEK KANAL, TEK HIZ — ONCEKI KARTTAN EN BUYUK FARK.
//
// Onceki Pati'de mikrofon ve hoparlor AYRI yongalardi (INMP441 ve
// MAX98357), yani ayri I2S kanallarinda ayri hizlarda kosabiliyorlardi:
// giris 16 kHz, cikis 24 kHz. Burada ikisi de ayni ES8311'in icinde ve
// ayni I2S hattini paylasiyorlar. TEK saat var; iki hiz mumkun degil.
//
// 48 kHz secildi ve secim keyfi degil, iki tarafi da TAM BOLEN tek
// deger oldugu icin:
//
//   Hoparlor : Gemini 24 kHz veriyor, 1.30x hizli calinacak, yani
//              saniyede 31.200 kaynak ornegi tuketiliyor. 48 kHz'de
//              her cikis ornegi kaynakta 31200/48000 = 0,65 ilerliyor.
//              Adim 1'DEN KUCUK, yani ARA DEGER URETIYORUZ. Katlanma
//              (aliasing) yok — seyreltme olsaydi olurdu.
//
//   Mikrofon : 48000 / 16000 = 3. Tam sayi. Ucer ornegin ortalamasi
//              hem seyreltiyor hem alcak geciren suzgec gorevi
//              goruyor. Kesirli bir oran olsaydi cok daha pahali bir
//              suzgec gerekirdi.
//
// 24 kHz secseydik mikrofon 24->16 = 2/3 kesirli oran olurdu.
// 16 kHz secseydik hoparlor 24->16 SEYRELTME olurdu ve Pati'nin sesi
// katlanma gurultusuyle bozulurdu — yani tam da korumaya calistigimiz
// sey giderdi.
#define PATI_SES_HZ 48000

// Mikrofonun hangi I2S yuvasindan okunacagi.
//
// 01.09.2026: SOL yuva gerçek kartta çalıştı. Önceki sıfır veri
// belirtisi DIN/DOUT tersliğinden kaynaklanıyordu; sağ yuva varsayımı
// doğrulanmadı ve kullanılmıyor.
#define PATI_SES_GIRIS_YUVA I2S_STD_SLOT_LEFT

// Kodege MCLK'i biz veriyoruz (ES8311 kole). 256 x 48 kHz = 12,288 MHz,
// standart bir deger.
#define PATI_SES_MCLK_KAT 256

// Gemini'nin konustugu hizlar. Kart degisti, bunlar degismedi —
// sunucunun ne verdigi ve ne istedigi donanimdan bagimsiz.
#define PATI_GEMINI_GIRIS_HZ 16000  // Gemini'ye gonderdigimiz
#define PATI_GEMINI_CIKIS_HZ 24000  // Gemini'den gelen

// ---------------------------------------------------------------------------
// Ekran — ST7789P3, 135x240
// ---------------------------------------------------------------------------
//
// Onceki karttaki modulun aksine bu ekran govdenin icinde ve CS pini
// gercekten bagli (G41). Onceki dosyada "CS'in GND'ye bagli oldugunu
// varsayiyoruz" diye bir not vardi ve o varsayim hic dogrulanamadi;
// burada varsayim yok.

#define PATI_EKR_SCK  GPIO_NUM_40
#define PATI_EKR_MOSI GPIO_NUM_39
#define PATI_EKR_CS   GPIO_NUM_41
#define PATI_EKR_DC   GPIO_NUM_45  // M5Stack tablosunda "RS"
#define PATI_EKR_RST  GPIO_NUM_21
#define PATI_EKR_BLK  GPIO_NUM_38  // arka isik — GUCU L3B'DEN GELIYOR

#define PATI_EKR_HZ (40 * 1000 * 1000)

// 🔴 YATAY KULLANIYORUZ. Panel fiziksel olarak 135 genis, 240 yuksek
// (dikey). Gozler yan yana duruyor ve iki goz + aradaki bosluk 178
// piksel yer kapliyor — 135'e SIGMAZ. Cevirince 240 genislik geliyor
// ve gozlerin YATAY olculeri hic degismiyor; yalnizca dikey merkez
// kayiyor.
//
// Ayrica cihaz zaten bir cubuk: yatay tutulunca bir yuz.
//
// Surucude swap_xy + mirror ile yapiliyor (pati_ekran.cpp).
#define PATI_EKR_G 240
#define PATI_EKR_Y 135

// ST7789P3'un denetleyicisi 240x320'lik bir cerceve belleğine sahip
// ama panel 135x240. Goruntu bellegin ortasina dusuyor, yani bir
// KAYMA var. Dikeyde 52, yatayda 40. Cevirince ikisi yer degistiriyor.
//
// Yanlissa goruntu birkac piksel kayik cikar ve kenarda cop gorunur —
// ilk acilista bakilacak yer burasi.
#define PATI_EKR_KAYMA_X 40
#define PATI_EKR_KAYMA_Y 52

// ---------------------------------------------------------------------------
// Butonlar
// ---------------------------------------------------------------------------
//
// 🔴 BURADA "YAN DUGME ESP32'YE GORUNMUYOR, YAZILIMDAN
// DEGISTIRILEMIYOR" YAZIYORDU VE YANLISTI.
//
// Ikisi de yanlis. M5PM1'in register haritasi (github.com/m5stack/M5PM1,
// src/M5PM1.h) sunlari veriyor:
//
//   0x48 BTN_STATUS  [0] dugme su an basili mi, [7] basildi bayragi
//   0x49 BTN_CFG_1   [7] indirme modu kilidi, [6:5] cift tik araligi,
//                    [4:3] uzun basma esigi, [0] tek tik sifirlamayi kapat
//   0x4A BTN_CFG_2   [0] cift tikla kapanmayi kapat
//
// Yani dugmenin durumu I2C'den OKUNABILIYOR ve zamanlamalari
// AYARLANABILIYOR. O satir olculmeden yazilmisti ve bir yeteneği
// gorunmez kilmisti.
//
// M5PM1'in kendi davranisi: tek tik = ac/sifirla, cift tik = kapat,
// uzun basma = indirme modu.
//
// Pati tek basış bayrağını seyrek okuyup guc_kapat() çağırır.
// Uzun basış indirme moduna ayrılır. Çift tıkla kapanma da bağımsız
// kurtarma yolu olarak açık kalır.
//
// Asagidaki ikisi programlanabilir olanlar. Cekme direncleri L2
// katmaninda, yani acilisla birlikte hazirlar.
#define PATI_TUS_1 GPIO_NUM_11
#define PATI_TUS_2 GPIO_NUM_12

// ---------------------------------------------------------------------------
// BEDEN — 2 DC motor + 2 servo, ayri beslemeli
// ---------------------------------------------------------------------------
//
// StickS3 bir gövdeye takilabiliyor: iki tekerlek (sari TT redüktörlü
// motor, L9110S surucu) ve iki kol (SG90 sinifi mini servo). Gövdenin
// kendi 4'lu AA pil yuvasi var.
//
// ===========================================================================
// 🔴 AA PILIN (+) UCU BU KARTA HICBIR SEKILDE GELMIYOR
// ===========================================================================
//
// Gövdeden Stick'e giden sekiz kablonun hepsi ya TOPRAK ya da Stick'in
// KENDI CIKISI. 6 V hatti gövdenin icinde kaliyor, konnektöre hic
// ugramiyor.
//
// Sebebi asagidaki Hat2-Bus dizilisi: sinyal pinlerinin arasinda BAT
// (lityum hucrenin kendi ucu), 5V_IN, 3V3_L2 ve EXT_5V duruyor. Oraya
// 6 V girerse hucre ve M5PM1 gider ve geri donusu kutuyu acmak.
//
// Bu bir uyari degil, PIN SECIMININ SEBEBI: kural boyle kurulunca bir
// kablo yanlis pine kaysa bile en kotu ihtimal "motor surekli donuyor"
// oluyor — yakmiyor.
//
// Ayri besleme ayrica pazarlik konusu degil: 250 mAh'lik hucre
// 01.09.2026'da YALNIZCA hoparlor akimiyla brownout yasadi (TESHIS.md).
// Motorlari ayni raydan beslemek Pati'yi her harekette kapatirdi.
//
// ===========================================================================
// Hat2-Bus — ustteki 16 pinlik baslik
// ===========================================================================
//
// docs.m5stack.com/en/core/StickS3 · PinMap · Hat2-Bus (06.09.2026):
//
//        SOL              SAG
//   GND    1  ────────  2   G5     sol motor ileri
//   EXT_5V 3  ────────  4   G4     sol motor geri
//   Boot   5  ────────  6   G6     sag motor ileri
//   G1     7  ────────  8   G7     sag motor geri
//   G8     9  ──────── 10   G43
//   BAT   11  ──────── 12   G44
//   3V3_L2 13 ──────── 14   G2     beden algilama
//   5V_IN 15  ──────── 16   G3
//
// Sira keyfi degil, uc gerekce var:
//
//   1. MOTORLAR HEP SAG SUTUNDA. Sag sutunun tamami GPIO — orada tek
//      bir guc pini yok. Motor kablosunun yanlislikla BAT'a dusmesi
//      imkansiz.
//
//   2. SERVOLAR SOL SUTUNDA (7 ve 9). Sol sutunda guc pinleri var, ama
//      bir servo sinyali kayip BAT'a duserse servo yalnizca gecersiz
//      darbe gorur ve durur. Motor kablosu kaysaydi motor SUREKLI
//      donerdi — yani Pati masadan duserdi. Riski, sonucu daha
//      zararsiz olan uca kaydirdik.
//
//   3. G43 / G44 BILEREK BOS. ESP32-S3'un ROM UART0'i orada ve HER
//      ACILISTA G43'ten onyukleyici copu cikiyor. Motor girisine bagli
//      olsaydi Pati her sifirlamada segirirdi — ustelik Pati brownout
//      yuzunden sik sifirlaniyor (PIL.md).
//
// G0 (Boot) ve G3 strapping pini oldugu icin de kullanilmiyor; yedi pin
// zaten yetiyor.

// ---- DC motorlar: L9110S cift kanal ---------------------------------------
//
// L9110'un lojik girisleri 2,5 V ustunu "yuksek" sayiyor, yani ESP32'nin
// 3,3 V'u yetiyor; besleme araligi 2,5-12 V ve AA paketinin 6 V'u tam
// ortada. Kanal basina 800 mA surekli.
//
// Yon "hangi girise PWM verildigi" ile seciliyor: ileri icin A pini
// darbeli ve B pini sifir, geri icin tersi. Ikisi birden yuksek olursa
// motor frenler; kodda bu durum hic uretilmiyor.
#define PATI_BEDEN_SOL_ILERI  GPIO_NUM_5   // L9110 A-1A
#define PATI_BEDEN_SOL_GERI   GPIO_NUM_4   // L9110 A-1B
#define PATI_BEDEN_SAG_ILERI  GPIO_NUM_6   // L9110 B-1A
#define PATI_BEDEN_SAG_GERI   GPIO_NUM_7   // L9110 B-1B

// ---- Kollar: SG90 sinifi mini servo ---------------------------------------
//
// Yalnizca SINYAL burada; besleme AA paketinden geliyor.
#define PATI_BEDEN_KOL_SOL    GPIO_NUM_1
#define PATI_BEDEN_KOL_SAG    GPIO_NUM_8

// ---- Beden algilama -------------------------------------------------------
//
// Iceri cekmeli (pull-up) giris. Beden yokken pin havada -> 1 okunuyor;
// takilinca gövde topragina baglanip 0 oluyor. Sifir ek parca, tek
// kablo.
//
// ⚠️ BUNUN BILMEDIGI SEY: AA pil yuvasinin anahtari acik mi. "Beden
// takili" diyor, "beden calisiyor" demiyor. Kapali anahtarla takili bir
// bedende panel kumandayi gosterir ama hicbir sey oynamaz — panel bunu
// yaziyor.
#define PATI_BEDEN_ALGILA     GPIO_NUM_2

// ---------------------------------------------------------------------------
// Bos kalanlar
// ---------------------------------------------------------------------------
//
//   G9  / G10 : Grove (HY2.0-4P). Sensor icin.
//               ⚠️ Grove'un tasiyabilecegi en fazla yuk 4,88 V @ 0,38 A.
//               Servo BURADAN BESLENMEZ — ayri besleme, sadece GND
//               ortak. M5Stack kendi belgesinde cikis modundaki bir
//               arayuzden guc vermenin kisa devre ve cihaz hasari
//               riski tasidigini yaziyor.
//   G46 / G42 : kizilotesi verici / alici. Pati kullanmiyor.
//               (Kullanilsaydi: alicinin calismasi icin hoparlor
//               amfisinin KAPALI olmasi gerekiyor.)
//   G43 / G44 : Hat2-Bus'ta ama ROM UART0 — yukaridaki 3. gerekce.
//   G0 / G3   : strapping.
