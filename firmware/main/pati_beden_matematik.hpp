// Bedenin saf matematigi — ESP-IDF'e HIC dokunmuyor
//
// ===========================================================================
// NEDEN AYRI BIR BASLIK
// ===========================================================================
//
// pati_beden.cpp'nin geri kalani LEDC, GPIO ve FreeRTOS'a bagli, yani
// konak makinede derlenemiyor. Oysa buradaki dort fonksiyon yanlis
// oldugunda bedeli DONANIMA odeniyor:
//
//   - Servo darbesi araligin disina cikarsa servo mekanik dayanmaya
//     biniyor: donmeye calisip duruyor, isiniyor, akim cekmeye devam
//     ediyor. Gozle gorulmuyor, kulakla "vizilti" diye duyuluyor ve
//     sebebi yazilimda aranmiyor.
//
//   - Karistirmanin isareti ters olursa "sola bas, saga gitsin" oluyor.
//     Bu ancak robot masadayken fark ediliyor ve ilk fark edildigi an
//     zaten kotu bir an.
//
// Ikisi de tek bir tam sayi hatasi. Ikisi de konak testinde bedava
// yakalaniyor (firmware/test/beden_karsilastir.cpp).
//
// Ayni desen pati_ornekleyici.hpp'de zaten var: donanimdan ayrilabilen
// matematik ayriliyor ve siniyor.

#pragma once

#include <algorithm>

namespace pati {

// ---------------------------------------------------------------------------
// Kollar
// ---------------------------------------------------------------------------

// Servonun kullanilabilir darbe araligi (mikrosaniye).
inline constexpr int SERVO_US_EN_AZ  = 500;
inline constexpr int SERVO_US_EN_COK = 2500;

// Kolun fiziksel aralik ucu. Servonun 0-180'inin TAMAMI kullanilmiyor:
// kenarlarda mekanik dayanmaya binme riski var ve dayanan bir servo
// akim cekmeye devam ediyor.
inline constexpr int KOL_DINLENME_DERECE = 20;    // kol asagida
inline constexpr int KOL_TAVAN_DERECE    = 150;   // kol yukarida

// 🔴 IKI SERVO KARSILIKLI MONTE EDILMIS: ayni fiziksel hareket icin sag
// kolun acisi aynalanmali. Geometri YALNIZCA burada; jest tablosu ve
// panel yuzde konusuyor.
//
// Kol ters yone gidiyorsa bakilacak ilk yer bu satir.
inline constexpr bool KOL_SAG_AYNA = true;

// Kaldirma yuzdesini (0 = asagi, 100 = yukari) servo acisina cevirir.
// Donen deger ONDA BIR DERECE — 20 ms'lik adimlarda sicrama gorunmesin
// diye tam sayi cozunurlugu artiriliyor.
inline int kol_derece10(int yuzde, bool sag)
{
    const int y = std::clamp(yuzde, 0, 100);
    int derece = KOL_DINLENME_DERECE
                 + (KOL_TAVAN_DERECE - KOL_DINLENME_DERECE) * y / 100;
    if (sag && KOL_SAG_AYNA) derece = 180 - derece;
    return derece * 10;
}

// Aciyi darbe genisligine cevirir (mikrosaniye).
//
// Kirpma SAVUNMA amacli: yukaridaki aralik zaten guvenli, ama bu
// fonksiyon disaridan gelen bir sayiyla da cagrilabilir ve servoyu
// dayanmaya bindirecek tek bir deger bile uretilmemeli.
inline int kol_darbe_us(int derece10)
{
    const int d = std::clamp(derece10, 0, 1800);
    return SERVO_US_EN_AZ + (SERVO_US_EN_COK - SERVO_US_EN_AZ) * d / 1800;
}

// ---------------------------------------------------------------------------
// Motorlar
// ---------------------------------------------------------------------------

inline constexpr int MOTOR_BIT  = 10;
inline constexpr int MOTOR_ADIM = 1 << MOTOR_BIT;   // 1024

// L9110'un olu bolgesi: cok kucuk duty'de motor donmeyip yalnizca
// vizildiyor. Sifirin ustundeki her istek en az buraya yukseltiliyor.
//
// ⚠️ BU SAYI OLCULMEDI. Ilk devreye almada en dusuk donen duty
// olculecek ve buraya yazilacak (BEDEN.md). Suanki deger bir baslangic
// tahmini ve oyle isaretli duruyor.
inline constexpr int MOTOR_EN_AZ_DUTY = MOTOR_ADIM * 35 / 100;

// Hizi (-100..100) LEDC duty'sine cevirir. Isaret BURADA KAYBOLUYOR —
// yonu hangi pine yazildigi belirliyor (pati_beden.cpp).
inline int motor_duty(int hiz)
{
    const int h = std::clamp(hiz, -100, 100);
    if (h == 0) return 0;
    const int d = (h < 0 ? -h : h) * (MOTOR_ADIM - 1) / 100;
    return std::clamp(std::max(d, MOTOR_EN_AZ_DUTY), 0, MOTOR_ADIM - 1);
}

// ---------------------------------------------------------------------------
// Surus karistirma
// ---------------------------------------------------------------------------

struct Surus {
    int sol;
    int sag;
};

// Joystick'in x/y'sini (-100..100) iki tekerlek hizina cevirir.
//
//   sol = y + x        sag = y - x
//
// Ileri iterken ikisi de ayni yone gidiyor. Saga iterken sol tekerlek
// hizlanip sag yavasliyor; tam saga itilince sol ileri / sag geri, yani
// Pati YERINDE doniyor. Doniste yer degistirmemesi tesadufi degil,
// masa kenarinda istenen davranis bu.
//
// `tavan` ebeveynin hiz siniri (10-100). BURADA uygulaniyor, panelde
// degil: sinir cihazda dursun, panel gonderse bile asilamasin.
inline Surus surus_karistir(int x, int y, int tavan)
{
    const int gx = std::clamp(x, -100, 100);
    const int gy = std::clamp(y, -100, 100);
    const int t  = std::clamp(tavan, 10, 100);

    // Kirpma carpimdan ONCE: once -100..100'e kirpip sonra tavanla
    // carpiyoruz. Ters sirada capraz itiste (x=100, y=100) sol tekerlek
    // once 200 olur, tavanla carpilir ve yine kirpilirdi — yani tavan
    // capraz iticte hicbir sey yapmazdi.
    const int sol = std::clamp(gy + gx, -100, 100) * t / 100;
    const int sag = std::clamp(gy - gx, -100, 100) * t / 100;
    return {sol, sag};
}

}  // namespace pati
