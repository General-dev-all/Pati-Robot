// Pati donanim tanimlari — ESP32-S3-DevKitC-1 N16R8 (YD klonu)
//
// BU DOSYA TEK KAYNAK. Pin numarasi baska hicbir yere yazilmayacak;
// yanlis yere bagli bir kablo saatler yakiyor ve iki yerde iki numara
// olursa hangisinin dogru oldugu anlasilmiyor.
//
// Tablo PLAN.md'den geliyor ve ürün sayfasindaki pin tablosu satir
// satir sayilarak dogrulandi (30.07.2026).

#pragma once

#include <driver/gpio.h>

// ---------------------------------------------------------------------------
// KULLANILMAYACAK PINLER — buraya bir sey baglamak bos zaman harcamaktir
// ---------------------------------------------------------------------------
//
//   GPIO35 / 36 / 37 : OKTAL PSRAM hatti. Header'da lehimli duruyorlar
//                      ama olu. Espressif user_guide_v1.1.rst satir 80:
//                      "not available for external use".
//                      Kartimiz N16R8 = oktal PSRAM, yani bu gecerli.
//   GPIO19 / 20      : USB-OTG (D- / D+)
//   GPIO43 / 44      : UART TX / RX (programlama portu)
//   GPIO0 / 45 / 46  : strapping / boot
//
// GPIO48'de yerlesik WS2812B RGB LED var — o KULLANILIYOR, asagida.

// ---------------------------------------------------------------------------
// INMP441 I2S mikrofon  (cocugun sesi)
// ---------------------------------------------------------------------------
//
// Besleme: VDD -> 3V3   !!! 5V verilirse modul YANAR !!!
//          GND -> GND
//          L/R -> GND   (sol kanal. BOSTA KALIRSA veri gelmez.)
//
// Ipucu: L/R'yi modulun uzerinde kendi GND teline birlestir, karta tek
// dupont ile gel. Kartta yalnizca 4 GND pini var ve dördü de dolu —
// boylece bir tanesi serbest kalir.

#define PATI_MIK_SCK GPIO_NUM_4  // bit clock  (BCLK)
#define PATI_MIK_WS  GPIO_NUM_5  // word select (LRCL)
#define PATI_MIK_SD  GPIO_NUM_6  // veri: mikrofon -> ESP32

// ---------------------------------------------------------------------------
// MAX98357 I2S amfi  (Pati'nin sesi)
// ---------------------------------------------------------------------------
//
// Besleme: VIN -> 5V  ·  GND -> GND
//
// GAIN : HICBIR SEYE BAGLANMIYOR -> 9 dB varsayilan.
//        Ses seviyesi yazilimdan ayarlaniyor (prototype/ses.py'deki carpanin
//        aynisi), donanimdan kismaya gerek yok.
// SD   : bagli degil. Modulun kendi pull-up'i amfiyi acik tutuyor.
//        Ses HIC gelmiyorsa ilk bakilacak yer burasi.
//
// !!! HOPARLORUN HICBIR UCU GND'YE GITMEZ !!!
// Cikis kopru bagli (bridge-tied); bir ucu topraga degerse amfi susar
// ya da bozulur.

#define PATI_AMFI_BCLK GPIO_NUM_15
#define PATI_AMFI_LRC  GPIO_NUM_16  // word select
#define PATI_AMFI_DIN  GPIO_NUM_17  // veri: ESP32 -> amfi

// ---------------------------------------------------------------------------
// Yerlesik WS2812B RGB LED — bedava durum gostergesi
// ---------------------------------------------------------------------------
//
// Ekran gelmeden once Pati'nin tek hayat belirtisi bu. Ek parca yok,
// kart uzerinde zaten var.

#define PATI_LED GPIO_NUM_48

// ---------------------------------------------------------------------------
// Ekran (1.3" IPS 240x240, ST7789VW, 7 pin SPI)
// ---------------------------------------------------------------------------
//
// Besleme: VCC -> 3V3   !!! 5V DEGIL !!!
//          GND -> GND
//
// PIN SECIMI BIZIM, MODULUN DEGIL. Yani asagidaki numaralar ESP32
// tarafi; modulun uzerindeki etiketler (SCL/SDA/RES/DC/BLK/CS) kargo
// gelince BUNLARLA KARSILASTIRILACAK. Bazi modullerde CS yok, bazisinda
// "SDA" MOSI demek — etikete bakmadan lehimlenmeyecek.
//
// NEDEN 10/11/12: bunlar ESP32-S3'un SPI2 (FSPI) IOMUX pinleri
// (FSPICS0 / FSPID / FSPICLK). IOMUX kullanilinca sinyal GPIO
// matrisinden gecmiyor ve daha yuksek saat hizi tasiyabiliyor. Matris
// uzerinden de calisir ama SPI'nin darbogaz oldugu bir iste (tam
// ekran 240x240x16 bit = 23 ms @40 MHz) bedava hizi birakmak sacma.
//
// Kalan uc pin (DC/RST/BLK) hiz kritik degil, sirasiyla 9/8/7.
// Cakisma yok: mikrofon 4/5/6, amfi 15/16/17, LED 48.

// 🔴 GELEN MODULDE CS PINI YOK — 31.07.2026'da kutu acilip GORULDU.
//
// Modul: ZJY-IPS130-DC1SP1-V2.0, ST7789, 240x240. Uzerinde YEDI pin var
// ve sirasi (kare padden baslayarak):
//
//     GND · VCC · SCL · SDA · RES · DC · BLK
//
// CS disari cikarilmamis. Bu modullerde yonganin CS ucu kart uzerinde
// kalici olarak asagi cekiliyor, yani panel her zaman "secili" —
// hatta tek SPI cihazi oldugumuz icin secilecek bir sey de yok.
//
// -1 verilince esp_lcd CS'i HIC surmuyor. Pin numarasi yazip kabloyu
// takmamak calisirdi ama GPIO10 bosuna ayrilmis olurdu; -1 dogru olani.
//
// ⚠️ Bu bir VARSAYIM: CS'in kart uzerinde GND'ye bagli oldugunu
// goremiyoruz, cikarim yapiyoruz. Ilk acilista test deseni cikmazsa
// bakilacak yerlerden biri burasi.
#define PATI_EKR_SCK  GPIO_NUM_12  // modulde: SCL
#define PATI_EKR_MOSI GPIO_NUM_11  // modulde: SDA
#define PATI_EKR_CS   (-1)         // modulde YOK (yukariya bak)
#define PATI_EKR_DC   GPIO_NUM_9   // modulde: DC
#define PATI_EKR_RST  GPIO_NUM_8   // modulde: RES
#define PATI_EKR_BLK  GPIO_NUM_7   // modulde: BLK — arka isik

// 🔴 BLK'ya bir sey verilmezse ARKA ISIK YANMAZ ve ekran bos gorunur.
// "Bozuk geldi" sanilmasinin en yaygin sebebi bu. Gelen modulde BLK
// AYRI BIR PIN olarak var, yani baglanmasi SART.

// SPI saat hizi. ST7789 veri sayfasi 62,5 ns cevrim (16 MHz) yaziyor
// ama gercekte 40-80 MHz ile calisiyorlar; uzun kablo/breadboard
// olunca bozuluyor.
//
// 40 MHz'den BASLIYORUZ cunku olculecek bir sey var: bu hizda tam
// ekran 23 ms suruyor, yani cizim bedava olsa bile en fazla 43 fps.
// Ekranda kirilma/karlanma gorulmezse 80'e cikarilip fark olculecek.
#define PATI_EKR_HZ (40 * 1000 * 1000)

// Ekranin AKTIF alani. 1.3" kosegen kare panel: 33,02 mm / V2 = 23,35 mm.
// Cizim bu iki sayiya gore yapiliyor (panel/gozler240.js ile ayni).
#define PATI_EKR_G 240
#define PATI_EKR_Y 240

// ---------------------------------------------------------------------------
// Ses bicimi — prototype'de OLCULDU, tahmin degil
// ---------------------------------------------------------------------------
//
// Gemini Live: giris 16 kHz, cikis 24 kHz, ikisi de mono int16.
// stackchan'in gemini_live_client.hpp'si de aynisini soyluyor.

#define PATI_MIK_HZ  16000
#define PATI_HOP_HZ  24000
