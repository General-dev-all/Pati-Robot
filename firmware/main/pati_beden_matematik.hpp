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
//   - Ozerk bir jest YER DEGISTIRIRSE Pati masadan duser. Ucurum
//     sensoru yok; kenari gorebilecegi hicbir yol yok.
//
// Ucu de tek bir tam sayi hatasi. Ucu de konak testinde bedava
// yakalaniyor (firmware/test/beden_karsilastir.cpp).
//
// JEST TABLOSU DA BURADA, ayni gerekceyle: icindeki bir hata donanima
// odeniyor ve tablo saf veri, IDF'e hic dokunmuyor.
//
// Ayni desen pati_ornekleyici.hpp'de zaten var: donanimdan ayrilabilen
// matematik ayriliyor ve siniyor.

#pragma once

#include <algorithm>
#include <cstdint>

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

// ---------------------------------------------------------------------------
// 🔴 KOLUN GIDEBILECEGI ARALIK — MEKANIK, YAZILIMSAL DEGIL
// ---------------------------------------------------------------------------
//
// Kollarin ucuna gercek kol takilinca ikisi ayni yerlere gidemiyor
// (09.09.2026, kullanicinin olcumu): SOL kol asagida tekerlege,
// yukarida ustteki kablo demetine carpiyor. SAG kolun onunde bir sey
// yok.
//
//     sol  %10 .. %70
//     sag  %0  .. %100
//
// Bu bir tercih degil, MEKANIK BIR SINIR. Asilirsa kol bir yere
// dayaniyor, servo donmeye calisip duruyor, isiniyor ve akim cekmeye
// devam ediyor — belirtisi surekli bir vizilti ve sonu yanmis servo.
//
// ⚠️ SINIR YUZDEYE UYGULANIYOR, ACIYA DEGIL. Sebep: jest tablosu da
// panel de yuzde konusuyor (bkz. KOL_SAG_AYNA gerekcesi). Aciya
// uygulansaydi aynalanmis sag kolda ters ucu kirpardi.
//
// Degerler PANELDEN degistirilebiliyor ve FABRIKA AYARLARINA DONMEK
// BUNLARI SILMIYOR (pati_anahtar.hpp · kalici_sayi_oku). Kullanicinin
// gerekcesi: "tekrar ayarlanmasi unutulursa bir yerlere carpip servo
// bozulabilir."
inline constexpr int KOL_SOL_VARSAYILAN_AZ  = 10;
inline constexpr int KOL_SOL_VARSAYILAN_COK = 70;
inline constexpr int KOL_SAG_VARSAYILAN_AZ  = 0;
inline constexpr int KOL_SAG_VARSAYILAN_COK = 100;

// Bir kol hedefini o kolun araligina kirpar.
//
// Sinirin kendisi de kirpiliyor: panel bozuk bir deger gonderebilir ve
// `en_az > en_cok` gelirse aralik ters doner. O durumda TABANA
// yaslaniyoruz — kolun hic oynamamasi, yanlis yere gitmesinden iyi.
inline int kol_sinirla(int yuzde, int en_az, int en_cok)
{
    const int a = std::clamp(en_az, 0, 100);
    const int b = std::clamp(en_cok, 0, 100);
    if (a > b) return a;
    return std::clamp(yuzde, a, b);
}

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

// ---------------------------------------------------------------------------
// 🔴 DONUS YONU — GERCEK KARTTA OLCULDU, TERSTI
// ---------------------------------------------------------------------------
//
// 06.09.2026: cocuk cubugu SAGA itince Pati SOLA donuyordu. Ileri ve
// geri dogruydu — kullanici Pati'yi hem yuzu hem sirti donukken denedi
// ve ikisinde de Pati kendi ileri yonune gitti. Yani iki motorun ileri
// yonu dogru; ters olan yalnizca donus.
//
// ⚠ SEBEBI AYIRT EDILMEDI ve iki ihtimal de bu belirtiyi birebir
// veriyor:
//
//   (a) KABLO. Sol ve sag motor L9110'un A/B kanallarina ters
//       baglanmis. Ileri/geri etkilenmez cunku ikisi de ayni yone
//       gider; yalnizca donus ters olur.
//
//   (b) AYNA. Pati'nin yuzu cocuga donukse robotun kendi "sagi"
//       cocugun "solu"dur. Kablo dogru olsa bile donus ters GORUNUR.
//       Her uzaktan kumandali oyuncakta olan sey.
//
// NASIL AYIRT EDILIR: Pati'nin SIRTI cocuga donukken sur. (a) ise o
// yonde de ters gorunur; (b) ise dogru gorunur.
//
// Duzeltme iki halde de AYNI, o yuzden burada duruyor: karistirma
// kumandanin hissini belirleyen yer ve konak testinin koruyabildigi
// tek yer. Pin haritasina koymak, ayirt edilmemis bir iddiayi donanim
// belgesine yazmak olurdu (bkz. pati_pinler.h — orada yalnizca
// olculmus seyler var).
//
// ⚠ (a) oldugu bir gun kanitlanirsa duzeltme pin haritasina TASINMALI,
// cunku o zaman /api/durum'daki `beden.sol` fiziksel SAG tekerlegi
// anlatiyor demektir ve bir ariza ararken saatler yer.
inline constexpr int SURUS_X_YONU = -1;

// Joystick'in x/y'sini (-100..100) iki tekerlek hizina cevirir.
//
//   sol = y - x        sag = y + x        (SURUS_X_YONU = -1)
//
// Ileri iterken ikisi de ayni yone gidiyor. Saga iterken bir tekerlek
// ileri obri geri gidiyor, yani Pati YERINDE doniyor. Doniste yer
// degistirmemesi tesadufi degil, masa kenarinda istenen davranis bu.
//
// ⚠ X'in isareti SURUS_X_YONU'nden geliyor ve gercek kartta olculdu —
// gerekcesi yukarida. Once "+x" idi ve cubugu saga itince Pati sola
// donuyordu.
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
    const int gx = std::clamp(x, -100, 100) * SURUS_X_YONU;
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

// ---------------------------------------------------------------------------
// 🔴 OZERK HAREKET — YALNIZCA YERINDE DONUS
// ---------------------------------------------------------------------------
//
// Pati konusurken kendiliginden kipirdiyor ve cocugun sesli komutuyla da
// hareket edebiliyor (06.09.2026, kullanicinin karari). Ikisi de
// panelden kapatilabiliyor; VARSAYILAN ACIK.
//
// Karar, "tekerlekler asla ozerk degil" kuralini deliyor gibi gorunuyor
// ama DELMIYOR — kuralin gerekcesi korunuyor. Gerekce sensor eksikligi
// degildi, YER DEGISTIRMEYDI: ucurum sensoru olmayan bir robot
// ilerledigi surece eninde sonunda duser.
//
// Bu yuzden ozerk hareketin tamami TEK BIR SAYIYLA anlatiliyor:
//
//     sol = +teker        sag = -teker
//
// Iki tekerlek her zaman TERS yonde, yani Pati yerinde doner ve yer
// DEGISTIRMEZ. Bu bir yorumdaki uyari degil, TIPIN KENDISI: jest
// tablosunda ileri giden bir kare YAZILAMIYOR. Sonradan gelen biri
// kurali bilmese de bozamiyor.
//
// ⚠ SESLI KOMUTUN ASIL RISKI YANLIS ANLAMA DEGIL, DOGRU ANLAMA.
// Cocuk "ileri git" derse ve Pati DOGRU anlarsa da masadan duser.
// Ustelik joystick'in aksine sesli komutun ustunde parmak yok: olu adam
// zamanlayicisi onu koruyamaz, cunku onay surekli degil tek seferlik.
// Cozum "model daha iyi anlasin" degil, sozlukte ilerlemenin HIC
// OLMAMASI. Yanlis anlamanin en kotu sonucu: Pati garip bir anda
// sevimli bir donus yapar.

// Ozerk donusun ust siniri. Cocugun joystick'i bunu asabiliyor (orada
// parmak var ve cocuk bakiyor); Pati'nin kendi karari asamiyor.
//
// 13.09.2026'da 60 -> 75. Kullanicinin istegi: "vurus gucu daha fazla
// olabilir, daha enerjik hareketli olsun". Yukselen sey darbe DEGIL,
// darbeden SONRAKI surekli donus hizi: tavan %70'te %42 -> %52.
// Tepe akimi degismiyor (kalkis darbesi zaten %85 ve o sabit), yani
// "anlik tam guc yok" kurali etkilenmiyor.
//
// Kuralin ruhu korunuyor: cocugun joystick'i hala 100'e cikabiliyor,
// Pati'nin kendi karari 75'te duruyor.
inline constexpr int JEST_DONUS_EN_COK = 75;

// 🔴 HER TEKERLEK KARESI EN AZ 300 ms — DARBEDEN UZUN OLMAK ZORUNDA.
//
// Her tekerlek karesi DURURKEN basliyor, yani MOTOR_KALKIS_DUTY (%85)
// devreye giriyor ve MOTOR_KALKIS_MS (180 ms) boyunca suruyor (tepeye
// 100 ms'de cikiyor). Darbe bitince rampa %85'ten tablodaki degere
// iniyor; inis MOTOR_INIS_ADIM (15/tik, 20 ms) ile:
//
//   teker 60, tavan %100 -> hedef %60   2 tik    40 ms
//   teker 60, tavan  %70 -> hedef %42   3 tik    60 ms
//   teker 60, tavan  %40 -> hedef %24   5 tik   100 ms
//
// Yani bir karenin ILK 220-280 ms'si darbe ve inistir — ust ucu
// ebeveyn hiz kaydiricisini en dibe cektiginde goruluyor. Tablodaki
// sayi ancak ondan SONRA motora gidiyor. Kural 300 ms, cunku en kotu
// hal 280 ve arada pay kalmali.
//
// 12.09.2026'ya kadar kareler 120-220 ms'ydi, yani HICBIR kare o esigi
// gormuyordu: tabloda 50 yazsa da motor kare boyunca ~%85'te doniyordu
// ve tablodaki sayinin hicbir etkisi yoktu.
//
// ⚠️ BEDELI GORULEBILIR BIR BELIRTIYDI. Kullanicinin ikinci
// (powerbank'li) gövdesinde "dans et" ve OZELLIKLE "kikirda" Pati'yi
// ileri kaydiriyordu — oysa tablonun net donusu sifir ve konak testi
// bunu her girdide tariyor. Yani kayan sey komut degil FIZIKSEL
// TEPKIYDI: 120 ms'lik bir kare, robot daha donmeye baslamadan bitiyor
// ve geriye yalnizca kalkis darbesinin sarsintisi kaliyor. Kare basina
// bir sarsinti, jest basina DORT sarsinti.
//
// Yeni kural iki isi birden goruyor:
//   - Kare >= 260 ms oldugunda jestin sonunda GERCEK bir donus var,
//     yani hareket daha belirgin (kullanicinin istegi).
//   - Ayni toplam donus suresi DORT yerine IKI kalkista harcaniyor,
//     yani sarsinti sayisi yariya iniyor.
//
// 🔴 SAYIYI DEGIL KAREYI AYARLA. Bir jest az donuyorsa cozum
// `teker` degerini buyutmek degil, `bekle_ms`'i uzatmaktir — ilk
// 220-280 ms zaten darbeye ait ve orada tablodaki sayinin hukmu yok.
//
// Dusme riski degismiyor: darbe de iki tekerlege ters isaretle gidiyor,
// yer degistirme yapisal olarak yine sifir. AA hattindaki akim tepesi
// bu jestlerde de var; kol donus boyunca duruyor ve servo darbesi
// kesiliyor, ucuncu onlem 100 nF'lar.

// Bir jestin toplam tekerlek suresi bunu asamaz.
//
// 🔴 13.09.2026: 900 -> 1400, VE BU BIR GEVSEME. Gerekcesi
// kullanicinin olcumu: 3.5.19'da jest basina hareket sayisi dortten
// IKIYE inmisti ve kullanici "daha kotu hale mi getirdin, dans et
// diyorum iki hareket yapiyor, seri degil, eglenceli degil" dedi.
//
// ⚠️ 3.5.19'DA YANLIS DEGISKEN BUYUTULDU. Ileri kaymayi azaltmak
// icin "ayni donus suresi, yarisi kadar kalkis" secildi. Donus suresi
// gercekten arttı ama INSANIN GORDUGU SEY TOPLAM SURE DEGIL, AYRI AYRI
// HAREKET SAYISI. Iki genis salinim, dort kisa salinimdan daha CANSIZ
// gorunuyor — olculdu, kullanici soyledi.
//
// Simdi ikisi birden aliniyor: dort hareket VE her kare yeterince uzun
// (>= 280 ms, yani darbe bittikten sonra gercek donus var). Bunun
// bedeli dogrudan ve acikca kayma: dort kalkis, iki kalkisin iki kati
// surunme. Kullanici bunu bilerek kabul etti; kaymanin gercek cozumu
// zaten mekanik (BEDEN.md · "govdede DORT tekerlek var").
inline constexpr int JEST_TEKER_EN_COK_MS = 1400;

// Yerinde donus hizini iki tekerlege dagitir.
//
// `tavan` ebeveynin panelden verdigi hiz siniri: kaydiriciyi kisan bir
// ebeveyn Pati'nin KENDI hareketlerini de kismis oluyor. Iki ayri sayi
// olsaydi panel yalan soylerdi.
inline Surus jest_donus(int teker, int tavan)
{
    const int t = std::clamp(tavan, HIZ_TAVAN_EN_AZ, HIZ_TAVAN_EN_COK);
    // SURUS_X_YONU burada da uygulanıyor: `teker` de joystick'in x'i
    // gibi COCUGUN GORDUGU yonu anlatiyor. Ayri kalsalardi jest
    // tablosundaki "saga don" ile kumandanin "saga don"u zit yonler
    // olurdu — kimse fark etmezdi cunku jestlerin net donusu sifir,
    // ama kod okuyani yanlis bilgilendirirdi.
    const int h = std::clamp(teker, -JEST_DONUS_EN_COK, JEST_DONUS_EN_COK)
                  * t / 100 * SURUS_X_YONU;
    return {h, -h};
}

// ---------------------------------------------------------------------------
// Jest tablosu
// ---------------------------------------------------------------------------
//
// Kare = "kollari su yuzdeye getir, tekerlegi su hizda dondur, sonra su
// kadar bekle". Kolda -1 = "bu kolu degistirme", yuzde 0 = asagi.
//
// 🔴 UC KURAL — ucu de konak testinde zorlaniyor:
//
//   1. TEKERLEK DONEN BIR KARE KOL OYNATMIYOR. Sebep olculmus
//      (06.09.2026): ikisi de ayni AA hattindan besleniyor ve motor
//      kalkisi gerilimde cokuntu yapiyor; o anda hareket eden servo
//      titriyor ya da sifirlaniyor. Gorev ayrica tekerlek donerken
//      servo darbesini tamamen kesiyor (pati_beden.cpp).
//
//   2. YON DEGISTIRMEDEN ONCE SIFIR KARESI VAR. Iki sebep: motoru
//      dogrudan ters cevirmek en kotu akim tepesi, ve motor_rampa yon
//      degisimini bilerek YAVAS geciyor (5/tik) — araya sifir
//      konmazsa kisa bir kare boyunca motor sadece yavaslar, hic
//      donmez. Sifirdan sonra kalkis darbesi yeniden devreye giriyor.
//
//   3. NET DONUS SIFIR. Her jestin (teker x sure) toplami sifir, yani
//      Pati jestin sonunda BASLADIGI YONE bakiyor. Aksi halde cocuk
//      joystick'e bastiginda "ileri" sandigi yon baska bir yer olurdu.

struct Kare {
    std::int8_t   sol;        // kol yuzdesi, -1 = degistirme
    std::int8_t   sag;
    std::int8_t   teker;      // YERINDE donus: + saga, - sola, 0 = dur
    std::uint16_t bekle_ms;
};

inline constexpr Kare JEST_DINLEN[]  = {{0, 0, 0, 0}};
inline constexpr Kare JEST_SELAM[]   = {{-1, 85, 0, 90}, {-1, 55, 0, 90},
                                        {-1, 85, 0, 90}, {-1, 55, 0, 90},
                                        {0, 0, 0, 0}};
inline constexpr Kare JEST_IKI_KOL[] = {{95, 95, 0, 320}, {0, 0, 0, 0}};
inline constexpr Kare JEST_ALKIS[]   = {{55, 55, 0, 60}, {30, 30, 0, 60},
                                        {55, 55, 0, 60}, {30, 30, 0, 60},
                                        {55, 55, 0, 60}, {0, 0, 0, 0}};
inline constexpr Kare JEST_DUSUN[]   = {{50, 0, 0, 420}, {0, 0, 0, 0}};

// 🔴 JEST BASINA DORT HAREKET — KULLANICININ OLCUMU (13.09.2026)
//
// 3.5.19'da hareket sayisi 4'ten 2'ye indirilmisti (ileri kaymayi
// azaltmak icin) ve kullanicinin cevabi net oldu: "daha kotu hale mi
// getirdin... seri degil, eglenceli degil, eskiden bu kadar degildi".
//
// Ders: CANLILIGIN OLCUSU TOPLAM DONUS SURESI DEGIL, AYRI AYRI HAREKET
// SAYISI. Bir izleyici icin "dort kere kipirdadi" ile "iki kere genis
// dondu" ayni sey degil, ikincisi daha az canli.
//
// Uc kural birlikte tutuluyor:
//
//   1. DORT hareket (bak_etrafina ve firildak haric — onlarin kimligi
//      zaten tek buyuk savurma).
//   2. Her kare >= 280 ms, yani darbe (180) + rampa inisi (~60) bitince
//      geriye gercek donus kaliyor. Bundan kisasi yalnizca sarsinti.
//   3. Yon degistirme boslugu 40 ms — sifirlanamaz (motoru dogrudan
//      ters cevirmek en kotu akim tepesi) ama en kucuk makul deger.
//
// ⚠️ KOL KARESI IKI DONUSUN ARASINA KONMAZ. Kullanicinin sozu:
// "1. ve 2. dc motor arasi mesafe cok fazla". Sebebi tabloda duruyordu:
// dans'ta iki donusun arasinda bir kol karesi vardi ve kol kareleri
// KOLUN VARMASINI BEKLIYOR (~180 ms yol + bekleme), yani iki donus
// arasi 40 ms degil ~350 ms oluyordu. Kollar artik donuslerin ONUNDE ve
// ARKASINDA.

// Sevinc: dort salinim, sonra kollarla kutlama.
inline constexpr Kare JEST_SEVIN[] = {
    {-1, -1,  75, 320}, {-1, -1, 0, 40},
    {-1, -1, -75, 320}, {-1, -1, 0, 40},
    {-1, -1,  75, 320}, {-1, -1, 0, 40},
    {-1, -1, -75, 320}, {-1, -1, 0, 40},
    {95, 95,   0, 180}, { 0,  0,  0,   0},
};

// Kikirdama: tablonun en hizli dort salinimi, sonra kollarla ziplama.
// 280 ms kuralin tam tabaninda — bilerek, cunku bu jestin kimligi HIZ.
inline constexpr Kare JEST_TITRE[] = {
    {-1, -1,  75, 280}, {-1, -1, 0, 40},
    {-1, -1, -75, 280}, {-1, -1, 0, 40},
    {-1, -1,  75, 280}, {-1, -1, 0, 40},
    {-1, -1, -75, 280}, {-1, -1, 0, 40},
    {70, 70,   0, 100}, { 0,  0,  0,   0},
};

// "Hayir" — kafa sallamanin tekerlekli karsiligi. Bedenin ANLAM
// tasidigi tek jest: Pati bir seye hayir derken bunu yapiyor.
// Anlam salinim SAYISINDA, o yuzden dort.
inline constexpr Kare JEST_HAYIR[] = {
    {-1, -1, -70, 300}, {-1, -1, 0, 40},
    {-1, -1,  70, 300}, {-1, -1, 0, 40},
    {-1, -1, -70, 300}, {-1, -1, 0, 40},
    {-1, -1,  70, 300}, {-1, -1, 0,  0},
};

// Merak: yavasca don, DUR VE BAK, sonra geri don.
// Bu jestin kimligi BEKLEMEK; dort salinim kurali buraya uygulanmiyor,
// yoksa "bak" degil "titre" olurdu.
inline constexpr Kare JEST_BAK[] = {
    {-1, -1,  70, 440}, {-1, -1, 0, 400},
    {-1, -1, -70, 440}, {-1, -1, 0,   0},
};

// Firildak: tablonun en buyuk iki savurmasi. Kol yok.
//
// Tek kare 600 ms — darbe ve rampa bittikten sonra ~360 ms boyunca
// tablodaki hizda donuyor, yani Pati gercekten kendi etrafinda
// savruluyor. Dort kisa salinimin yapamadigi sey bu.
inline constexpr Kare JEST_FIRILDAK[] = {
    {-1, -1,  75, 600}, {-1, -1, 0, 40},
    {-1, -1, -75, 600}, {-1, -1, 0,  0},
};

// Zipla: kollarla hizli ziplama. TEKERLEK YOK — ve bu bilincli.
// Kol hareketinin ileri kayma maliyeti sifir, yani canlilik buradan
// bedavaya geliyor.
inline constexpr Kare JEST_ZIPLA[] = {
    {85, 85, 0, 80}, {20, 20, 0, 80},
    {85, 85, 0, 80}, {20, 20, 0, 80},
    {85, 85, 0, 80}, { 0,  0, 0,  0},
};

// Dans: hazirlik — DORT SALINIM ust uste — kol koreografisi.
//
// ⚠️ 3.5.19'a kadar kol kareleri donuslerin ARASINDAYDI ve jestin
// ritmini bozan sey oydu (yukaridaki kol karesi uyarisi). Artik dort
// donus kesintisiz, kollar once ve sonra.
inline constexpr Kare JEST_DANS[] = {
    {75, 75,   0, 110},
    {-1, -1,  75, 320}, {-1, -1, 0, 40},
    {-1, -1, -75, 320}, {-1, -1, 0, 40},
    {-1, -1,  75, 320}, {-1, -1, 0, 40},
    {-1, -1, -75, 320}, {-1, -1, 0, 40},
    {20, 20,   0, 110},
    {95, 95,   0, 130},
    {30, 30,   0, 110},
    {95, 95,   0, 130},
    { 0,  0,   0,   0},
};

// Tek kol. Cocuk "sag kolunu kaldir" dedi diye var.
//
// Kaldirip 900 ms TUTUYOR, sonra indiriyor. Tutmanin bedeli yok: servo
// hedefe varinca darbe kesiliyor (KOL_SUS_GECIKME) ve kol hafif plastik
// oldugu icin kendi agirligiyla dusmuyor.
//
// Sifirinci kare digerini de acikca INDIRIYOR (-1 degil 0): "tek kolunu
// kaldir" denince onceki jestten kalan oteki kolun havada kalmasi,
// hareketi okunmaz yapardi.
inline constexpr Kare JEST_SAG_KOL[] = {{0, 95, 0, 900}, {0, 0, 0, 0}};
inline constexpr Kare JEST_SOL_KOL[] = {{95, 0, 0, 900}, {0, 0, 0, 0}};

struct Jest {
    const char*  ad;
    const Kare*  kare;
    std::uint8_t adet;
    bool         kendiliginden;   // konusurken secilebilir mi
    bool         teker_var;       // tekerlek kullaniyor mu
};

// `teker_var` elle YAZILMIYOR gibi gorunsun diye degil, gorunur olsun
// diye elle yaziliyor — ama konak testi tabloyla karsilastiriyor, yani
// yanlis yazilamiyor. Yeni bir jest eklerken bu alani unutmak, testi
// dusuren bir hata.
inline constexpr Jest JESTLER[] = {
    {"dinlen",       JEST_DINLEN,   1, false, false},
    {"selam",        JEST_SELAM,    5, true,  false},
    {"iki_kol",      JEST_IKI_KOL,  2, true,  false},
    {"alkis",        JEST_ALKIS,    6, true,  false},
    {"dusun",        JEST_DUSUN,    2, true,  false},
    {"zipla",        JEST_ZIPLA,    6, true,  false},
    {"sevin",        JEST_SEVIN,   10, true,  true},
    {"titre",        JEST_TITRE,   10, true,  true},
    {"hayir",        JEST_HAYIR,    8, false, true},
    {"bak_etrafina", JEST_BAK,      4, true,  true},
    {"firildak",     JEST_FIRILDAK, 4, true,  true},
    {"dans",         JEST_DANS,    14, true,  true},
    {"sag_kol",      JEST_SAG_KOL,  2, false, false},
    {"sol_kol",      JEST_SOL_KOL,  2, false, false},
};
inline constexpr int JEST_ADET = sizeof(JESTLER) / sizeof(JESTLER[0]);

// std::strcmp derleme zamaninda kullanilamiyor; jest numaralari ise
// sabit olmali (pati_beden.cpp "selam"i numarayla istiyor). Elle
// yazilan bir numara, tabloya bir satir eklenince sessizce baska bir
// jesti gosterirdi.
constexpr bool jest_adi_esit(const char* a, const char* b)
{
    while (*a != '\0' && *a == *b) { ++a; ++b; }
    return *a == *b;
}

constexpr int jest_no(const char* ad)
{
    for (int i = 0; i < JEST_ADET; ++i) {
        if (jest_adi_esit(JESTLER[i].ad, ad)) return i;
    }
    return -1;
}

}  // namespace pati
