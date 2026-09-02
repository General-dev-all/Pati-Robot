// Pati ses hatti — ES8311 kodek uzerinden mikrofon ve hoparlor
//
// TEK I2S KANAL CIFTI, TEK SAAT. Mikrofon ve hoparlor ayni yonganin
// (ES8311) icinde ve ayni I2S hattini paylasiyorlar, yani ikisi ayni
// orneklem hizinda kosmak ZORUNDA. Hat 48 kHz'de calisiyor; sebep
// pati_pinler.h icindeki PATI_SES_HZ blogunda.
//
// Gemini'nin istedigi hizlar farkli (giris 16 kHz, cikis 24 kHz), yani
// iki tarafta da YENIDEN ORNEKLEME yapiliyor. Ikisi de burada.
//
// ⚠️ Onceki kartta (v2.2.8-devkit) durum baskaydi: INMP441 ve MAX98357
// ayri yongalardi, ayri kanallarda ayri hizlarda kosuyorlardi ve
// yeniden ornekleme HIC YOKTU — calma hizi dogrudan I2S saatinden
// ayarlaniyordu. O yol burada kapali.
//
// Bu dosya SADECE bayt tasiyor ve ornek cevirisi yapiyor. Ne olcum
// yapiyor ne karar veriyor; karar app_main'de.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <esp_err.h>

namespace pati {

// ---------------------------------------------------------------------------
// Ortak kurulum
// ---------------------------------------------------------------------------
//
// Iki fonksiyon da AYNI seyi yapiyor ve ikisini de cagirmak zararsiz:
// ilk cagiran I2C'yi, I2S'i ve kodegi kuruyor, ikincisi hemen donuyor.
//
// Ayri isimlerle durmalarinin sebebi cagiran taraf: app_main "mikrofonu
// baslat" ve "hoparloru baslat" diye okunuyor ve donanimin ikisini tek
// yongada birlestirmis olmasi orayi ilgilendirmiyor.
//
// 🔴 ONCESINDE guc_baslat() CAGRILMIS OLMALI. I2C hatti ve kodegin
// beslemesi (L3B) oradan geliyor; bu fonksiyonlar hazir bir hat
// bekliyor, kendileri acmiyor.

esp_err_t mikrofon_baslat();
esp_err_t hoparlor_baslat();

// I2S ve kodek gercekten ayaga kalkti mi. Acilis kaydinin ilk saniyesi
// USB'de gorulemedigi icin sonradan da sorulabilmeli (bkz. pati_guc.hpp).
bool ses_hazir();

// ---------------------------------------------------------------------------
// Mikrofon — Gemini'ye 16 kHz mono int16
// ---------------------------------------------------------------------------

// Mikrofondan int16 ornek okur. Doner: okunan ORNEK sayisi (bayt degil).
// Blokluyor; timeout_ms kadar bekler.
//
// Donen ornekler 16 kHz: kodekten 48 kHz okunup UCER ORNEGIN ORTALAMASI
// aliniyor. Oran tam sayi oldugu icin bu hem seyreltme hem suzgec
// gorevi goruyor (ayrinti .cpp'de).
size_t mikrofon_oku(std::span<std::int16_t> hedef, uint32_t timeout_ms = 100);

// Tek cagrida okunabilecek en fazla ornek. 16 kHz'de 20 ms = 320 ornek.
// Gemini'ye 20 ms'lik parcalar halinde gonderiyoruz; prototype'de olculen
// yol da buydu (PARCA_ORNEK = 320).
constexpr size_t PATI_OKUMA_ORNEK = 320;

// ---------------------------------------------------------------------------
// Hoparlor — Gemini'den 24 kHz mono int16
// ---------------------------------------------------------------------------

// Gemini'den gelen 24 kHz int16 orneklerini calar.
//
// Doner: TUKETILEN KAYNAK ornegi. Yazilan cikis ornegi DEGIL — ikisi
// artik farkli, cunku 24 kHz kaynak 48 kHz'e cevriliyor ve ustune hiz
// carpani uygulaniyor (1.30'da bir kaynak ornegi ~1,54 cikis ornegi).
//
// Ses seviyesi ve yumusak sinirlayici burada uygulaniyor.
size_t hoparlor_yaz(std::span<const std::int16_t> kaynak,
                    uint32_t timeout_ms = 200);

// Calma hizini ayarlar. 1.30 -> ses %30 hizli ve ~%30 tiz calinir.
//
// 🔴 PATI'NIN SESI BUNA BAGLI. Live API'de tizlik ve hiz ayarlanamiyor
// (SpeechConfig semasinda sadece voiceConfig / languageCode var), o
// yuzden prototype eski bir hileye basvuruyor: sesi oldugundan hizli
// caliyor. Puck'in ham sesi bir yetiskin erkegi; afacan cocuk sesi
// tamamen bu carpandan geliyor.
//
// Gerekcesi ve 1.30 degerinin nasil secildigi prototype/ayarlar.py
// icindeki CIKIS_HIZ blogunda — panelden DINLENEREK secilmis, olculerek
// degil.
//
// ⚠️ Carpan yukseldikce ses HEM tizlesiyor HEM aceleci oluyor; ikisi
// ayrilamiyor. v1 uretimi ayri yavaslatabildigi icin 1.52 kullaniyordu,
// burada o deger robotu aceleci yapar.
//
// ARTIK SESI KESMIYOR. Onceki kartta bu fonksiyon I2S kanalini kapatip
// saati yeniden kuruyordu, yani panelden hiz degistirmek calan sesi
// kesiyordu. Burada saat mikrofonla paylasildigi icin zaten
// degistirilemez ve carpan ornek uzerinde calisiyor — degisiklik
// aninda ve sessizce uygulaniyor.
esp_err_t hoparlor_hiz_ayarla(float carpan);

// Bekleyen sesi at — cocuk robotun sozunu kesti (barge-in).
//
// Atmazsak robot susmus gorunup birkac saniye sonra eski cumlesine
// devam ediyor; prototype'de bunun "cocuk icin en sinir bozucu davranis"
// oldugu not edilmisti.
esp_err_t hoparlor_temizle();

// ---------------------------------------------------------------------------
// Ses seviyesi
// ---------------------------------------------------------------------------
//
// Cocuk "sesini kis" diyebiliyor. Tamamen sessize alma KASITLI olarak
// yok, cocuk sifira indirip "Pati bozuldu" sanmasin.
//
// 1.0 ustu mumkun cunku yumusak sinirlayici var: olceklenen her ornek
// `yumusak_sinirla`dan geciyor, esigin altinda hicbir sey olmuyor,
// ustunde tavana asimptot yaklasiyor. Kirpma yok.
//
// ---------------------------------------------------------------------------
// 🔴 BASLANGIC DEGERI 1.50'DEN 1.00'E INDIRILDI — SEBEBI HOPARLOR DEGIL,
//    PIL.
// ---------------------------------------------------------------------------
//
// Onceki kartta 1.50 guvenliydi ve dogruydu: govdede 5 W'lik bir
// hoparlor, MAX98357 amfi ve duvardan gelen 5 V vardi. Amfi kendi
// besleme rayinda doyuma gidiyordu, yani sayisal seviye hoparlorun
// sinirini asamiyordu.
//
// StickS3'te ucu de degisti: hoparlor 8 ohm / 1 W (2011 kasa), amfi
// AW8737, ve besleme 250 mAh'lik bir lityum pil.
//
// M5Stack'in kendi urun belgesi (docs.m5stack.com/en/core/StickS3,
// "Speaker Volume Notice"): pille calisirken hoparlor seviyesini %75'in
// ALTINDA tutmak gerekiyor, yoksa cekilen akim cihazi BEKLENMEDIK
// SEKILDE YENIDEN BASLATIYOR.
//
// Bir cocuk icin bunun anlami sudur: Pati cumlenin ortasinda kapanip
// yeniden aciliyor. Bu, "ses biraz kisik" olmasindan cok daha kotu bir
// ariza — ve sebebi hicbir yerde gorunmuyor, guc dalgalanmasi diye
// aranir.
//
// 1.00 secildi: ne yukseltme ne kisma, kaynagin kendi seviyesi.
// Panelden yukseltilebiliyor. Yukseltilirken kural basit — Pati pilde
// calisirken kendiliginden yeniden basliyorsa seviye YUKSEK.
//
// Kullanicinin bu donanim icin soyledigi de buydu: "olduğu kadar,
// artık ses düşük olsa bile çıksın yeter."
constexpr float SES_SEVIYESI_BASLANGIC = 1.00f;

// Tavan 2.50'den 2.00'ye indirildi, ayni gerekceyle. Sinir HASAR degil
// (amfi kendi rayinda doyuma gidiyor), YENIDEN BASLAMA.
constexpr float SES_SEVIYESI_EN_FAZLA  = 2.00f;

// ---------------------------------------------------------------------------
// 🔴 PILDE UST SINIR — 0.70, ve bu sayi iki kez olculdu
// ---------------------------------------------------------------------------
//
// 01.09.2026: Pati pille calisirken, ses 1.00'DE, konusmaya baslayacagi
// anda brownout'a dusup yeniden basliyordu. Tavan 0.70 kondu.
//
// 02.09.2026: PSRAM 40 MHz'e indirilip asil cokme sebebi giderilince
// (TESHIS.md) bu tavanin gereksiz oldugu dusunuldu ve 1.00'e cikarildi.
// Gerekce makuldu: 1.00 zaten "kaynagin kendi seviyesi", yukseltme degil.
//
// 🔴 O KARAR YANLISTI VE OLCUM SOYLEDI. Ayni gece, pilde, MASADA,
// SALLAMADAN, tavan 1.00'de:
//
//     00:12:46  pil=3696 mV   <- taban 3816-3826, yani 120 mV sarkma
//     00:13:03  ifade=dusunuyor
//     00:13:05  COKTU
//     00:13:10  geri geldi -> acilis=brownout
//
// Tam konusmaya baslarken. Yani hoparlorun cektigi akim, PSRAM sorunu
// giderildikten SONRA da tek basina brownout yapmaya yetiyor.
//
// 0.70 geri alindi: elimizdeki tek destekli deger o.
//
// ⚠️ 0.70 ile 1.00 ARASI OLCULMEDI. Cocuk Pati'yi cogunlukla pilde
// kullanacak ve orada daha yuksek ses gercek bir kazanc olurdu; 0.85
// denenmeye deger. Denenirse yontem belli: pilde, masada, sallamadan,
// birkac dakika konus ve DURUM ozetindeki "acilis" satirina bak.
//
// ⚠️ TAVAN ASLINDA PIL GERILIMINE BAGLI OLMALI. Dolu hucrede (4,1 V)
// 1.00 sorun cikarmiyor olabilir; yukaridaki cokme 3,8 V'ta yasandi.
// `pil_mv()` zaten okunuyor, yani kademeli bir tavan mumkun. Yapilmadi
// cunku esikleri belirleyecek olcum yok — tahminle yazilsaydi bu
// yorumdaki hatanin aynisi tekrarlanirdi.
// ---------------------------------------------------------------------------
// 02.09.2026 — 0.70'ten 0.65'e indirildi, KULLANICININ ACIK IZNIYLE
// ---------------------------------------------------------------------------
//
// 🔴 BU DEGER UZUN SURE PAZARLIGA KAPALIYDI ve oyle kalmasi dogruydu:
// pilde kisik ses gercek bir kayip, cocuk duyamiyorsa robot ise
// yaramiyor. Iki gun boyunca bu satira dokunulmadan cozum arandi.
//
// Neden simdi indi: iki gunluk olcumden sonra hoparlorun tepe akimi
// disinda denenmedik buyuk bir aday kalmadi. Ve olculen sey su —
// brownout esigi ZATEN en toleransli ayarda (ESP32-S3 LVL_SEL_7 =
// 2,44 V; kaynak: esp-idf/components/esp_hw_support/power_supply/
// port/esp32s3/Kconfig.power). Yani 3,3 V rayi gercekten 2,44 V'a
// dusuyor. Bu kucuk bir dalgalanma degil; tepe akimi cekense M5Stack'in
// kendi belgesinin de isaret ettigi hoparlor.
//
// Kullanicinin sozu: "hatta cok ihtiyacin varsa ve sorunumuzu
// cozucegine inaniyorsan biraz hoparloru bile kismana izin veriyorum."
//
// Once 0.55 yazildi ve kullanici geri cevirdi: "0.55 az olmadi mi?"
// Hakliydi — 0.70'ten 0.55'e inmek %21'lik bir dususe denk geliyor ve
// bu, olculmemis bir kazanc icin cok buyuk bir odun. 0.65 kucuk bir
// adim; ise yaramazsa zaten geri alinacak, yarasa bile daha ileri
// gitmeden once olculecek.
//
// ⚠️ BU BIR GERI ADIM VE OLCULMESI SART. Cokme belirgin azalmazsa
// 0.70'e GERI DONULMELI — sesi bedavaya kismis oluruz ve bu, ustunde
// iki gun durulmus bir degeri bosuna feda etmek olur.
constexpr float SES_PIL_TAVANI = 0.65f;
constexpr float SES_SEVIYESI_EN_AZ     = 0.15f;
constexpr float SES_SEVIYESI_ADIM      = 0.15f;

// Seviyeyi sinirlar icinde ayarlar, gercekte ne oldugunu dondurur.
float ses_seviyesi_ayarla(float yeni);
float ses_seviyesi_degistir(float adim);
float ses_seviyesi();

}  // namespace pati
