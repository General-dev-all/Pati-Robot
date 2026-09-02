#include "pati_uyari.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "pati_ekran.hpp"
#include "pati_goz_uretilmis.h"
#include "pati_pinler.h"

namespace pati {
namespace {

// ---------------------------------------------------------------------------
// 5x7 font — yalnizca BUYUK harf, rakam ve birkac isaret
// ---------------------------------------------------------------------------
//
// NEDEN KUCUK HARF YOK: bu fontun tek isi ekrandaki kisa uyarilari
// yazmak ve hepsi buyuk harfle yaziliyor (kisik ekranda ve uzaktan
// buyuk harf daha okunur). Kucuk harf tabloyu iki katina cikarirdi,
// karsiliginda kullanilmayacak 26 glif.
//
// Her glif 5 SUTUN, sutun basina bir bayt, bitin 0'i EN UST satir.
//
// 🔴 TURKCE HARFLER GOVDEDEN AYRI. 5x7'lik kutuya hem harf hem aksan
// sigmiyor — S'nin altina kuyruk, O'nun ustune iki nokta koyacak yer
// yok. Cozum: govde ayni kaliyor, aksan AYRI KATMAN olarak govdenin
// ustune/altina ciziliyor. Tabloda "S" ile "S" ayni bes bayti
// paylasiyor, tek fark aksan alani.
enum Aksan : std::uint8_t {
    AKSAN_YOK = 0,
    AKSAN_KUYRUK,      // C, S  — govdenin ALTINDA
    AKSAN_IKI_NOKTA,   // O, U  — govdenin USTUNDE
    AKSAN_KAVIS,       // G     — govdenin USTUNDE
    AKSAN_NOKTA,       // I     — govdenin USTUNDE
};

struct Glif {
    std::uint16_t kod;          // Unicode kod noktasi
    std::uint8_t  sutun[5];
    std::uint8_t  aksan;
};

constexpr Glif FONT[] = {
    {0x0020, {0x00, 0x00, 0x00, 0x00, 0x00}, AKSAN_YOK},   // bosluk
    {0x0021, {0x00, 0x00, 0x5F, 0x00, 0x00}, AKSAN_YOK},   // !
    {0x0025, {0x23, 0x13, 0x08, 0x64, 0x62}, AKSAN_YOK},   // %
    {0x0030, {0x3E, 0x51, 0x49, 0x45, 0x3E}, AKSAN_YOK},   // 0
    {0x0031, {0x00, 0x42, 0x7F, 0x40, 0x00}, AKSAN_YOK},   // 1
    {0x0032, {0x42, 0x61, 0x51, 0x49, 0x46}, AKSAN_YOK},   // 2
    {0x0033, {0x21, 0x41, 0x45, 0x4B, 0x31}, AKSAN_YOK},   // 3
    {0x0034, {0x18, 0x14, 0x12, 0x7F, 0x10}, AKSAN_YOK},   // 4
    {0x0035, {0x27, 0x45, 0x45, 0x45, 0x39}, AKSAN_YOK},   // 5
    {0x0036, {0x3C, 0x4A, 0x49, 0x49, 0x30}, AKSAN_YOK},   // 6
    {0x0037, {0x01, 0x71, 0x09, 0x05, 0x03}, AKSAN_YOK},   // 7
    {0x0038, {0x36, 0x49, 0x49, 0x49, 0x36}, AKSAN_YOK},   // 8
    {0x0039, {0x06, 0x49, 0x49, 0x29, 0x1E}, AKSAN_YOK},   // 9
    {0x0041, {0x7E, 0x11, 0x11, 0x11, 0x7E}, AKSAN_YOK},   // A
    {0x0042, {0x7F, 0x49, 0x49, 0x49, 0x36}, AKSAN_YOK},   // B
    {0x0043, {0x3E, 0x41, 0x41, 0x41, 0x22}, AKSAN_YOK},   // C
    {0x0044, {0x7F, 0x41, 0x41, 0x22, 0x1C}, AKSAN_YOK},   // D
    {0x0045, {0x7F, 0x49, 0x49, 0x49, 0x41}, AKSAN_YOK},   // E
    {0x0046, {0x7F, 0x09, 0x09, 0x09, 0x01}, AKSAN_YOK},   // F
    {0x0047, {0x3E, 0x41, 0x49, 0x49, 0x7A}, AKSAN_YOK},   // G
    {0x0048, {0x7F, 0x08, 0x08, 0x08, 0x7F}, AKSAN_YOK},   // H
    {0x0049, {0x00, 0x41, 0x7F, 0x41, 0x00}, AKSAN_YOK},   // I
    {0x004A, {0x20, 0x40, 0x41, 0x3F, 0x01}, AKSAN_YOK},   // J
    {0x004B, {0x7F, 0x08, 0x14, 0x22, 0x41}, AKSAN_YOK},   // K
    {0x004C, {0x7F, 0x40, 0x40, 0x40, 0x40}, AKSAN_YOK},   // L
    {0x004D, {0x7F, 0x02, 0x0C, 0x02, 0x7F}, AKSAN_YOK},   // M
    {0x004E, {0x7F, 0x04, 0x08, 0x10, 0x7F}, AKSAN_YOK},   // N
    {0x004F, {0x3E, 0x41, 0x41, 0x41, 0x3E}, AKSAN_YOK},   // O
    {0x0050, {0x7F, 0x09, 0x09, 0x09, 0x06}, AKSAN_YOK},   // P
    {0x0052, {0x7F, 0x09, 0x19, 0x29, 0x46}, AKSAN_YOK},   // R
    {0x0053, {0x46, 0x49, 0x49, 0x49, 0x31}, AKSAN_YOK},   // S
    {0x0054, {0x01, 0x01, 0x7F, 0x01, 0x01}, AKSAN_YOK},   // T
    {0x0055, {0x3F, 0x40, 0x40, 0x40, 0x3F}, AKSAN_YOK},   // U
    {0x0056, {0x1F, 0x20, 0x40, 0x20, 0x1F}, AKSAN_YOK},   // V
    {0x0059, {0x07, 0x08, 0x70, 0x08, 0x07}, AKSAN_YOK},   // Y
    {0x005A, {0x61, 0x51, 0x49, 0x45, 0x43}, AKSAN_YOK},   // Z
    // Turkce — govde yukaridakilerin AYNISI, fark yalnizca aksan.
    {0x00C7, {0x3E, 0x41, 0x41, 0x41, 0x22}, AKSAN_KUYRUK},     // C cedilla
    {0x00D6, {0x3E, 0x41, 0x41, 0x41, 0x3E}, AKSAN_IKI_NOKTA},  // O umlaut
    {0x00DC, {0x3F, 0x40, 0x40, 0x40, 0x3F}, AKSAN_IKI_NOKTA},  // U umlaut
    {0x011E, {0x3E, 0x41, 0x49, 0x49, 0x7A}, AKSAN_KAVIS},      // G breve
    {0x0130, {0x00, 0x41, 0x7F, 0x41, 0x00}, AKSAN_NOKTA},      // I noktali
    {0x015E, {0x46, 0x49, 0x49, 0x49, 0x31}, AKSAN_KUYRUK},     // S cedilla
};
constexpr int FONT_SAYISI = sizeof(FONT) / sizeof(FONT[0]);

constexpr int GLIF_GEN = 5;
constexpr int GLIF_YUK = 7;
constexpr int GLIF_ARA = 1;    // olcekten ONCE, karakterler arasi bosluk

const Glif* glif_bul(std::uint16_t kod)
{
    for (int i = 0; i < FONT_SAYISI; ++i) {
        if (FONT[i].kod == kod) return &FONT[i];
    }
    return nullptr;
}

// UTF-8'den kod noktasi. Yalnizca 1 ve 2 baytlik diziler cozuluyor:
// fontta bunun disinda karakter yok (Turkce harflerin hepsi 2 bayt).
// Uzun bir dizi gelirse bosluga dusuyor, cunku zaten cizilemezdi.
std::uint16_t sonraki_kod(const char*& p)
{
    const auto b0 = static_cast<std::uint8_t>(*p);
    if (b0 < 0x80) {
        ++p;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0
        && (static_cast<std::uint8_t>(p[1]) & 0xC0) == 0x80) {
        const auto k = static_cast<std::uint16_t>(
            ((b0 & 0x1F) << 6) | (static_cast<std::uint8_t>(p[1]) & 0x3F));
        p += 2;
        return k;
    }
    ++p;
    return 0x0020;
}

// ---------------------------------------------------------------------------
// Serit tamponuna cizim
// ---------------------------------------------------------------------------
//
// Butun cizim fonksiyonlari EKRAN koordinati aliyor ve serit disina
// dusen kismi kendisi kirpiyor. Boylece cagiran taraf hangi seritte
// oldugunu hic dusunmuyor: ayni cizim listesi her serit icin oldugu
// gibi tekrar cagriliyor. Alti serit x birkac dikdortgen — bu boyutta
// tekrar hesaplamak, serit basina hangi ogenin dustugunu kitaplamaktan
// hem ucuz hem okunakli.

std::uint16_t* g_serit = nullptr;
int g_serit_y0 = 0;
int g_serit_yuk = 0;

void kutu(int x, int y, int gen, int yuk, std::uint16_t renk)
{
    // Serit disindaysa hic dongu kurma.
    if (y + yuk <= g_serit_y0 || y >= g_serit_y0 + g_serit_yuk) return;

    const int ybas = std::max(y, g_serit_y0);
    const int yson = std::min(y + yuk, g_serit_y0 + g_serit_yuk);
    const int xbas = std::max(x, 0);
    const int xson = std::min(x + gen, PATI_EKR_G);

    for (int j = ybas; j < yson; ++j) {
        std::uint16_t* satir =
            g_serit + static_cast<std::size_t>(j - g_serit_y0) * PATI_EKR_G;
        for (int i = xbas; i < xson; ++i) satir[i] = renk;
    }
}

// Ici bos dikdortgen — pil sembolunun govdesi.
void cerceve(int x, int y, int gen, int yuk, int kalinlik, std::uint16_t renk)
{
    kutu(x, y, gen, kalinlik, renk);
    kutu(x, y + yuk - kalinlik, gen, kalinlik, renk);
    kutu(x, y, kalinlik, yuk, renk);
    kutu(x + gen - kalinlik, y, kalinlik, yuk, renk);
}

// Bir glifi (x, y) sol-ust kosesine, `olcek` katinda cizer.
// y GOVDENIN ustu; aksan bunun disina tasiyor.
void glif_ciz(int x, int y, const Glif& g, int olcek, std::uint16_t renk)
{
    for (int s = 0; s < GLIF_GEN; ++s) {
        const std::uint8_t sut = g.sutun[s];
        for (int b = 0; b < GLIF_YUK; ++b) {
            if ((sut & (1u << b)) == 0) continue;
            kutu(x + s * olcek, y + b * olcek, olcek, olcek, renk);
        }
    }

    switch (g.aksan) {
        case AKSAN_KUYRUK:
            // Kisa bir kuyruk yeter: bu boyutta cengel sekli okunmuyor,
            // leke gibi gorunuyor.
            kutu(x + 2 * olcek, y + GLIF_YUK * olcek, olcek, olcek * 2, renk);
            break;
        case AKSAN_IKI_NOKTA:
            kutu(x + olcek, y - olcek * 2, olcek, olcek, renk);
            kutu(x + 3 * olcek, y - olcek * 2, olcek, olcek, renk);
            break;
        case AKSAN_KAVIS:
            kutu(x + olcek, y - olcek * 2, olcek * 3, olcek, renk);
            kutu(x + olcek, y - olcek, olcek, olcek, renk);
            kutu(x + 3 * olcek, y - olcek, olcek, olcek, renk);
            break;
        case AKSAN_NOKTA:
            kutu(x + 2 * olcek, y - olcek * 2, olcek, olcek, renk);
            break;
        default:
            break;
    }
}

int metin_genislik(const char* s, int olcek)
{
    int gen = 0;
    for (const char* p = s; *p != '\0';) {
        sonraki_kod(p);
        gen += (GLIF_GEN + GLIF_ARA) * olcek;
    }
    return gen > 0 ? gen - GLIF_ARA * olcek : 0;
}

// Ekranin yatayinda ortalanmis metin.
void metin_orta(int y, const char* s, int olcek, std::uint16_t renk)
{
    int x = (PATI_EKR_G - metin_genislik(s, olcek)) / 2;
    for (const char* p = s; *p != '\0';) {
        const std::uint16_t kod = sonraki_kod(p);
        const Glif* g = glif_bul(kod);
        if (g != nullptr) glif_ciz(x, y, *g, olcek, renk);
        x += (GLIF_GEN + GLIF_ARA) * olcek;
    }
}

// ---------------------------------------------------------------------------
// Yerlesim — 240 x 135
// ---------------------------------------------------------------------------
//
// Sayilar elle secildi, olcut uc tane: sembol uzaktan taninsin, yazi
// kisik ekranda (pilde parlaklik 0,35-0,45) okunsun, hicbir sey disari
// tasmasin. Yazinin kuyruklu harfi (S) govdenin 2 olcek altina
// iniyor — YAZI_Y secilirken bu paya dikkat.

constexpr int PIL_X   = 70;
constexpr int PIL_Y   = 16;
constexpr int PIL_GEN = 100;
constexpr int PIL_YUK = 46;
constexpr int PIL_KAL = 4;     // cerceve kalinligi
constexpr int UC_GEN  = 7;     // pilin arti ucu
constexpr int UC_YUK  = 20;

constexpr int YUZDE_Y = 72;
constexpr int YUZDE_OLCEK = 2;

constexpr int YAZI_Y = 98;     // govde 98..118, kuyruk 119..124
constexpr int YAZI_OLCEK = 3;

}  // namespace

esp_err_t uyari_pil_ciz(int yuzde)
{
    if (!ekran_hazir()) return ESP_ERR_INVALID_STATE;

    const std::uint16_t zemin = ekran_renk(0, 0, 0);

    // 🔴 RENK GOZLERDEN GELIYOR, elle yazilmiyor.
    //
    // Ilk surumde amber (255,138,30) denendi — gerekcesi "uyari, gozden
    // ayirt edilsin" idi. Ekranda gorulunce yanlis oldugu anlasildi:
    // turuncu kutu Pati'ye ait gorunmuyordu, baska bir cihazin hata
    // ekrani gibi duruyordu. Pati'nin kimligi bu turkuaz.
    //
    // GOZ_ACIK / GOZ_KOYU'dan okunmasinin sebebi tek kaynak: goz rengi
    // gozler240.js'ten uretiliyor ve bir gun degisirse uyari da
    // kendiliginde degisiyor. Buraya sayi yazmak iki tarafi sessizce
    // ayirirdi.
    const std::uint16_t vurgu = ekran_renk(GOZ_ACIK.r, GOZ_ACIK.g, GOZ_ACIK.b);
    const std::uint16_t soluk = ekran_renk(GOZ_KOYU.r, GOZ_KOYU.g, GOZ_KOYU.b);

    // 0-100'e sikistiriliyor. Cagiran taraf zaten bu araligi veriyor ama
    // derleyici bunu BILMIYOR: sikistirmadan snprintf "%d 10 haneye
    // kadar yazabilir" deyip -Werror=format-truncation ile duruyor.
    // Ustelik sinir cizimi de dogruluyor.
    const int y = (yuzde < 0) ? -1 : std::min(yuzde, 100);

    // Dolgu kalan yuzdeyi gosteriyor. Yuzde bilinmiyorsa cerceve bos
    // kaliyor: yanlis bir dolulukta cizmektense hic cizmemek dogru.
    const int ic_gen = PIL_GEN - PIL_KAL * 2 - 4;
    const int dolgu = (y > 0) ? (ic_gen * y) / 100 : 0;

    char yuzde_metni[8] = {};
    if (y >= 0) {
        std::snprintf(yuzde_metni, sizeof(yuzde_metni), "%%%d", y);
    }

    for (int y = 0; y < PATI_EKR_Y; y += EKRAN_SERIT_YUKSEK) {
        const int yuk = std::min(EKRAN_SERIT_YUKSEK, PATI_EKR_Y - y);
        g_serit = ekran_serit();
        g_serit_y0 = y;
        g_serit_yuk = yuk;

        // Zemin. Gozlerden kalan pikselleri de bu siliyor — uyaridan
        // sonra gozlerin tam yeniden cizilmesi gerekmesinin sebebi bu.
        for (int i = 0; i < PATI_EKR_G * yuk; ++i) g_serit[i] = zemin;

        cerceve(PIL_X, PIL_Y, PIL_GEN, PIL_YUK, PIL_KAL, vurgu);
        kutu(PIL_X + PIL_GEN, PIL_Y + (PIL_YUK - UC_YUK) / 2,
             UC_GEN, UC_YUK, vurgu);
        if (dolgu > 0) {
            kutu(PIL_X + PIL_KAL + 2, PIL_Y + PIL_KAL + 2,
                 dolgu, PIL_YUK - PIL_KAL * 2 - 4, vurgu);
        }

        if (yuzde_metni[0] != '\0') {
            metin_orta(YUZDE_Y, yuzde_metni, YUZDE_OLCEK, soluk);
        }
        // "SARJA TAK" — bastaki S, U+015E (UTF-8: C5 9E).
        //
        // ⚠️ KACIS AYRI DIZGIDE OLMAK ZORUNDA. "\x9EARJA" yazilsaydi
        // derleyici hex kacisini ACGOZLU okur, "9EA"yi tek sayi sanip
        // tasma hatasi verirdi. Bitisik yazilan iki dizgi zaten tek
        // dizgi olarak birlesiyor.
        metin_orta(YAZI_Y, "\xC5\x9E" "ARJA TAK", YAZI_OLCEK, vurgu);

        const esp_err_t hata = ekran_serit_bas(0, y, PATI_EKR_G, yuk);
        if (hata != ESP_OK) return hata;
    }

    return ESP_OK;
}

}  // namespace pati
