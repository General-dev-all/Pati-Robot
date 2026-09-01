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

// ---------------------------------------------------------------------------
// Sistemi su an ne besliyor?
// ---------------------------------------------------------------------------
//
// 🔴 NEDEN GEREKLI: 01.09.2026'da gercek kartta olculdu — Pati pille
// calisirken, ses seviyesi 1.00'de, konusmaya basladigi anda brownout'a
// dusup yeniden basladi. Cocuk acisindan gorunusu: "tak" diye bir ses,
// ekran kapaniyor, Pati cumlesinin ortasinda kayboluyor.
//
// M5Stack'in kendi belgesi de soyluyor (docs.m5stack.com StickS3,
// "Speaker Volume Notice"): pille calisirken yuksek ses cihazi
// beklenmedik sekilde yeniden baslatiyor.
//
// USB'de ayni seviye sorun cikarmiyor, cunku akim oradan geliyor. Yani
// dogru cozum sesi hep kismak DEGIL, kaynaga gore karar vermek.
//
// ⚠️ "USB KABLOSU TAKILI MI" DIYE SORMUYORUZ. M5PM1 pil doluyken sistemi
// pilden besleyip USB'yi yalnizca sarja ayirabiliyor; o halde kablo
// takili olsa bile akimi pil veriyor. Sorulan sey sistemi FIILEN neyin
// besledigi.
enum class GucKaynagi {
    Usb,          // 5VIN — priz/bilgisayar, akim bol
    Pil,          // batarya — 250 mAh, ses sinirlanmali
    Bilinmiyor,   // M5PM1 cevap vermedi ya da beklenmeyen deger
};

// M5PM1'e sorar. I2C islemi yapiyor (~1 ms); her ses karesinde degil,
// saniyede bir mertebesinde cagrilmali.
//
// Bilinmiyor donerse cagiran taraf GUVENLI OLANI secmeli (yani pil
// varsayimi): yanlis tahminin bedeli bir yanda "ses biraz kisik",
// obur yanda "Pati cumle ortasinda kapaniyor".
GucKaynagi guc_kaynak();

// Pil gerilimi, milivolt. Okunamazsa 0.
//
// Tek basina yuzdeye cevrilmiyor: lityum egrisi dogrusal degil ve
// yuk altinda dusuyor. Ham deger raporda daha durust.
int pil_mv();

// Haricî 5 V girisinin gerilimi, mV. Okunamazsa -1.
//
// guc_kaynak() kararini buradan veriyor. Ayri acilmasinin sebebi
// teshis: "USB yok" ile "USB var ama gerilim dusuk" ayri sorunlar.
int vin_mv();

// Yonganin kendi sicakligi, santigrat. Okunamazsa -1000.
//
// 🔴 NEDEN VAR: 01.09.2026'da pilde tekrarlayan brownout'larin sebebi
// aranirken "cihaz isiniyor olabilir mi" sorusu cikti ve cevaplanamadi.
// Isinma tahmin edilecek bir sey degil — yonga kendi sicakligini
// soyleyebiliyor.
//
// Neden onemli: M5PM1'in LDO'su asiri isinirsa kendini korumaya alir ve
// 3,3 V rayi coker. Disaridan gorunusu brownout'un aynisi olur. Sicaklik
// olculmeden ikisi ayirt edilemiyor.
//
// Beklenen: oda sicakliginda 40-60 C normal (240 MHz + telsiz surekli
// acik). 80 C uzeri sureklilik arz ediyorsa isi gercek bir etken.
float yonga_sicakligi();

// guc_baslat() basarili oldu mu — L3B ve amfi gercekten acildi mi.
//
// Acilis kaydinin ILK SANIYESI bu kartta gorulemiyor: konsol USB
// uzerinden geliyor ve her sifirlamada USB yeniden numaralaniyor, yani
// bilgisayar portu birkac saniye kaybediyor. Durum sonradan da
// sorulabilmeli.
bool guc_hazir();

}  // namespace pati
