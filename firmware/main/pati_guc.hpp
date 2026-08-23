// Pati guc ve I2C hatti — M5PM1 guc yonetimi + donanim dogrulamasi
//
// Bu dosya UC is yapiyor ve ucu de ses/ekran kodundan ONCE olmali:
//
//   1. I2C hattini kuruyor. Uc aygit paylasiyor (ES8311, BMI270, M5PM1),
//      yani hat tek bir yerde acilmali.
//   2. L3B guc katmanini aciyor. Mikrofon, hoparlor ve LCD arka isigi
//      bundan besleniyor ve acilista KENDILIGINDEN GELMIYOR.
//   3. Kartin gercekten StickS3 olup olmadigini yokluyor.
//
// Ucuncusu bir kolaylik degil, EMNIYET KILIDI — asagida.

#pragma once

#include <driver/i2c_master.h>
#include <esp_err.h>

namespace pati {

// I2C hattini kurar, L3B'yi ve hoparlor amfisini acar.
//
// EN BASTA cagrilmali: ekran ve ses bundan sonra gelmeli, cunku
// ikisinin de gucu buradan geliyor.
esp_err_t guc_baslat();

// Kurulmus I2C hatti. ES8311 surucusu bunu aliyor — kodek kendi
// hattini acmiyor, ayni hatti paylasiyor.
//
// guc_baslat() cagrilmadan once nullptr doner.
i2c_master_bus_handle_t i2c_yolu();

// ---------------------------------------------------------------------------
// 🔴 DONANIM KILIDI — yanlis karta inen yazilim kendini geri almali
// ---------------------------------------------------------------------------
//
// SORUN. Guncelleme manifestinde (surum.json) DONANIM ALANI YOK; icinde
// yalnizca surum numarasi ve indirme adresi var. Eski Pati (ESP32-S3
// DevKit + INMP441 + MAX98357) ile bu kart AYNI yongayi kullaniyor
// (esp32s3), yani eski Pati bu yazilimi indirir, dogrular ve calistirir.
//
// Ve calistirdiginda GERI DONEMEZ. Onyukleyicinin geri alma mekanizmasi
// yeni yapinin kendini "saglam" isaretlememesine bakiyor; oysa
// guncelleme_onayla() ag ile panel kalkinca cagriliyor ve ikisi de
// ses kodeginden bagimsiz. Yani yanlis karttaki yazilim sessizce
// kendini saglam ilan eder, geri alma HIC devreye girmez ve eski Pati
// kalici olarak susar. Caresi kutuyu acip USB takmak olur.
//
// COZUM. Kart kimligini yazilimin KENDISI soruyor: ES8311 I2C'de cevap
// veriyor mu? Eski kartta o yongadan hic yok, dolayisiyla cevap da yok.
//
// Bu, "dikkat edilerek" degil YAPISI GEREGI calisan bir koruma: kimsenin
// bir butona basmamayi hatirlamasi gerekmiyor.
bool donanim_dogru();

// Hoparlor amfisini (AW8737) acar/kapatir.
//
// Kapatmanin iki gerekcesi olabilir: guc tasarrufu ve kizilotesi alici
// (M5Stack'in belgesi amfi acikken IR alimin bozuldugunu yaziyor).
// Pati kizilotesi kullanmiyor, yani pratikte hep acik.
esp_err_t hoparlor_amfi(bool ac);

}  // namespace pati
