// Pati'nin hafizasi — cihazda, NVS'te.
//
// ===========================================================================
// prototype/hafiza.py'NIN PORTU — ve neden birebir olmasi gerekiyor
// ===========================================================================
//
// Hafiza sistem promptuna giriyor. Prompt degisirse modelin davranisi
// degisiyor ve PC'de olculen sayilar (medyan 1325 ms, uyum %86) ESP32
// icin gecersiz olur. Yani "buraya benzer bir sey yazdim" yetmiyor:
// ayni cumleler, ayni sirada, ayni sinirlarla uretilmeli.
//
// Bu yuzden benzerlik/govdeleme mantigi konak testiyle Python'a karsi
// dogrulaniyor (test/hafiza_karsilastir.cpp).
//
// ===========================================================================
// UTF-8 TUZAGI — bu bir varsayim degil, kacinilmasi gereken bir hata
// ===========================================================================
//
// Python govdelemeyi KARAKTER uzerinde yapiyor: `k[:5]` "cocuk"tan
// "cocuk", "cicek"ten "cicek" aliyor. C++'ta ayni seyi BAYT uzerinde
// yapmak Turkce'de bozuyor:
//
//     "cicek" -> UTF-8'de c,i,ç(2 bayt),e,k = 6 bayt
//     ilk 5 BAYT = "ciç" + yarim bayt  -> bozuk dizi
//
// O yuzden butun metin islemleri KOD NOKTASI sayiyor.
//
// ===========================================================================
// NEDEN NVS, NEDEN TEK BLOB
// ===========================================================================
//
// NVS blob siniri 508.000 bayt (ESP-IDF v5.5 dokumani) — bizim
// ihtiyacimizin cok ustunde.
//
// Tek blob olmasinin sebebi ATOMIKLIK. Kayitlari ayri anahtarlara
// yazsaydik, cocuk fisi yazma ortasinda cekince yarisi yeni yarisi
// eski kalirdi. NVS bir anahtari guncellerken yeni degeri yazip sonra
// eskisini gecersiz kiliyor, yani guc kesilirse ELDE YA ESKI YA YENI
// oluyor — Python tarafindaki "gecici dosyaya yaz, sonra tasi"
// yonteminin aynisi.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <esp_err.h>

namespace pati {

// --- sinirlar: prototype/hafiza.py ile AYNI sayilar ---------------------------
//
// Degistirilirse iki tarafta birden degistirilmeli, yoksa PC'de olculen
// prompt uzunlugu ESP32'de baska cikar.

constexpr int HAFIZA_EN_FAZLA_KAYIT = 200;        // NVS'te saklanan
constexpr int HAFIZA_PROMPT_EN_FAZLA_KAYIT = 25;  // prompta giren
constexpr int HAFIZA_PROMPT_EN_FAZLA_KARAKTER = 1200;   // ~400 token
constexpr int HAFIZA_EBEVEYN_NOTU_EN_FAZLA = 600;
constexpr float HAFIZA_BENZERLIK_ESIGI = 0.70f;
constexpr int HAFIZA_GOVDE_UZUNLUK = 5;

// Serilestirilmis JSON'un sert ust siniri. 200 kayit x ~140 bayt ~ 28 KB;
// 48 KB pay birakiyor. Asilirsa daha agresif budaniyor.
//
// PLAN.md: "Sinirsiz buyuyen hicbir sey birakma." Kayit sayisi siniri
// tek basina yetmiyor cunku tek bir kaydin metni de uzun olabiliyor.
constexpr int HAFIZA_EN_FAZLA_BAYT = 48 * 1024;

// Tek bir bilginin metin sinirlari (Python: 3 <= len <= 200 karakter).
constexpr int HAFIZA_BILGI_EN_AZ = 3;
constexpr int HAFIZA_BILGI_EN_FAZLA = 200;

struct Bilgi {
    int id = 0;
    std::string metin;
    std::string kaynak;       // "sohbet" | "ebeveyn"
    std::string tarih;        // ISO benzeri; cihazda saat yoksa bos
    std::string son_gorulme;
    int kez = 1;
};

struct Cocuk {
    std::string ad;
    int yas = 0;              // 0 = bilinmiyor
};

// Hafizayi NVS'ten okur. Yoksa bos hafiza ile baslar.
//
// Bozuk blob DURUMUNDA: sifirdan baslamiyor, YEDEKLIYOR. Cocugun
// hafizasi degerli; bozuk olsa bile silmek son secenek olmali
// (Python tarafi da bozugu `.bozuk-*.json` diye sakliyor).
esp_err_t hafiza_baslat();

// --- okuma ----------------------------------------------------------------

const Cocuk& hafiza_cocuk();
const std::vector<Bilgi>& hafiza_bilgiler();
const std::string& hafiza_ebeveyn_notu();
int hafiza_oturum_sayisi();

// Robotun SU ANKI adi. Cocuk degistirmediyse PATI_ROBOT_ADI.
//
// Sistem promptunun ilk cumlesi bunu kullaniyor: uretilen baslikta
// {ROBOT_ADI} token'i duruyor ve pati_sohbet.cpp calisma aninda
// yerine koyuyor (bkz. firmware/prompt_uret.py aciklamasi).
const std::string& hafiza_robot_adi();

// --- yazma (hepsi NVS'e islenir) ------------------------------------------

// Yeni kalici bilgi. Ayni sey biliniyorsa yeni kayit ACMAZ, `kez`
// sayacini artirir — robot bir seyi ne kadar cok duyduysa o kadar emin
// olsun diye.
//
// Dondurdugu: eklendi/guncellendi ise true, gecersiz metin ise false.
bool hafiza_bilgi_ekle(const std::string& yazi,
                       const std::string& kaynak = "sohbet");

bool hafiza_bilgi_sil(int id);
void hafiza_cocugu_tanimla(const std::string& ad, int yas);

// Cocuk robota yeni bir ad takti ("bundan sonra senin adin Osman").
// Cocuk istedigi zaman degistirebilir; en son soyledigi gecerli.
// "Tumunu sil" varsayilana donduruyor.
//
// Doner: kabul edildiyse true (rakam iceren / cok kisa ad reddedilir).
bool hafiza_robot_adini_degistir(const std::string& ad);

// Adi varsayilana ("Pati") dondurur. Ebeveyn panelde alani bosaltinca
// cagriliyor; bosaltmak hata degil, "bastaki ada don" demek.
void hafiza_robot_adini_sifirla();
void hafiza_ebeveyn_notu_kaydet(const std::string& yazi);
void hafiza_her_seyi_unut();
void hafiza_oturum_sayaci();

// --- sistem promptuna giren blok -------------------------------------------
//
// Bicim prototype/hafiza.py `prompt_blogu()` ile AYNI. Fark olursa modelin
// davranisi degisiyor ve PC olcumleri gecersiz oluyor.
std::string hafiza_prompt_blogu();

// --- panel icin -----------------------------------------------------------

// Panelin bekledigi JSON (prototype `ozet()` ile ayni alanlar).
std::string hafiza_ozet_json();

// --- ic mantik, testten cagriliyor ----------------------------------------
//
// Disa aciliyor cunku konak testi bunlari Python'un urettikleriyle
// karsilastiriyor. Metin islemenin sessizce ayrilmasi, hafizanin
// sessizce yanlis calismasi demek.

// Turkce'ye dogru kucuk harf. `str.lower()` iki yerde yaniliyor:
//   "I" -> "i" olmali degil, "ı"
//   "İ" -> iki karakterlik bozuk dizi
std::string hafiza_kucult(const std::string& m);

// Kucult + Turkce harfleri ASCII'ye indir (Python: metin.sadele).
std::string hafiza_sadele(const std::string& m);

// Python `str.strip()` karsiligi.
std::string hafiza_kirp(const std::string& m);

// --- ad / yas suzgecleri --------------------------------------------------
//
// BURADA DURUYORLAR cunku Python'da da hafiza.py'nin icindeler ve
// konak testi (test/hafiza_karsilastir.cpp) iki tarafi karsilastirabilsin.
// Onceden pati_cikarim.cpp'nin isimsiz uzayindaydilar: test goremiyordu
// ve iki taraf sessizce ayrilmisti — C++ "Boş" gibi bir yer tutucuyu
// eliyemiyordu cunku ASCII'ye indirmeden karsilastiriyordu.
//
// Gerekceleri .cpp'de, olculmus hatalarla birlikte yazili.
bool hafiza_ad_bicimi_uygun_mu(const std::string& ad);
bool hafiza_cocuk_adi_gecerli_mi(const std::string& ad);
int hafiza_yas_gecerli_mi(int ham);

// Kelime govdeleri (ilk HAFIZA_GOVDE_UZUNLUK KOD NOKTASI).
std::vector<std::string> hafiza_kokler(const std::string& m);

float hafiza_benzerlik(const std::string& a, const std::string& b);
bool hafiza_ayni_bilgi_mi(const std::string& a, const std::string& b);

}  // namespace pati
