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
//   5. 🔴 OZERK JESTIN YER DEGISTIRMESI. Pati konusurken ve cocugun
//      sesli komutuyla kendi kararıyla kipirdiyor. Ucurum sensoru YOK:
//      yer degistiren bir ozerk hareket, eninde sonunda masadan
//      dusmek demek. Tablodaki tek bir isaret hatasi bunu yapar ve
//      ancak robot masadayken fark edilir.
//
// Dordu de saf tam sayi aritmetigi, yani konak makinede bedava
// sininiyor. Donanima dokunan kisim (LEDC, GPIO) testin disinda kaldi;
// o yuzden pati_beden_matematik.hpp ayri bir dosya.

#include <cstdio>
#include <cstdlib>

#include "../main/pati_beden_matematik.hpp"

// Modelin isteyebilecegi hareket adlari icin. Uretilen baslik saf
// C++ — IDF'e dokunmuyor, konakta derleniyor.
#include "../main/pati_kisilik_uretilmis.h"

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
    // 🔴 ISARET GERCEK KARTTA OLCULDU (06.09.2026): once tersti ve
    // cubugu saga itince Pati SOLA donuyordu. Buradaki iki satir o
    // olcumun kaydi — degistirmeden once
    // pati_beden_matematik.hpp'deki SURUS_X_YONU aciklamasini oku.
    kontrol(sagaDon.sol == -100 && sagaDon.sag == 100,
            "saga donus yanlis yone gidiyor");
    const Surus solaDon = surus_karistir(-100, 0, 100);
    kontrol(solaDon.sol == 100 && solaDon.sag == -100,
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
    const Surus capraz = surus_karistir(-100, 100, 55);
    std::printf("    capraz itis, tavan %%55 -> sol %d  sag %d\n",
                capraz.sol, capraz.sag);
    kontrol(capraz.sol == 55, "caprazda tavan uygulanmiyor");

    // Tavan kirpilmali: panel bozuk bir deger gonderemesin, ve ALT
    // sinirin altina duserse tekerlek hic donmezdi.
    // x NEGATIF: SURUS_X_YONU = -1 oldugu icin sol tekerlegin tam
    // komut aldigi kose burasi. (100, 100) verilseydi sol tam sifir
    // cikardi ve test tavani degil, kendi kosesini olcerdi.
    for (int t : {-100, 0, 5, 39, 500}) {
        const Surus m = surus_karistir(-100, 100, t);
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

// ---------------------------------------------------------------------------
// 5b) 🔴 KOLUN MEKANIK ARALIGI
// ---------------------------------------------------------------------------
//
// Kollarin ucuna gercek kol takilinca sol kol asagida tekerlege,
// yukarida ustteki kabloya carpiyor. Asilirsa kol dayaniyor, servo
// donmeye calisip duruyor, isiniyor ve akim cekmeye devam ediyor —
// sonu yanmis servo, belirtisi surekli bir vizilti.
//
// Aralik disina TEK BIR deger uretmek yetiyor. Bu yuzden burada tum
// giris uzayi taraniyor: hicbir yuzde, hicbir aralik, hicbir tasma
// sinirin disina cikmamali.
void kol_araligi()
{
    std::printf("\n 5b) kolun mekanik araligi\n");

    for (int az = -20; az <= 120; az += 5) {
        for (int cok = -20; cok <= 120; cok += 5) {
            const int a = std::clamp(az, 0, 100);
            const int b = std::clamp(cok, 0, 100);
            for (int y = -50; y <= 150; y += 5) {
                const int c = kol_sinirla(y, az, cok);

                // Her zaman gecerli bir yuzde.
                kontrol(c >= 0 && c <= 100, "kirpma yuzde araligini asiyor");

                if (a <= b) {
                    kontrol(c >= a && c <= b, "kol sinirin DISINA cikti");
                } else {
                    // Ters aralik: tabana yaslaniyoruz. Kolun hic
                    // oynamamasi, yanlis yere gitmesinden iyi.
                    kontrol(c == a, "ters aralikta tabana yaslanmiyor");
                }
            }
        }
    }
    std::printf("    taranan tum (yuzde x aralik) ciftleri sinir icinde\n");

    // Varsayilanlar kullanicinin olcumu (09.09.2026).
    kontrol(KOL_SOL_VARSAYILAN_AZ == 10 && KOL_SOL_VARSAYILAN_COK == 70,
            "sol kol varsayilani degismis");
    kontrol(KOL_SAG_VARSAYILAN_AZ == 0 && KOL_SAG_VARSAYILAN_COK == 100,
            "sag kol varsayilani degismis");

    // 🔴 JEST TABLOSU SINIRI ASAMIYOR. Tablo %95'e kadar deger
    // tasiyor; sol kolda o deger kirpilmali, yoksa jest kolu kabloya
    // carptirir.
    int en_yuksek_sol = 0;
    for (int j = 0; j < JEST_ADET; ++j) {
        for (int k = 0; k < JESTLER[j].adet; ++k) {
            const int sol = JESTLER[j].kare[k].sol;
            if (sol < 0) continue;
            const int c = kol_sinirla(sol, KOL_SOL_VARSAYILAN_AZ,
                                      KOL_SOL_VARSAYILAN_COK);
            kontrol(c <= KOL_SOL_VARSAYILAN_COK && c >= KOL_SOL_VARSAYILAN_AZ,
                    "jest karesi sol kol sinirini asiyor");
            if (c > en_yuksek_sol) en_yuksek_sol = c;
        }
    }
    std::printf("    jestlerin sol kolda ulastigi en yuksek deger: %%%d "
                "(sinir %%%d)\n", en_yuksek_sol, KOL_SOL_VARSAYILAN_COK);

    // Servo darbesi sinirlanmis yuzdelerde de guvenli aralikta mi?
    for (int y = KOL_SOL_VARSAYILAN_AZ; y <= KOL_SOL_VARSAYILAN_COK; ++y) {
        for (int sag = 0; sag < 2; ++sag) {
            const int us = kol_darbe_us(kol_derece10(y, sag == 1));
            kontrol(us >= SERVO_US_EN_AZ && us <= SERVO_US_EN_COK,
                    "sinirli yuzdede darbe araligin disinda");
        }
    }
}

// ---------------------------------------------------------------------------
// 6) 🔴 OZERK DONUS — PATI YERINDEN GIDEBILIR MI?
// ---------------------------------------------------------------------------
//
// Bu testteki en onemli kontrol. Pati'de ucurum sensoru yok; ozerk bir
// hareket yer degistirdigi anda masa kenari zaman meselesi.
//
// Guvence bir kural degil, bir YAPI: jest_donus tek bir sayi aliyor ve
// iki tekerlege TERS isaretle dagitiyor. Burada o yapinin gercekten
// tutup tutmadigi sininiyor — hicbir girdi, hicbir tavan, hicbir tasma
// iki tekerlegi ayni yone dondurememeli.
void ozerk_donus()
{
    std::printf("\n  6) OZERK DONUS — yer degistirme\n");

    // ---- HICBIR GIRDI ILERI GITMIYOR ------------------------------------
    //
    // Ileri gitmek = iki tekerlek AYNI yonde. Tum makul ve makul olmayan
    // girdileri tarayip bunun hic olmadigini gosteriyoruz.
    int en_buyuk = 0;
    for (int teker = -400; teker <= 400; ++teker) {
        for (int tavan = -50; tavan <= 200; tavan += 5) {
            const Surus d = jest_donus(teker, tavan);

            // ASIL KONTROL: toplam sifir, yani iki tekerlek her zaman
            // ters yonde ve esit buyuklukte. Toplam sifirdan farkli
            // olsaydi robotun net bir ilerlemesi olurdu.
            kontrol(d.sol + d.sag == 0, "ozerk donus YER DEGISTIRIYOR");

            kontrol(std::abs(d.sol) <= JEST_DONUS_EN_COK,
                    "ozerk donus hizi ust sinirini asiyor");
            kontrol(std::abs(d.sol) <= HIZ_TAVAN_EN_COK,
                    "ozerk donus tam gucu asiyor");
            if (std::abs(d.sol) > en_buyuk) en_buyuk = std::abs(d.sol);
        }
    }
    std::printf("    taranan tum girdilerde sol + sag == 0\n");
    std::printf("    en yuksek ozerk donus hizi: %%%d (sinir %%%d)\n",
                en_buyuk, JEST_DONUS_EN_COK);

    // ---- TAVAN OZERK HAREKETE DE UYGULANIYOR ----------------------------
    //
    // Ebeveyn kaydiriciyi kisiyorsa Pati'nin KENDI hareketleri de
    // kisilmali. Ayri kalsalardi panel yalan soylerdi: "hizi dusurdum"
    // ama Pati eskisi gibi donuyor.
    const int kisik = std::abs(jest_donus(JEST_DONUS_EN_COK,
                                          HIZ_TAVAN_EN_AZ).sol);
    const int acik  = std::abs(jest_donus(JEST_DONUS_EN_COK,
                                          HIZ_TAVAN_EN_COK).sol);
    kontrol(kisik < acik, "hiz tavani ozerk hareketi kismiyor");
    std::printf("    tavan %%%d -> donus %%%d   ·   tavan %%%d -> donus %%%d\n",
                HIZ_TAVAN_EN_AZ, kisik, HIZ_TAVAN_EN_COK, acik);

    // Sifir istegi sifir kalmali: jest bitince tekerlek durmali.
    for (int tavan : {HIZ_TAVAN_EN_AZ, 70, HIZ_TAVAN_EN_COK}) {
        const Surus d = jest_donus(0, tavan);
        kontrol(d.sol == 0 && d.sag == 0, "sifir istegi tekerlegi durdurmuyor");
    }
}

// ---------------------------------------------------------------------------
// 7) JEST TABLOSU — uc kural
// ---------------------------------------------------------------------------
//
// Tablo saf veri ama icindeki her hata donanima odeniyor. Ucu de
// yazarken "kucuk bir ayrinti" gorunuyor:
//
//   1. Tekerlek donen kare kol oynatmamali. Ikisi ayni AA hattinda ve
//      motor kalkisi gerilimde cokuntu yapiyor; o anda hareket eden
//      servo titriyor ya da sifirlaniyor (06.09.2026'da yasandi).
//
//   2. Yon degistirmeden once sifir karesi olmali. motor_rampa yon
//      degisimini bilerek YAVAS geciyor (5/tik = 400 ms); araya sifir
//      konmazsa 150 ms'lik bir kare boyunca motor yalnizca yavaslar,
//      hic donmez. Belirtisi "jest calismiyor" olur ve sebebi rampada
//      aranmaz.
//
//   3. Net donus sifir olmali. Aksi halde Pati her jestten sonra biraz
//      daha baska bir yone bakar ve cocugun "ileri" sandigi yon
//      kayar — kumandayi ogrenilemez yapar.
void jest_tablosu()
{
    std::printf("\n  7) JEST TABLOSU — %d jest\n", JEST_ADET);

    for (int j = 0; j < JEST_ADET; ++j) {
        const Jest& g = JESTLER[j];
        kontrol(g.ad != nullptr && g.ad[0] != '\0', "jestin adi yok");
        kontrol(g.adet > 0, "jestte hic kare yok");

        bool teker_gorundu = false;
        int  net_donus = 0;      // teker x sure
        int  teker_ms = 0;
        int  onceki_isaret = 0;  // en son sifir OLMAYAN yon

        for (int k = 0; k < g.adet; ++k) {
            const Kare& kare = g.kare[k];

            kontrol(kare.sol == -1 || (kare.sol >= 0 && kare.sol <= 100),
                    "kol yuzdesi araligin disinda");
            kontrol(kare.sag == -1 || (kare.sag >= 0 && kare.sag <= 100),
                    "kol yuzdesi araligin disinda");
            kontrol(std::abs(kare.teker) <= JEST_DONUS_EN_COK,
                    "kare donus sinirini asiyor");

            // \U0001f534 KOL HIZI TANIMLI UC DEGERDEN BIRI OLMALI.
            // kol_hiz_derece_sn tanimadigi degeri sessizce NORMAL
            // sayiyor, yani tablodaki bir yazim hatasi "jest niye
            // yavas oynuyor" diye aranirdi.
            kontrol(kare.hiz == KOL_NORMAL || kare.hiz == KOL_HIZLI
                        || kare.hiz == KOL_YAVAS,
                    "kol hizi tanimsiz");

            // Tekerlek karesi kol oynatmiyor, dolayisiyla hizinin bir
            // anlami da yok. Sifirdan farkliysa tabloyu yazan kisi
            // orada bir sey olacagini saniyor demektir.
            if (kare.teker != 0) {
                kontrol(kare.hiz == KOL_NORMAL,
                        "tekerlek karesine kol hizi yazilmis (etkisi yok)");
            }

            if (kare.teker == 0) {
                onceki_isaret = 0;   // sifir karesi: yon serbest
                continue;
            }

            // KURAL 1
            kontrol(kare.sol == -1 && kare.sag == -1,
                    "tekerlek donen kare kol da oynatiyor");

            // KURAL 2
            const int isaret = (kare.teker > 0) ? 1 : -1;
            kontrol(onceki_isaret == 0 || onceki_isaret == isaret,
                    "yon degisiminde sifir karesi yok");
            onceki_isaret = isaret;

            teker_gorundu = true;
            teker_ms  += kare.bekle_ms;
            net_donus += kare.teker * static_cast<int>(kare.bekle_ms);
        }

        // KURAL 3
        kontrol(net_donus == 0, "jestin net donusu sifir degil");

        kontrol(teker_ms <= JEST_TEKER_EN_COK_MS,
                "jestin tekerlek suresi sinirin ustunde");

        // `teker_var` tabloyla tutarli mi? Yeni bir jest eklerken bu
        // alani unutmak, jesti sessizce "kolsuz" ya da anahtardan
        // muaf yapardi.
        kontrol(g.teker_var == teker_gorundu,
                "teker_var alani karelerle uyusmuyor");

        // \U0001f534 KENDILIGINDEN SECILEN HER JESTIN BIR RUH KUMESI OLMALI.
        // Sifir birakmak, jesti hicbir ruh halinde SECILMEZ yapardi
        // (ilk tur onu hep eler) ve belirtisi "bu jest hic olmuyor"
        // olurdu — tabloda gorunur ama calismaz.
        if (g.kendiliginden) {
            kontrol(g.ruh != 0, "kendiliginden jestin ruh kumesi bos");
        }
        kontrol((g.ruh & ~RUH_HEPSI) == 0, "ruh kumesinde tanimsiz bit var");

        // \U0001f534 KALKIS SAYISI — surunmenin olcusu ve artik tekerlek
        // boslugunu belirleyen sayi (pati_beden.cpp). Elle sayilan bir
        // sey degil, tablodan cikiyor; yanlis sayarsa bir jest hak
        // ettiginden sik ya da seyrek oynar ve sebebi gorunmez olur.
        const int kalkis = jest_kalkis_sayisi(g);
        kontrol(kalkis == 0 || g.teker_var,
                "tekerleksiz jestte kalkis sayiliyor");
        kontrol(!g.teker_var || kalkis > 0,
                "tekerlekli jestte hic kalkis yok");

        std::printf("    %-14s %2d kare · teker %4d ms · %d kalkis · "
                    "net donus %d%s\n",
                    g.ad, g.adet, teker_ms, kalkis, net_donus,
                    g.teker_var ? "  [tekerlekli]" : "");
    }

    // jest_no derleme zamaninda calismali: pati_beden.cpp jestleri
    // numarayla istiyor ve elle yazilan bir numara, tabloya satir
    // eklenince sessizce baska bir jesti gosterirdi.
    static_assert(jest_no("selam") >= 0, "selam jesti yok");
    static_assert(jest_no("sevin") >= 0, "sevin jesti yok");
    static_assert(jest_no("dinlen") == 0, "dinlen ilk sirada degil");
    static_assert(jest_no("boyle_bir_jest_yok") == -1,
                  "jest_no olmayan adi buluyor");
    kontrol(jest_no("selam") >= 0, "jest_no selami bulamiyor");

    // ---- MODELIN ISTEYEBILECEGI HER AD TABLODA OLMALI -------------------
    //
    // Ad listesi prototype/yuz.py'den uretiliyor (HAREKET_ADLARI), jest
    // tablosu ise burada. Ikisi ayrisirsa belirtisi "Pati bazen dans
    // etmiyor" olur: model gecerli bir cagri yapar, cihaz adi tanimaz ve
    // sessizce hicbir sey olmaz. Kimse bunu bir ad uyusmazligi diye
    // okumaz.
    const int ad_adet = static_cast<int>(sizeof(HAREKET_ADLARI)
                                         / sizeof(HAREKET_ADLARI[0]));
    const int tek_adet = static_cast<int>(sizeof(HAREKET_TEKERLEKLI)
                                          / sizeof(HAREKET_TEKERLEKLI[0]));

    for (int i = 0; i < ad_adet; ++i) {
        const int no = jest_no(HAREKET_ADLARI[i]);
        kontrol(no >= 0, "modelin isteyebilecegi hareket tabloda yok");
        if (no < 0) {
            std::printf("      EKSIK: %s\n", HAREKET_ADLARI[i]);
            continue;
        }

        // ---- PROMPTUN "TEKERLEKLI" IDDIASI TABLOYLA UYUSUYOR MU ---------
        //
        // Ebeveyn anahtari kapatinca prompt modele "sunlari secme" diye
        // bir liste veriyor (HAREKET_TEKERLEKLI). Cihaz tarafinda ayni
        // bilgi jest tablosunun `teker_var` alaninda duruyor.
        //
        // Ayrisirlarsa iki yonlu hata cikiyor ve ikisi de sessiz:
        //   - Listede eksik bir jest -> anahtar kapaliyken model onu
        //     seciyor, tekerlek donmuyor, Pati "dans ediyorum" diyor.
        //   - Listede fazla bir jest -> anahtar kapaliyken model
        //     gereksiz yere kol jestinden de kaciniyor.
        bool listede = false;
        for (int t = 0; t < tek_adet; ++t) {
            if (jest_adi_esit(HAREKET_TEKERLEKLI[t], HAREKET_ADLARI[i])) {
                listede = true;
                break;
            }
        }
        kontrol(listede == JESTLER[no].teker_var,
                "promptun tekerlekli listesi jest tablosuyla uyusmuyor");
        if (listede != JESTLER[no].teker_var) {
            std::printf("      AYRISMA: %s  prompt=%d  tablo=%d\n",
                        HAREKET_ADLARI[i], listede ? 1 : 0,
                        JESTLER[no].teker_var ? 1 : 0);
        }
    }

    // Ters yon: listede olup HAREKETLER'de olmayan bir ad, modelin hic
    // gormeyecegi bir yasak demek olurdu. Uretici de bunu denetliyor
    // (prompt_uret.py) ama iki kapi bir kapidan iyi.
    for (int t = 0; t < tek_adet; ++t) {
        kontrol(jest_no(HAREKET_TEKERLEKLI[t]) >= 0,
                "tekerlekli listede tabloda olmayan ad var");
    }

    std::printf("    modelin %d hareket adinin hepsi tabloda var "
                "(%d tekerlekli)\n", ad_adet, tek_adet);
}


// ---------------------------------------------------------------------------
// 8) RUH HALI ESLEMESI
// ---------------------------------------------------------------------------
//
// Gozlerin ifade adlari uretilen tablodan geliyor (pati_goz_uretilmis.h),
// ruh kumeleri ise elle yazildi. Ayrisirlarsa belirtisi SESSIZ olur:
// Pati o ifadedeyken yalnizca ikinci turdan jest secer, yani beden yuze
// uymaz ve kimse bunu bir eslesme hatasi diye okumaz.
void ruh_eslemesi()
{
    std::printf("\n  8) RUH HALI ESLEMESI\n");

    struct Ornek { const char* ifade; std::uint8_t bekle; };
    const Ornek ornekler[] = {
        {"mutlu", RUH_NESE},   {"cok_mutlu", RUH_NESE},
        {"afacan", RUH_NESE},  {"haylaz", RUH_NESE},
        {"saskin", RUH_MERAK}, {"anlamadim", RUH_MERAK},
        {"dusunuyor", RUH_MERAK},
        {"uzgun", RUH_DUSUK},  {"kizgin", RUH_DUSUK},
        {"somurtkan", RUH_DUSUK}, {"uykulu", RUH_DUSUK},
        {"notr", RUH_SAKIN},   {"konusuyor", RUH_SAKIN},
        {"dinliyor", RUH_SAKIN}, {"bos", RUH_SAKIN},
    };
    for (const Ornek& o : ornekler) {
        kontrol(ruh_no(o.ifade) == o.bekle, "ifade yanlis ruha eslendi");
        if (ruh_no(o.ifade) != o.bekle) {
            std::printf("      %s -> %d (beklenen %d)\n", o.ifade,
                        ruh_no(o.ifade), o.bekle);
        }
    }

    // Tanimadigi ad SAKIN donmeli: yeni bir ifade eklenip burasi
    // unutulursa Pati jestsiz kalmamali.
    kontrol(ruh_no("boyle_bir_ifade_yok") == RUH_SAKIN,
            "bilinmeyen ifade SAKIN donmuyor");
    kontrol(ruh_no(nullptr) == RUH_SAKIN, "nullptr ifade SAKIN donmuyor");

    // \U0001f534 HER RUH HALINDE SECILEBILIR BIR JEST OLMALI.
    //
    // Ikinci tur (ruhsuz arama) zaten kurtariyor, ama bir ruh halinde
    // HIC jest olmamasi tasarim hatasidir: o ifade boyunca beden her
    // zaman "ruha uymayan" bir jest oynar.
    for (std::uint8_t r : {RUH_NESE, RUH_SAKIN, RUH_MERAK, RUH_DUSUK}) {
        int kol = 0, teker = 0;
        for (int j = 0; j < JEST_ADET; ++j) {
            if (!JESTLER[j].kendiliginden) continue;
            if ((JESTLER[j].ruh & r) == 0) continue;
            if (JESTLER[j].teker_var) ++teker; else ++kol;
        }
        std::printf("    ruh %2d -> %d kol jesti, %d tekerlekli\n", r, kol,
                    teker);
        // Kol jesti SART: tekerlek kurasi tutmadiginda ya da tekerlek
        // kipi kapaliyken yalnizca kol jestleri aranıyor.
        kontrol(kol > 0, "bu ruh halinde hic kol jesti yok");

        // 🔴 TEKERLEKLI JEST DE SART. Tekerlek turu SECILDIKTEN
        // sonra yalnizca tekerlekli jestlere bakiliyor; bu ruh halinde
        // hic yoksa ikinci tur devreye girip RUHA UYMAYAN birini
        // seciyor. Belirtisi en kotu haliyle su: uzgun bakan Pati
        // neseyle firil firil donuyor.
        kontrol(teker > 0, "bu ruh halinde hic tekerlekli jest yok");
    }
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
    kol_araligi();
    ozerk_donus();
    jest_tablosu();
    ruh_eslemesi();

    std::printf("\n  ------------------------------------------------------\n");
    if (g_hata == 0) {
        std::printf("  GECTI — %d kontrol\n\n", g_gecen);
        return 0;
    }
    std::printf("  BASARISIZ — %d hata (%d kontrol gecti)\n\n", g_hata,
                g_gecen);
    return 1;
}
