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
// Mikrofon, hoparlor ve LCD arka isigi dogrudan ESP32'den DEGIL, M5PM1
// guc yonetim yongasinin actigi "L3B" katmanindan besleniyor. Ve L3B
// acilista KENDILIGINDEN GELMIYOR.
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

#define PATI_I2C_HZ (400 * 1000)

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
#define PATI_PM1_L3B 2  // PYG2 — mikrofon + hoparlor + LCD arka isik gucu
#define PATI_PM1_AMF 3  // PYG3 — AW8737 hoparlor amfisi anahtari

// ---------------------------------------------------------------------------
// Ses — ES8311 kodek, MEMS mikrofon, AW8737 amfi
// ---------------------------------------------------------------------------
//
// 🔴 DIN/DOUT ADLARI KODEGIN AGZINDAN. M5Stack'in tablosu ES8311
// satirini etiketliyor, ESP32 satirini degil:
//
//     ESP32-S3 |  G18 |  G14 |  G17 |  G15 |  G16
//     ES8311   | MCLK | DOUT | BCLK | LRCK | DIN
//
// Kodegin DOUT'u ESP32'nin GIRISI, kodegin DIN'i ESP32'nin CIKISI.
// Tabloyu oldugu gibi kopyalayan biri mikrofonla hoparloru ters
// baglar ve iki taraf da sessiz kalir. Asagidaki isimler ESP32'nin
// agzindan yazildi.

#define PATI_SES_MCLK GPIO_NUM_18
#define PATI_SES_BCLK GPIO_NUM_17
#define PATI_SES_LRCK GPIO_NUM_15
#define PATI_SES_DIN  GPIO_NUM_14  // kodek DOUT -> ESP32 girisi (mikrofon)
#define PATI_SES_DOUT GPIO_NUM_16  // ESP32 cikisi -> kodek DIN  (hoparlor)

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
// Yan taraftaki guc dugmesi M5PM1'in kendi kontrolunde ve ESP32'ye
// gorunmuyor: tek tik = ac/sifirla, cift tik = kapat, uzun basma =
// indirme modu. Yazilimdan degistirilemiyor, degistirilmesi de
// gerekmiyor.
//
// Asagidaki ikisi programlanabilir olanlar. Cekme direncleri L2
// katmaninda, yani acilisla birlikte hazirlar.
#define PATI_TUS_1 GPIO_NUM_11
#define PATI_TUS_2 GPIO_NUM_12

// ---------------------------------------------------------------------------
// Bos kalanlar
// ---------------------------------------------------------------------------
//
//   G9  / G10 : Grove (HY2.0-4P). Ileride servo ya da sensor icin.
//               ⚠️ Grove'un tasiyabilecegi en fazla yuk 4,88 V @ 0,38 A.
//               Servo BURADAN BESLENMEZ — ayri besleme, sadece GND
//               ortak. M5Stack kendi belgesinde cikis modundaki bir
//               arayuzden guc vermenin kisa devre ve cihaz hasari
//               riski tasidigini yaziyor.
//   G46 / G42 : kizilotesi verici / alici. Pati kullanmiyor.
//               (Kullanilsaydi: alicinin calismasi icin hoparlor
//               amfisinin KAPALI olmasi gerekiyor.)
//   Hat2-Bus  : ust taraftaki 16 pinlik genisleme yolu.
