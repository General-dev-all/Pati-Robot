// -*- coding: utf-8 -*-
//
// C++ gozler, tarayicidaki gozlerle AYNI pikselleri mi uretiyor?
//
// ===========================================================================
// NEDEN BU TEST VAR
// ===========================================================================
//
// `pati_gozler.cpp` `panel/gozler240.js`'in portu. "Portladim" ile
// "dogru portladim" ayri seyler ve arasindaki fark gozle gorulmuyor:
// bir isaret hatasi (egim +/-), bir sabitte basamak kaymasi (0.018
// yerine 0.18), bir kapak hesabinda ic/dis karismasi — hepsi
// "calisiyor gibi" gorunuyor ama ifade baska bir sey oluyor.
//
// Bu test iki tarafi ayni girdiyle calistirip PIKSEL PIKSEL
// karsilastiriyor.
//
//   1. node test/goz_js_dok.mjs   -> js_kareler.ham  (tarayici motoru)
//   2. bu program                 -> cpp_kareler.ham (firmware motoru)
//   3. bu program ikisini karsilastirir
//
// ===========================================================================
// BELIRLENIMCILIK
// ===========================================================================
//
// Animasyon zamana ve rastgeleye bagli; cizim degil. O yuzden
// karsilastirma CIZIM uzerinde yapiliyor:
//
//   - durum hedefe OTURTULUYOR (yumusatma yok)   -> hedefe_otur()
//   - gecen = 0, kirp = 0, tit = 0               -> nefes/kirpma yok
//   - sakkad tamamlanmis kabul ediliyor
//
// Ayni sartlar JS tarafinda da kuruluyor (Gozler240.testKaresi).
//
// ===========================================================================
// DERLEME
// ===========================================================================
//
//   test\derle.bat        (MSVC 2019 Build Tools kullaniyor)
//
// Konak derleyicisi olmayan bir makinede bu test ATLANIR — firmware
// derlemesini engellemiyor, `main/` icinde degil.

// fopen uyarisi: bu bir test programi, guvenli surumune gecmenin
// getirisi yok ve MSVC'ye ozel fopen_s tasinabilir degil.
#define _CRT_SECURE_NO_WARNINGS

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// --- test'in surdugu saat ve rastgele (stub basliklar bunlara bakiyor)
std::uint32_t test_rastgele_deger = 0;
std::int64_t test_saat_us = 0;

// Pin/olcu sabitleri (PATI_EKR_G, PATI_EKR_Y) burada tanimli.
// pati_ekran.hpp onlari kendi ice ALMIYOR — gercek .cpp de ayrica
// dahil ediyor.
#include "../main/pati_pinler.h"
#include "../main/pati_ekran.hpp"

// ---------------------------------------------------------------------------
// TAKLIT EKRAN
// ---------------------------------------------------------------------------
//
// Gercek ekran yerine 240x240'lik bir RGB888 tampona yaziyor.
//
// ekran_renk() 565'e PAKETLIYOR (bayt cevirmeden — bu testte bayt
// sirasinin onemi yok, karsilastirilan sey renk), ekran_serit_bas()
// ise 888'e GERI aciyor. Acma formulu tarayicidaki `rgb565Uygula` ile
// birebir ayni:
//
//     R5 = r >> 3;  geri = (R5 << 3) | (R5 >> 2)  ==  (r & 0xf8) | (r >> 5)
//
// Boylece 565 bantlanmasi da karsilastirmaya giriyor. Bantlanmayi
// disarida birakmak, ekranda gorulmeyecek bir esitligi dogrulamak
// olurdu.

namespace pati {

constexpr int EKR_G = PATI_EKR_G;
constexpr int EKR_Y = PATI_EKR_Y;

std::uint8_t sahne[EKR_Y][EKR_G][3];
std::uint16_t serit_tampon[2][EKR_G * EKRAN_SERIT_YUKSEK];
int serit_sira = 0;

std::uint16_t ekran_renk(std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    return static_cast<std::uint16_t>(
        ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

std::uint16_t* ekran_serit() { return serit_tampon[serit_sira]; }

esp_err_t ekran_serit_bas(int x0, int y0, int gen, int yuk)
{
    const std::uint16_t* p = serit_tampon[serit_sira];
    for (int sy = 0; sy < yuk; ++sy) {
        for (int x = 0; x < gen; ++x) {
            const std::uint16_t v = p[sy * gen + x];
            const int r5 = (v >> 11) & 0x1f;
            const int g6 = (v >> 5) & 0x3f;
            const int b5 = v & 0x1f;
            const int ey = y0 + sy, ex = x0 + x;
            if (ey < 0 || ey >= EKR_Y || ex < 0 || ex >= EKR_G) continue;
            sahne[ey][ex][0] = static_cast<std::uint8_t>((r5 << 3) | (r5 >> 2));
            sahne[ey][ex][1] = static_cast<std::uint8_t>((g6 << 2) | (g6 >> 4));
            sahne[ey][ex][2] = static_cast<std::uint8_t>((b5 << 3) | (b5 >> 2));
        }
    }
    serit_sira ^= 1;
    return ESP_OK;
}

esp_err_t ekran_doldur(std::uint16_t)
{
    std::memset(sahne, 0, sizeof(sahne));
    return ESP_OK;
}

esp_err_t ekran_baslat() { return ESP_OK; }
void ekran_arka_isik(bool) {}
void ekran_test_deseni() {}
bool ekran_hazir() { return true; }

}  // namespace pati

// Cizim motorunun kendisi. .cpp ice aliniyor ki isimsiz uzaydaki
// `cerceve_bas` ve `hedefe_otur` gorunur olsun — test seami budur.
#include "../main/pati_gozler.cpp"

// ---------------------------------------------------------------------------

namespace {

int hata = 0;

void bak(bool kosul, const std::string& ad)
{
    std::printf(kosul ? "  OK    %s\n" : "  HATA  %s\n", ad.c_str());
    if (!kosul) ++hata;
}

// Bir durumun belirlenimci karesini cizer.
void kare_uret(const pati::GozDurumu* d)
{
    std::memset(pati::sahne, 0, sizeof(pati::sahne));
    pati::hedefe_otur(d);
    // Onceki kirli alani sifirla: her kare bagimsiz olsun.
    pati::g_onceki_kirli = pati::Dikdortgen{0, 0, 0, 0};
    pati::cerceve_bas(0.0f, 0.0f, 0.0f);
}

}  // namespace

int main(int argc, char** argv)
{
    const std::string kok = (argc > 1) ? argv[1] : ".";

    // Satir tamponu gozler_baslat() icinde ayriliyor; testte elle.
    pati::g_satir = new pati::Satir{};

    std::printf("\n=== C++ gozler <-> tarayici gozleri ===\n\n");
    std::printf("  %d durum, %dx%d, RGB565\n\n",
                pati::GOZ_DURUM_SAYISI, pati::EKR_G, pati::EKR_Y);

    // --- C++ kareleri uret ve dosyaya yaz
    const std::string cpp_yol = kok + "/cpp_kareler.ham";
    std::FILE* f = std::fopen(cpp_yol.c_str(), "wb");
    if (!f) {
        std::printf("  HATA  %s yazilamadi\n", cpp_yol.c_str());
        return 1;
    }
    for (int i = 0; i < pati::GOZ_DURUM_SAYISI; ++i) {
        kare_uret(&pati::GOZ_DURUMLARI[i]);
        std::fwrite(pati::sahne, 1, sizeof(pati::sahne), f);
    }
    std::fclose(f);
    std::printf("  yazildi: %s (%d kare)\n\n", cpp_yol.c_str(),
                pati::GOZ_DURUM_SAYISI);

    // --- tarayici kareleri
    const std::string js_yol = kok + "/js_kareler.ham";
    std::FILE* g = std::fopen(js_yol.c_str(), "rb");
    if (!g) {
        std::printf("  ! %s yok. Once sunu calistir:\n", js_yol.c_str());
        std::printf("      node test/goz_js_dok.mjs\n\n");
        return 2;
    }

    const size_t kare_bayt = sizeof(pati::sahne);
    std::vector<std::uint8_t> js(kare_bayt);
    std::FILE* c = std::fopen(cpp_yol.c_str(), "rb");

    int toplam_fark = 0;
    for (int i = 0; i < pati::GOZ_DURUM_SAYISI; ++i) {
        std::vector<std::uint8_t> cpp(kare_bayt);
        if (std::fread(js.data(), 1, kare_bayt, g) != kare_bayt ||
            std::fread(cpp.data(), 1, kare_bayt, c) != kare_bayt) {
            std::printf("  HATA  %d. kare okunamadi\n", i);
            ++hata;
            break;
        }

        // Kac piksel farkli, en buyuk kanal farki ne.
        int farkli = 0, en_buyuk = 0;
        for (size_t k = 0; k < kare_bayt; k += 3) {
            const int dr = std::abs(int(js[k]) - int(cpp[k]));
            const int dg = std::abs(int(js[k + 1]) - int(cpp[k + 1]));
            const int db = std::abs(int(js[k + 2]) - int(cpp[k + 2]));
            const int d = std::max({dr, dg, db});
            if (d > 0) ++farkli;
            en_buyuk = std::max(en_buyuk, d);
        }
        toplam_fark += farkli;

        // TOLERANS. JS double, C++ float — kenar pikselinin kapsamasi
        // son basamakta ayrilabiliyor. Kabul: farkli piksel sayisi
        // toplamin %0,5'inden az VE hicbir kanal 24'ten fazla sapmiyor.
        //
        // Bunlar keyfi degil: %0,5 = 288 piksel, yani sadece kenar
        // seridi. 24 ise 565'te bir adim (8) uc katı — bir kenar
        // pikselinin alfasi biraz farkli hesaplanirsa bu kadar oynuyor,
        // ama sekil ya da renk degisirse cok daha buyuk cikiyor.
        const int siniri = (pati::EKR_G * pati::EKR_Y) / 200;
        const bool tamam = (farkli <= siniri) && (en_buyuk <= 24);
        char ad[160];
        std::snprintf(ad, sizeof(ad),
                      "%-12s farkli piksel %5d/%d (sinir %d), en buyuk kanal "
                      "farki %d",
                      pati::GOZ_DURUMLARI[i].ad, farkli,
                      pati::EKR_G * pati::EKR_Y, siniri, en_buyuk);
        bak(tamam, ad);
    }
    std::fclose(g);
    std::fclose(c);

    std::printf("\n  toplam farkli piksel: %d\n", toplam_fark);
    std::printf("\n%s\n\n",
                hata == 0 ? "  IKI MOTOR AYNI GORUNTUYU URETIYOR"
                          : "  URETILEN GORUNTULER AYRILIYOR");
    return hata ? 1 : 0;
}
