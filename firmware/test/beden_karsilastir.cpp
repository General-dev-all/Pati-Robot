// Bedenin matematigi — konak testi
//
// ===========================================================================
// NEDEN BU TEST VAR
// ===========================================================================
//
// Buradaki dort fonksiyonun hatasi DONANIMA odeniyor ve ikisi de gozle
// gorulmuyor:
//
//   1. SERVO ARALIGI. Darbe 500-2500 us disina cikarsa servo mekanik
//      dayanmaya biniyor: donmeye calisip duruyor, isiniyor ve akim
//      cekmeye devam ediyor. Belirtisi "kol biraz eksik kalkiyor" ve
//      surekli bir vizilti — kimse bunu bir tam sayi tasmasi diye
//      okumuyor.
//
//   2. KARISTIRMANIN ISARETI. Ters olursa "sola bas, saga gitsin"
//      oluyor. Bu ancak robot MASADAYKEN fark ediliyor ve ilk fark
//      edildigi an zaten kotu bir an.
//
//   3. HIZ TAVANI. Caprazda (x=100, y=100) kirpma sirasi yanlis olursa
//      tavan hicbir sey yapmiyor: ebeveyn %30 seciyor, Pati tam guc
//      gidiyor. Panelde her sey dogru gorunuyor.
//
//   4. OLU BOLGE. Sifirin ustundeki her istek donduren bir duty'ye
//      cikmali; cikmazsa tekerlek donmeyip yalnizca vizildiyor ve
//      "motor bozuk" saniliyor.
//
// Dordu de saf tam sayi aritmetigi, yani konak makinede bedava
// sininiyor. Donanima dokunan kisim (LEDC, GPIO) testin disinda kaldi;
// o yuzden pati_beden_matematik.hpp ayri bir dosya.

#include <cstdio>
#include <cstdlib>

#include "../main/pati_beden_matematik.hpp"

using namespace pati;

namespace {

int g_hata = 0;
int g_gecen = 0;

void kontrol(bool dogru, const char* ne)
{
    if (dogru) {
        ++g_gecen;
        return;
    }
    ++g_hata;
    std::printf("   BASARISIZ: %s\n", ne);
}

// ---------------------------------------------------------------------------
// 1) Servo darbesi ARALIK DISINA CIKMAMALI
// ---------------------------------------------------------------------------
void servo_araligi()
{
    std::printf("\n 1) servo darbe araligi\n");

    int en_az = 999999, en_cok = -999999;
    for (int yuzde = 0; yuzde <= 100; ++yuzde) {
        for (int sag = 0; sag < 2; ++sag) {
            const int us = kol_darbe_us(kol_derece10(yuzde, sag != 0));
            if (us < en_az) en_az = us;
            if (us > en_cok) en_cok = us;
            kontrol(us >= SERVO_US_EN_AZ && us <= SERVO_US_EN_COK,
                    "darbe araligin disinda");
        }
    }
    std::printf("    kullanilan aralik: %d - %d us (sinir %d - %d)\n",
                en_az, en_cok, SERVO_US_EN_AZ, SERVO_US_EN_COK);

    // Disaridan gelen bozuk deger de servoyu dayanmaya bindirmemeli.
    for (int d : {-5000, -1, 0, 1800, 1801, 99999}) {
        const int us = kol_darbe_us(d);
        kontrol(us >= SERVO_US_EN_AZ && us <= SERVO_US_EN_COK,
                "kirpma disaridan gelen degerde calismiyor");
    }
    // Yuzde de kirpilmali.
    for (int y : {-50, -1, 101, 5000}) {
        for (int sag = 0; sag < 2; ++sag) {
            const int d = kol_derece10(y, sag != 0);
            kontrol(d >= 0 && d <= 1800, "yuzde kirpilmiyor");
        }
    }
}

// ---------------------------------------------------------------------------
// 2) Iki kol AYNALI olmali
// ---------------------------------------------------------------------------
//
// Ayni yuzde iki kolda ayni FIZIKSEL yuksekligi vermeli. Servolar
// karsilikli monte edildigi icin acilarin toplami 180 derece.
void kol_aynasi()
{
    std::printf("\n 2) kollarin aynalanmasi\n");
    if (!KOL_SAG_AYNA) {
        std::printf("    KOL_SAG_AYNA kapali — aynalama sinanmiyor\n");
        return;
    }
    for (int yuzde = 0; yuzde <= 100; ++yuzde) {
        const int sol = kol_derece10(yuzde, false);
        const int sag = kol_derece10(yuzde, true);
        kontrol(sol + sag == 1800, "kollar aynali degil");
    }
    // Yon: yuzde artarken sol aci BUYUYOR, sag aci KUCULUYOR.
    for (int yuzde = 1; yuzde <= 100; ++yuzde) {
        kontrol(kol_derece10(yuzde, false) >= kol_derece10(yuzde - 1, false),
                "sol kol yuzdeyle birlikte artmiyor");
        kontrol(kol_derece10(yuzde, true) <= kol_derece10(yuzde - 1, true),
                "sag kol yuzdeyle birlikte azalmiyor");
    }
    std::printf("    %%0  -> sol %.1f  sag %.1f derece\n",
                kol_derece10(0, false) / 10.0, kol_derece10(0, true) / 10.0);
    std::printf("    %%100-> sol %.1f  sag %.1f derece\n",
                kol_derece10(100, false) / 10.0, kol_derece10(100, true) / 10.0);
}

// ---------------------------------------------------------------------------
// 3) Motor duty: olu bolge ve tavan
// ---------------------------------------------------------------------------
void motor_dutysi()
{
    std::printf("\n 3) motor duty\n");

    kontrol(motor_duty(0) == 0, "durma sifir duty vermiyor");

    int onceki = 0;
    for (int h = 1; h <= 100; ++h) {
        const int d = motor_duty(h);
        kontrol(d >= MOTOR_EN_AZ_DUTY, "olu bolgenin altinda duty uretiliyor");
        kontrol(d < MOTOR_ADIM, "duty cozunurlugu asiyor");
        kontrol(d >= onceki, "duty hizla birlikte artmiyor");
        onceki = d;
        // Isaret duty'de KAYBOLMALI — yonu pin secimi belirliyor.
        kontrol(motor_duty(-h) == d, "geri yon farkli duty veriyor");
    }
    // Kirpma.
    kontrol(motor_duty(5000) == motor_duty(100), "ust kirpma yok");
    kontrol(motor_duty(-5000) == motor_duty(100), "alt kirpma yok");

    std::printf("    olu bolge %d / %d  (%%%d)\n", MOTOR_EN_AZ_DUTY,
                MOTOR_ADIM, MOTOR_EN_AZ_DUTY * 100 / MOTOR_ADIM);
    std::printf("    %%1 -> %d   %%50 -> %d   %%100 -> %d\n",
                motor_duty(1), motor_duty(50), motor_duty(100));
}

// ---------------------------------------------------------------------------
// 4) Surus karistirma
// ---------------------------------------------------------------------------
void karistirma()
{
    std::printf("\n 4) surus karistirma\n");

    // Orta = durma.
    const Surus orta = surus_karistir(0, 0, 100);
    kontrol(orta.sol == 0 && orta.sag == 0, "orta noktada motor donuyor");

    // Ileri = iki tekerlek de ayni yone, esit.
    const Surus ileri = surus_karistir(0, 100, 100);
    kontrol(ileri.sol == 100 && ileri.sag == 100, "ileri esit degil");

    // Geri = ikisi de negatif, esit.
    const Surus geri = surus_karistir(0, -100, 100);
    kontrol(geri.sol == -100 && geri.sag == -100, "geri esit degil");

    // 🔴 TAM SAGA = YERINDE DONUS. Sol ileri, sag geri, buyuklukler esit.
    // Ters cikarsa "sola bas saga gitsin" hatasi budur.
    const Surus sagaDon = surus_karistir(100, 0, 100);
    kontrol(sagaDon.sol == 100 && sagaDon.sag == -100,
            "saga donus yanlis yone gidiyor");
    const Surus solaDon = surus_karistir(-100, 0, 100);
    kontrol(solaDon.sol == -100 && solaDon.sag == 100,
            "sola donus yanlis yone gidiyor");

    // Simetri: x'in isareti degisince iki tekerlek yer degistirmeli.
    for (int x = -100; x <= 100; x += 5) {
        for (int y = -100; y <= 100; y += 5) {
            const Surus a = surus_karistir(x, y, 100);
            const Surus b = surus_karistir(-x, y, 100);
            kontrol(a.sol == b.sag && a.sag == b.sol,
                    "karistirma sag/sol simetrik degil");
        }
    }

    // 🔴 HIZ TAVANI HER YERDE, CAPRAZDA DA. Kirpma carpimdan once
    // yapilmazsa capraz itiste (x=100, y=100) tavan hicbir sey yapmiyor.
    for (int tavan : {HIZ_TAVAN_EN_AZ, 55, 75, HIZ_TAVAN_EN_COK}) {
        for (int x = -100; x <= 100; x += 10) {
            for (int y = -100; y <= 100; y += 10) {
                const Surus m = surus_karistir(x, y, tavan);
                kontrol(std::abs(m.sol) <= tavan && std::abs(m.sag) <= tavan,
                        "hiz tavani asiliyor");
            }
        }
    }
    const Surus capraz = surus_karistir(100, 100, 55);
    std::printf("    capraz itis, tavan %%55 -> sol %d  sag %d\n",
                capraz.sol, capraz.sag);
    kontrol(capraz.sol == 55, "caprazda tavan uygulanmiyor");

    // Tavan kirpilmali: panel bozuk bir deger gonderemesin, ve ALT
    // sinirin altina duserse tekerlek hic donmezdi.
    for (int t : {-100, 0, 5, 39, 500}) {
        const Surus m = surus_karistir(100, 100, t);
        kontrol(std::abs(m.sol) <= HIZ_TAVAN_EN_COK, "tavan ust sinirdan tasiyor");
        kontrol(std::abs(m.sol) >= HIZ_TAVAN_EN_AZ,
                "tavan alt sinirin altina dusuyor — tekerlek donmez");
    }
    std::printf("    tavan araligi: %%%d - %%%d\n",
                HIZ_TAVAN_EN_AZ, HIZ_TAVAN_EN_COK);
}

// ---------------------------------------------------------------------------
// 5) Yumusak kalkis
// ---------------------------------------------------------------------------
//
// Kullanicinin acik istegi: motorlar anlik tam guce gecmesin. Iki sey
// birden dogru olmali ve ikisi de tek satirlik bir hatayla bozulur:
//
//   - YUKARI cikis kademeli (akim tepesi dussun)
//   - SIFIRA inis ANINDA (olu adam beklemez; gecikme masa kenarinda
//     santimetre demek)
void yumusak_kalkis()
{
    std::printf("\n 5) yumusak kalkis\n");

    // Durmak asla beklemez — hangi hizdan olursa olsun.
    for (int v = -100; v <= 100; v += 7) {
        kontrol(motor_rampa(0, v) == 0, "durmak rampadan geciyor");
    }

    // Yukari cikis adim adim, ve adim asilmiyor.
    int su_an = 0;
    int tik = 0;
    while (su_an != 100 && tik < 1000) {
        const int yeni = motor_rampa(100, su_an);
        kontrol(yeni - su_an <= MOTOR_RAMPA_ADIM, "rampa adimi asiliyor");
        kontrol(yeni > su_an, "rampa ilerlemiyor");
        su_an = yeni;
        ++tik;
    }
    kontrol(su_an == 100, "rampa hedefe varmiyor");
    std::printf("    0 -> %%100: %d tik (%d ms)\n", tik, tik * 20);
    kontrol(tik >= 10, "rampa cok hizli — kalkis tepesi dusmez");

    // Yon degistirme SIFIRDAN GECMELI: sert ters cevirme en kotu akim
    // tepesi. +60'tan -60'a giderken 0 mutlaka ziyaret edilmeli.
    su_an = 60;
    bool sifir_gorundu = false;
    for (int i = 0; i < 200 && su_an != -60; ++i) {
        su_an = motor_rampa(-60, su_an);
        if (su_an == 0) sifir_gorundu = true;
    }
    kontrol(su_an == -60, "ters yone varilmiyor");
    kontrol(sifir_gorundu, "yon degisimi sifirdan gecmiyor");

    // Hedefe cok yakinken tam oturmali, salinmamali.
    for (int f = -MOTOR_RAMPA_ADIM; f <= MOTOR_RAMPA_ADIM; ++f) {
        kontrol(motor_rampa(50, 50 - f) == 50, "hedefin yaninda salinim var");
    }

    // YAVASLAMA hizlanmadan HIZLI olmali: akim zaten dusuyor, yavas
    // inmenin faydasi yok ve kalkis darbesinden sonra istenen yavas hiza
    // cabuk oturmak gerekiyor.
    const int hizlanma = motor_rampa(100, 50) - 50;      // 50 -> yukari
    const int yavaslama = 50 - motor_rampa(10, 50);      // 50 -> asagi
    kontrol(yavaslama > hizlanma, "yavaslama hizlanmadan hizli degil");
    std::printf("    hizlanma %d/tik · yavaslama %d/tik · kalkis %d/tik\n",
                hizlanma, yavaslama, MOTOR_KALKIS_ADIM);
}

// ---------------------------------------------------------------------------
// 6) Kalkis darbesi
// ---------------------------------------------------------------------------
//
// Motor DURURKEN kalkmak icin yuksek guc istiyor, donduginde cok azi
// yetiyor. Darbe bu ikisini ayiriyor. Iki sey birden dogru olmali:
// darbe surtunmeyi kiracak kadar HIZLI cikmali, ama yine de RAMPALI
// olmali — kullanicinin kurali "anlik tam guc yok".
void kalkis_darbesi()
{
    std::printf("\n 6) kalkis darbesi\n");

    // Darbe anlik DEGIL: tek tikta tepeye ciplak siçramamali.
    kontrol(motor_rampa(MOTOR_KALKIS_DUTY, 0, true) < MOTOR_KALKIS_DUTY,
            "darbe tek tikta tepeye siçriyor — rampasiz");

    // Ama hizli olmali: darbe suresi icinde tepeye varmali.
    int su_an = 0, tik = 0;
    while (su_an != MOTOR_KALKIS_DUTY && tik < 1000) {
        su_an = motor_rampa(MOTOR_KALKIS_DUTY, su_an, true);
        ++tik;
    }
    const int cikis_ms = tik * 20;
    kontrol(su_an == MOTOR_KALKIS_DUTY, "darbe tepeye varmiyor");
    kontrol(cikis_ms < MOTOR_KALKIS_MS,
            "darbe kendi suresi icinde tepeye varamiyor");
    std::printf("    tepeye cikis %d ms / darbe suresi %d ms\n",
                cikis_ms, MOTOR_KALKIS_MS);

    // Darbe kalkistan HIZLI, hizlanmadan da hizli olmali.
    kontrol(MOTOR_KALKIS_ADIM > MOTOR_RAMPA_ADIM,
            "darbe normal hizlanmadan hizli degil");

    // 🔴 DURMAK DARBEDEN DE ETKILENMEMELI. Olu adam beklemez.
    for (int v = -100; v <= 100; v += 11) {
        kontrol(motor_rampa(0, v, true) == 0, "darbe kipinde durmak gecikiyor");
    }

    // Darbe TABAN, tavan degil: istek darbeden buyukse istek kazanmali.
    // (Bu kural gorevde uygulaniyor; burada sadece degerin mumkun
    // oldugunu dogruluyoruz.)
    kontrol(MOTOR_KALKIS_DUTY < 100, "darbe tam guc — kural deliniyor");
}

// ---------------------------------------------------------------------------
// 7) Egrisel joystick tepkisi
// ---------------------------------------------------------------------------
//
// Dogrusalken joystick'in ilk milimetresi bile gitme tabaninin ustune
// atliyordu: cocuk icin "duruyor" ile "firliyor" arasinda ara yoktu.
void egri()
{
    std::printf("\n 7) egrisel tepki\n");

    kontrol(hiz_egrisi(0) == 0, "orta nokta sifir degil");
    kontrol(hiz_egrisi(100) == 100, "tam itiste tam hiz yok");
    kontrol(hiz_egrisi(-100) == -100, "tam geride tam hiz yok");

    // Tek fonksiyon: isaret korunmali.
    for (int h = -100; h <= 100; ++h) {
        kontrol(hiz_egrisi(-h) == -hiz_egrisi(h), "egri tek fonksiyon degil");
    }

    // Egri HER ZAMAN dogrusalin altinda kalmali (yumusatiyor, sertlestirmiyor).
    for (int h = 0; h <= 100; ++h) {
        kontrol(hiz_egrisi(h) <= h, "egri dogrusaldan sert");
    }

    // Monoton: itdikce artmali, yoksa kumanda tutarsiz hissettirir.
    for (int h = 1; h <= 100; ++h) {
        kontrol(hiz_egrisi(h) >= hiz_egrisi(h - 1), "egri monoton degil");
    }

    // Kirpma.
    kontrol(hiz_egrisi(5000) == 100, "ust kirpma yok");
    kontrol(hiz_egrisi(-5000) == -100, "alt kirpma yok");

    std::printf("    %%25 -> %d   %%50 -> %d   %%75 -> %d   %%100 -> %d\n",
                hiz_egrisi(25), hiz_egrisi(50), hiz_egrisi(75),
                hiz_egrisi(100));

    // Kumandanin alt yarisi GERCEKTEN yavas olmali: yarim itiste hizin
    // dortte biri. Bu, "cok hizli" sikayetinin dogrudan karsiligi.
    kontrol(hiz_egrisi(50) <= 30, "yarim itiste hala hizli");
}

}  // namespace

int main()
{
    std::printf("\n  BEDEN MATEMATIGI\n");
    std::printf("  ------------------------------------------------------\n");

    servo_araligi();
    kol_aynasi();
    motor_dutysi();
    karistirma();
    yumusak_kalkis();
    kalkis_darbesi();
    egri();

    std::printf("\n  ------------------------------------------------------\n");
    if (g_hata == 0) {
        std::printf("  GECTI — %d kontrol\n\n", g_gecen);
        return 0;
    }
    std::printf("  BASARISIZ — %d hata (%d kontrol gecti)\n\n", g_hata,
                g_gecen);
    return 1;
}
