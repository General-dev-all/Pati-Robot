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

// 🔴 KALKMAK ILE GITMEYE DEVAM ETMEK AYRI IKI SEY.
//
// Bu ayrim bu dosyadaki en onemli fikir ve 06.09.2026'da gercek kartta
// olculerek ogrenildi. Motor DURURKEN yuksek guc istiyor (durgun rotor
// + reduktor surtunmesi), ama bir kez donduginde cok daha azi yetiyor.
//
// Ikisi tek bir sayiya baglandiginda robot ya OTUYOR ya FIRLIYOR;
// arada kullanilabilir yer kalmiyor. Kullanicinin sikayeti buydu:
// "cok hizli donuyorlar, bu kadar hiz olmaz cocuk icin."
//
// Cozum ikisini ayirmak:
//
//   MOTOR_KALKIS_DUTY   kisa bir darbe, yalnizca dururken harekete
//                       gecerken. Surtunmeyi kiriyor.
//   MOTOR_EN_AZ_DUTY    ondan sonraki GITME tabani. Cok daha dusuk
//                       olabiliyor, cunku artik kaldirmasi gerekmiyor.
//
// Bu, kullanicinin "anlik tam guc yapma" kuralina bilincli bir
// istisna ve ONAYI ALINDI (06.09.2026). Farki buyuklukte: kurali
// doguran olay bitmis pille 5 saniye kesintisiz tam guctu; bu ise dolu
// pille 180 ms ve yalnizca kalkista. Ustelik darbenin kendisi de
// rampali (MOTOR_KALKIS_ADIM) — anlik siçrama yok.

// Kalkis darbesinin tepesi ve suresi.
inline constexpr int MOTOR_KALKIS_DUTY = 85;
inline constexpr int MOTOR_KALKIS_MS   = 180;

// Gitme tabani. Sifirin ustundeki her istek en az buraya yukseltiliyor;
// altinda motor donmeyip yalnizca vizildiyor.
//
// ⚠️ %18 OLCULMEDI, ama artik bir KALKIS esigi degil bir GITME tabani
// ve o cok daha bagislayici. Kalkisi darbe hallediyor.
//
// NASIL OLCULUR — ek koda gerek yok: tekerlekler TAKILI, Pati YERDE,
// panelden hiz sinirini dusur. Motorun donmeye devam edemedigi (kalkip
// hemen durdugu) deger tabandir.
inline constexpr int MOTOR_EN_AZ_DUTY = MOTOR_ADIM * 18 / 100;

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
// 🔴 YUMUSAK KALKIS — motorlar anlik tam guce ASLA gecmiyor
// ---------------------------------------------------------------------------
//
// KULLANICININ ACIK ISTEGI (06.09.2026): "bir daha motorlari anlik %100'de
// yapma, ne olursa olsun dikkatli gidelim."
//
// Istek hakli ve teknik karsiligi net: bir DC motorun en yuksek akimi
// KALKIS anindadir (rotor duruyorken sargi direnci disinda bir sey
// akimi sinirlamiyor). O tepe, AA hattinda gerilim cokusu yapiyor ve
// ayni hatta duran servolar o cokusu goruyor.
//
// 06.09.2026 aksami iki motor bir anda %100'e cikarildi ve hemen
// ardindan servolar oynamaz oldu. Sebep kanitlanmadi ama bu adim
// tepeyi dusuruyor ve bedeli yok.
//
// ⚠️ DURMAK BEKLEMEZ. Rampa yalnizca YUKARI cikarken var; sifira inis
// ANINDA. Olu adam zamanlayicisi ya da cocugun parmagini kaldirmasi
// kademeli olamaz — o gecikme masa kenarinda santimetre demek.
//
// Yon degistirme de rampadan geciyor: +60'tan -60'a giderken deger
// once 0'dan geciyor, yani sert ters cevirme (en kotu akim tepesi)
// kendiliginden ortadan kalkiyor.

// Tik basina en fazla degisim — UC AYRI DEGER, cunku uc durumun riski
// ayni degil (dongu 20 ms):
//
//   HIZLANMA   5   -> 0'dan %100'e ~400 ms. Akim tepesini dusuren yer
//                    burasi, en yavas olan bu olmali.
//   YAVASLAMA  15  -> akim ZATEN dusuyor, yavas inmenin bir faydasi
//                    yok. Hizli inmek, kalkis darbesinden sonra istenen
//                    yavas hiza cabuk oturmayi sagliyor.
//   KALKIS     20  -> darbe ~85 ms'de tepeye ciksin. Yine de RAMPALI:
//                    anlik siçrama yok.
inline constexpr int MOTOR_RAMPA_ADIM  = 5;
inline constexpr int MOTOR_INIS_ADIM   = 15;
inline constexpr int MOTOR_KALKIS_ADIM = 20;

inline int motor_rampa(int hedef, int su_an, bool kalkis = false)
{
    if (hedef == 0) return 0;                       // durmak beklemez
    // Sifira DOGRU mu gidiyoruz (ayni yonde ve kuculuyor)?
    const bool yavasliyor = (hedef >= 0) == (su_an >= 0)
                            && (hedef < 0 ? hedef > su_an : hedef < su_an);
    const int adim = kalkis     ? MOTOR_KALKIS_ADIM
                     : yavasliyor ? MOTOR_INIS_ADIM
                                  : MOTOR_RAMPA_ADIM;
    const int fark = hedef - su_an;
    if (fark > adim) return su_an + adim;
    if (fark < -adim) return su_an - adim;
    return hedef;
}

// ---------------------------------------------------------------------------
// Surus karistirma
// ---------------------------------------------------------------------------

struct Surus {
    int sol;
    int sag;
};

// ---------------------------------------------------------------------------
// Hiz tavaninin sinirlari
// ---------------------------------------------------------------------------
//
// 🔴 PANEL 0-100 GOSTERIYOR, CIHAZ 40-100 SAKLIYOR (06.09.2026,
// kullanicinin karari).
//
// Sebep: %40'in altinda tekerlek donmuyor, yalnizca otuyor. O araligi
// kaydiricida gostermek, cocuga hicbir sey yapmayan bir yer birakmak
// olurdu. Ama cubuk yine de alisildik 0-100 gorunumunde kaliyor —
// donusum panelde yapiliyor (pati.js · gosterilenden_gercege).
//
// ⚠️ BEDELI: panelde yazan sayi ile motora giden sayi AYNI DEGIL
// (gosterilen %50 -> gercekte %70). Bir ariza ararken karistirmamak
// icin panel gercek degeri de kucuk puntoyla yaziyor ve /api/durum
// her zaman GERCEK degeri donduruyor.
inline constexpr int HIZ_TAVAN_EN_AZ  = 40;
inline constexpr int HIZ_TAVAN_EN_COK = 100;

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
// 🔴 EGRISEL TEPKI — parmagin az ittiginde GERCEKTEN az hiz.
//
// Dogrusalken joystick'in ilk milimetresi bile tabanin ustune
// atliyordu: cocuk icin "duruyor" ile "firliyor" arasinda ara yoktu.
//
// Karesel egri, isaret korunarak: |100| -> 100, |50| -> 25, |25| -> 6.
// Yani kumandanin alt yarisi hassas, ust yarisi hizli. Ucak/araba
// kumandalarinda kullanilan aliskin egri.
inline int hiz_egrisi(int h)
{
    const int g = std::clamp(h, -100, 100);
    return (g * (g < 0 ? -g : g)) / 100;
}

inline Surus surus_karistir(int x, int y, int tavan)
{
    const int gx = std::clamp(x, -100, 100);
    const int gy = std::clamp(y, -100, 100);
    const int t  = std::clamp(tavan, HIZ_TAVAN_EN_AZ, HIZ_TAVAN_EN_COK);

    // Kirpma carpimdan ONCE: once -100..100'e kirpip sonra tavanla
    // carpiyoruz. Ters sirada capraz itiste (x=100, y=100) sol tekerlek
    // once 200 olur, tavanla carpilir ve yine kirpilirdi — yani tavan
    // capraz iticte hicbir sey yapmazdi.
    //
    // Egri kirpmadan SONRA, tavandan ONCE: egri kumandanin hissini
    // belirliyor, tavan ise ust siniri. Sirasi degisirse tavan egriyi
    // ezip hissi bozar.
    const int sol = hiz_egrisi(std::clamp(gy + gx, -100, 100)) * t / 100;
    const int sag = hiz_egrisi(std::clamp(gy - gx, -100, 100)) * t / 100;
    return {sol, sag};
}

}  // namespace pati
