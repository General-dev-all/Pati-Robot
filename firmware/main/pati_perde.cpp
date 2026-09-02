#include "pati_perde.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "pati_ag.hpp"
#include "pati_ekran.hpp"
#include "pati_goz_uretilmis.h"
#include "pati_guc.hpp"
#include "pati_guncelleme.hpp"
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
    {0x0051, {0x3E, 0x41, 0x51, 0x21, 0x5E}, AKSAN_YOK},   // Q
    {0x0057, {0x3F, 0x40, 0x38, 0x40, 0x3F}, AKSAN_YOK},   // W
    {0x0058, {0x63, 0x14, 0x08, 0x14, 0x63}, AKSAN_YOK},   // X
    {0x0059, {0x07, 0x08, 0x70, 0x08, 0x07}, AKSAN_YOK},   // Y
    {0x005A, {0x61, 0x51, 0x49, 0x45, 0x43}, AKSAN_YOK},   // Z
    // Isaretler — wifi adlarinda sik gecenler.
    {0x002D, {0x08, 0x08, 0x08, 0x08, 0x08}, AKSAN_YOK},   // -
    {0x005F, {0x40, 0x40, 0x40, 0x40, 0x40}, AKSAN_YOK},   // _
    {0x002E, {0x00, 0x60, 0x60, 0x00, 0x00}, AKSAN_YOK},   // .
    {0x003A, {0x00, 0x36, 0x36, 0x00, 0x00}, AKSAN_YOK},   // :
    {0x002F, {0x20, 0x10, 0x08, 0x04, 0x02}, AKSAN_YOK},   // /
    // ---- KUCUK HARFLER ----
    //
    // NEDEN EKLENDI: bilgi sayfasi WIFI ADINI yaziyor ve ag adlari
    // buyuk harfe cevrilirse yanlis okunur — cocuk ekranda gordugu adi
    // evdeki agla eslestirebilmeli. "HizliVeKotali-v2" ile
    // "HIZLIVEKOTALI-V2" ayni sey degil.
    {0x0061, {0x20, 0x54, 0x54, 0x54, 0x78}, AKSAN_YOK},   // a
    {0x0062, {0x7F, 0x48, 0x44, 0x44, 0x38}, AKSAN_YOK},   // b
    {0x0063, {0x38, 0x44, 0x44, 0x44, 0x20}, AKSAN_YOK},   // c
    {0x0064, {0x38, 0x44, 0x44, 0x48, 0x7F}, AKSAN_YOK},   // d
    {0x0065, {0x38, 0x54, 0x54, 0x54, 0x18}, AKSAN_YOK},   // e
    {0x0066, {0x08, 0x7E, 0x09, 0x01, 0x02}, AKSAN_YOK},   // f
    {0x0067, {0x0C, 0x52, 0x52, 0x52, 0x3E}, AKSAN_YOK},   // g
    {0x0068, {0x7F, 0x08, 0x04, 0x04, 0x78}, AKSAN_YOK},   // h
    {0x0069, {0x00, 0x44, 0x7D, 0x40, 0x00}, AKSAN_YOK},   // i
    {0x006A, {0x20, 0x40, 0x44, 0x3D, 0x00}, AKSAN_YOK},   // j
    {0x006B, {0x7F, 0x10, 0x28, 0x44, 0x00}, AKSAN_YOK},   // k
    {0x006C, {0x00, 0x41, 0x7F, 0x40, 0x00}, AKSAN_YOK},   // l
    {0x006D, {0x7C, 0x04, 0x18, 0x04, 0x78}, AKSAN_YOK},   // m
    {0x006E, {0x7C, 0x08, 0x04, 0x04, 0x78}, AKSAN_YOK},   // n
    {0x006F, {0x38, 0x44, 0x44, 0x44, 0x38}, AKSAN_YOK},   // o
    {0x0070, {0x7C, 0x14, 0x14, 0x14, 0x08}, AKSAN_YOK},   // p
    {0x0071, {0x08, 0x14, 0x14, 0x18, 0x7C}, AKSAN_YOK},   // q
    {0x0072, {0x7C, 0x08, 0x04, 0x04, 0x08}, AKSAN_YOK},   // r
    {0x0073, {0x48, 0x54, 0x54, 0x54, 0x20}, AKSAN_YOK},   // s
    {0x0074, {0x04, 0x3F, 0x44, 0x40, 0x20}, AKSAN_YOK},   // t
    {0x0075, {0x3C, 0x40, 0x40, 0x20, 0x7C}, AKSAN_YOK},   // u
    {0x0076, {0x1C, 0x20, 0x40, 0x20, 0x1C}, AKSAN_YOK},   // v
    {0x0077, {0x3C, 0x40, 0x30, 0x40, 0x3C}, AKSAN_YOK},   // w
    {0x0078, {0x44, 0x28, 0x10, 0x28, 0x44}, AKSAN_YOK},   // x
    {0x0079, {0x0C, 0x50, 0x50, 0x50, 0x3C}, AKSAN_YOK},   // y
    {0x007A, {0x44, 0x64, 0x54, 0x4C, 0x44}, AKSAN_YOK},   // z
    // Turkce kucukler. 'i' ile 'i' (noktasiz) AYRI HARF — ag adinda
    // gecerse ikisini karistirmak adi yanlis gosterir.
    {0x00E7, {0x38, 0x44, 0x44, 0x44, 0x20}, AKSAN_KUYRUK},     // c cedilla
    {0x00F6, {0x38, 0x44, 0x44, 0x44, 0x38}, AKSAN_IKI_NOKTA},  // o umlaut
    {0x00FC, {0x3C, 0x40, 0x40, 0x20, 0x7C}, AKSAN_IKI_NOKTA},  // u umlaut
    {0x011F, {0x0C, 0x52, 0x52, 0x52, 0x3E}, AKSAN_KAVIS},      // g breve
    {0x0131, {0x00, 0x44, 0x7C, 0x40, 0x00}, AKSAN_YOK},        // i noktasiz
    {0x015F, {0x48, 0x54, 0x54, 0x54, 0x20}, AKSAN_KUYRUK},     // s cedilla
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

// ---------------------------------------------------------------------------
// 🔴 SERITLER ARASINDA NEFES — bu satir bir OLCUMDEN geliyor
// ---------------------------------------------------------------------------
//
// 02.09.2026, 2. kart, USB'ye TAKILIYKEN ve pil DOLUYKEN uc brownout:
//
//     20:25:45  cokme=3  acilis=brownout  4058 mV  kaynak=usb
//     20:26:10  cokme=4  acilis=brownout  4060 mV  kaynak=usb
//     20:28:08  cokme=5  acilis=brownout  4070 mV  kaynak=usb
//
// Ucu de, uyarinin 15 saniyede bir cizildigi 2,5 dakikalik test
// yapisinda oldu. Uyari USB'de kapatilinca durdu.
//
// USB'de brownout olmasi tuhaf gorunuyor ama brownout dedektoru 5 V
// girisini degil YONGANIN 3,3 V RAYINI izliyor, ve o ray M5PM1'in
// LDO'sundan geliyor. Tam ekran perde 240x135x2 = 65 KB'yi SPI'ye
// KESINTISIZ basiyor: DMA surekli, PSRAM erisimi surekli, arada
// hesap yok. Goz cizimi ayni isi yapmiyor — kucuk bir dikdortgen ve
// serit aralarinda rasterleme hesabi var, yani akim yayiliyor.
//
// Bir tik (10 ms) beklemek tepeyi boluyor. Alti serit icin 60 ms
// ekliyor; uyari zaten 3 saniye duruyor, bilgi sayfasi 2 saniyede bir
// yenileniyor — ikisinde de gorunur bir bedeli yok.
//
// ⚠️ OLCULECEK: bu nefes brownout'lari durduruyor mu. Durdurmuyorsa
// sebep SPI dolulugu degildir ve baska yere bakilmali.
esp_err_t serit_bas_ve_nefes(int sy, int yuk)
{
    const esp_err_t hata = ekran_serit_bas(0, sy, PATI_EKR_G, yuk);
    if (hata != ESP_OK) return hata;
    vTaskDelay(1);
    return ESP_OK;
}

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

// Serit icinde tek piksel. Yay ve daire cizimi bunu cok cagiriyor, o
// yuzden kutu() gibi genel degil: sinir kontrolu cagiranin isi.
inline void nokta_hizli(int x, int y, std::uint16_t renk)
{
    if (x < 0 || x >= PATI_EKR_G) return;
    const int sy = y - g_serit_y0;
    if (sy < 0 || sy >= g_serit_yuk) return;
    g_serit[static_cast<std::size_t>(sy) * PATI_EKR_G + x] = renk;
}

// Dolu daire — wifi sembolunun altindaki nokta.
void daire(int cx, int cy, int r, std::uint16_t renk)
{
    const int r2 = r * r;
    const int ybas = std::max(cy - r, g_serit_y0);
    const int yson = std::min(cy + r + 1, g_serit_y0 + g_serit_yuk);
    for (int y = ybas; y < yson; ++y) {
        const int dy = y - cy;
        for (int x = cx - r; x <= cx + r; ++x) {
            const int dx = x - cx;
            if (dx * dx + dy * dy <= r2) nokta_hizli(x, y, renk);
        }
    }
}

// Wifi yayi: merkezden YUKARI dogru, 90 derecelik bir dilim.
//
// Gercek bir yay cizmek yerine iki kosul yetiyor: nokta halkanin
// icinde mi (r1 <= uzaklik <= r2) ve 45 derecelik koninin icinde mi
// (|dx| <= dy). Ikisi birlesince wifi sembolunun tanidik dilimi
// cikiyor. Bu boyutta kenar yumusatmaya gerek yok — yaylar kalin.
void yay(int cx, int cy, int r_ic, int r_dis, std::uint16_t renk)
{
    const int i2 = r_ic * r_ic;
    const int d2 = r_dis * r_dis;
    const int ybas = std::max(cy - r_dis, g_serit_y0);
    const int yson = std::min(cy, g_serit_y0 + g_serit_yuk);
    for (int y = ybas; y < yson; ++y) {
        const int dy = cy - y;              // pozitif: merkezin ustundeyiz
        for (int x = cx - r_dis; x <= cx + r_dis; ++x) {
            const int dx = x - cx;
            if (std::abs(dx) > dy) continue;         // 45 derece koni
            const int u = dx * dx + dy * dy;
            if (u < i2 || u > d2) continue;          // halka
            nokta_hizli(x, y, renk);
        }
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

// ---------------------------------------------------------------------------
// Bilgi sayfasi yerlesimi
// ---------------------------------------------------------------------------
//
// Ust yari pil, alt yari wifi. Aradaki ince cizgi ikisini ayiriyor —
// olmadan sayfa tek bir yigin gibi okunuyordu.

constexpr int B_PIL_X   = 22;
constexpr int B_PIL_Y   = 16;
constexpr int B_PIL_GEN = 66;
constexpr int B_PIL_YUK = 30;
constexpr int B_PIL_KAL = 3;
constexpr int B_UC_GEN  = 5;
constexpr int B_UC_YUK  = 14;

constexpr int B_YUZDE_X = 108;   // pil sembolunun sagi
constexpr int B_YUZDE_Y = 17;
constexpr int B_YUZDE_OLCEK = 3;

constexpr int B_AYIRAC_Y = 56;

// ⚠️ BU IKI SAYI BIRBIRINE BAGLI — degistirirken ikisine birden bak.
//
// Cubuklar TABANDAN yukari buyuyor, yani en yuksek cubugun tepesi
// B_CUBUK_Y - (8 + 3*6) = B_CUBUK_Y - 26. Wifi adinin govdesi ise
// B_AD_Y'den B_AD_Y + 7*olcek'e kadar iniyor.
//
// Ilk surumde ad 72, cubuk tabani 98 yazilmisti: en yuksek cubugun
// tepesi tam 72 cikti ve cubuk yazinin uzerine bindi. Ekranda goruldu.
//
//   ad govdesi   : 66 .. 80   (olcek 2)
//   cubuk tepeler: 116, 110, 104, 98
//   aradaki bosluk: 18 piksel
constexpr int B_AD_Y = 66;       // wifi adi — govdenin USTU
constexpr int B_CUBUK_Y = 124;   // sinyal cubuklarinin TABANI
constexpr int B_CUBUK_GEN = 12;
constexpr int B_CUBUK_ARA = 6;
constexpr int B_CUBUK_SAYI = 4;

// Metni verilen x'ten baslayarak yazar (ortalamadan).
void metin_yaz(int x, int y, const char* s, int olcek, std::uint16_t renk)
{
    for (const char* p = s; *p != ' ';) {
        const std::uint16_t kod = sonraki_kod(p);
        const Glif* g = glif_bul(kod);
        if (g != nullptr) glif_ciz(x, y, *g, olcek, renk);
        x += (GLIF_GEN + GLIF_ARA) * olcek;
    }
}

}  // namespace

esp_err_t perde_pil_uyarisi(int yuzde)
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

        const esp_err_t hata = serit_bas_ve_nefes(y, yuk);
        if (hata != ESP_OK) return hata;
    }

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Bilgi sayfasi — cocuk tusa basinca
// ---------------------------------------------------------------------------

esp_err_t perde_bilgi()
{
    if (!ekran_hazir()) return ESP_ERR_INVALID_STATE;

    const std::uint16_t zemin = ekran_renk(0, 0, 0);
    const std::uint16_t vurgu = ekran_renk(GOZ_ACIK.r, GOZ_ACIK.g, GOZ_ACIK.b);
    const std::uint16_t soluk = ekran_renk(GOZ_KOYU.r, GOZ_KOYU.g, GOZ_KOYU.b);
    // Bos sinyal cubugu ve ayirac cizgisi. Turkuazin cok koyusu —
    // "yok" demek icin ayri bir renk kullanmiyoruz, ayni rengin
    // sonugu yetiyor ve sayfa tek renkli kaliyor.
    const std::uint16_t golge = ekran_renk(10, 60, 60);

    // Veriyi BIR KEZ okuyup sakliyoruz: alti seridin her birinde
    // yeniden sorulsa pil yuzdesi serit ortasinda degisip sayfayi
    // kendi icinde tutarsiz birakabilirdi.
    const int yuzde = pil_yuzde();
    const bool bagli = (ag_durumu() == AgDurumu::Bagli);
    const char* ad = ag_adi();
    const int guc = ag_gucu();

    const int ic_gen = B_PIL_GEN - B_PIL_KAL * 2 - 4;
    const int y = (yuzde < 0) ? -1 : std::min(yuzde, 100);
    const int dolgu = (y > 0) ? (ic_gen * y) / 100 : 0;

    char yuzde_metni[8] = {};
    std::snprintf(yuzde_metni, sizeof(yuzde_metni),
                  (y >= 0) ? "%%%d" : "%%--", y);

    // Wifi adi sigmiyorsa once olcek 1'e dusuyoruz, o da yetmezse
    // kirpiliyor. Kirpmak son care: cocuk adi evdeki agla
    // eslestirebilmeli, yarim ad bunun icin yeterli olmayabilir.
    char ad_metni[40] = {};
    if (bagli && ad != nullptr && ad[0] != ' ') {
        std::snprintf(ad_metni, sizeof(ad_metni), "%s", ad);
    } else {
        std::snprintf(ad_metni, sizeof(ad_metni), "%s", "WiFi yok");
    }
    int ad_olcek = 2;
    if (metin_genislik(ad_metni, ad_olcek) > PATI_EKR_G - 8) ad_olcek = 1;

    const int cubuk_toplam =
        B_CUBUK_SAYI * B_CUBUK_GEN + (B_CUBUK_SAYI - 1) * B_CUBUK_ARA;
    const int cubuk_x0 = (PATI_EKR_G - cubuk_toplam) / 2;

    for (int sy = 0; sy < PATI_EKR_Y; sy += EKRAN_SERIT_YUKSEK) {
        const int yuk = std::min(EKRAN_SERIT_YUKSEK, PATI_EKR_Y - sy);
        g_serit = ekran_serit();
        g_serit_y0 = sy;
        g_serit_yuk = yuk;

        for (int i = 0; i < PATI_EKR_G * yuk; ++i) g_serit[i] = zemin;

        // ---- pil ----
        cerceve(B_PIL_X, B_PIL_Y, B_PIL_GEN, B_PIL_YUK, B_PIL_KAL, vurgu);
        kutu(B_PIL_X + B_PIL_GEN, B_PIL_Y + (B_PIL_YUK - B_UC_YUK) / 2,
             B_UC_GEN, B_UC_YUK, vurgu);
        if (dolgu > 0) {
            kutu(B_PIL_X + B_PIL_KAL + 2, B_PIL_Y + B_PIL_KAL + 2,
                 dolgu, B_PIL_YUK - B_PIL_KAL * 2 - 4, vurgu);
        }
        metin_yaz(B_YUZDE_X, B_YUZDE_Y, yuzde_metni, B_YUZDE_OLCEK, vurgu);

        // ---- ayirac ----
        kutu(20, B_AYIRAC_Y, PATI_EKR_G - 40, 1, golge);

        // ---- wifi ----
        metin_orta(B_AD_Y, ad_metni, ad_olcek, bagli ? vurgu : golge);

        // Sinyal cubuklari: soldan saga yukselen dort cubuk. Dolu
        // olanlar parlak, kalanlar sonuk — "3/4" gibi bir sayi yerine
        // cubuk, cunku cocuk icin sayinin anlami yok.
        for (int i = 0; i < B_CUBUK_SAYI; ++i) {
            const int c_yuk = 8 + i * 6;
            const bool dolu = bagli && (i < guc);
            kutu(cubuk_x0 + i * (B_CUBUK_GEN + B_CUBUK_ARA),
                 B_CUBUK_Y - c_yuk, B_CUBUK_GEN, c_yuk,
                 dolu ? vurgu : golge);
        }

        const esp_err_t hata = serit_bas_ve_nefes(sy, yuk);
        if (hata != ESP_OK) return hata;
    }

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// WiFi araniyor
// ---------------------------------------------------------------------------
//
// Yerlesim: merkez alt ortada, uzerinde uc yay. En dis yayin tepesi
// y = 96 - 62 = 34, yani ustte pay var; altta yazi icin 39 piksel
// kaliyor.

esp_err_t perde_wifi(int faz)
{
    if (!ekran_hazir()) return ESP_ERR_INVALID_STATE;

    const std::uint16_t zemin = ekran_renk(0, 0, 0);
    const std::uint16_t vurgu = ekran_renk(GOZ_ACIK.r, GOZ_ACIK.g, GOZ_ACIK.b);
    const std::uint16_t golge = ekran_renk(10, 60, 60);

    constexpr int CX = 120;
    constexpr int CY = 96;

    // Dalga YUKARIDAN ASAGI iniyor: once en genis yay parliyor, sonra
    // ortadaki, sonra dar olan, en son ortadaki nokta. Yani "sinyal
    // disaridan geliyor ve Pati'ye ulasmaya calisiyor" okunuyor —
    // asagidan yukari olsaydi "Pati yayin yapiyor" gibi gorunurdu.
    const int p = ((faz % 4) + 4) % 4;

    for (int y = 0; y < PATI_EKR_Y; y += EKRAN_SERIT_YUKSEK) {
        const int yuk = std::min(EKRAN_SERIT_YUKSEK, PATI_EKR_Y - y);
        g_serit = ekran_serit();
        g_serit_y0 = y;
        g_serit_yuk = yuk;

        for (int i = 0; i < PATI_EKR_G * yuk; ++i) g_serit[i] = zemin;

        yay(CX, CY, 54, 62, (p == 0) ? vurgu : golge);
        yay(CX, CY, 36, 44, (p == 1) ? vurgu : golge);
        yay(CX, CY, 18, 26, (p == 2) ? vurgu : golge);
        daire(CX, CY, 7,   (p == 3) ? vurgu : golge);

        metin_orta(108, "WiFi aranıyor", 2, vurgu);

        const esp_err_t hata = serit_bas_ve_nefes(y, yuk);
        if (hata != ESP_OK) return hata;
    }

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Guncelleme
// ---------------------------------------------------------------------------

esp_err_t perde_guncelleme(const char* surum, int yuzde, const char* uyari)
{
    if (!ekran_hazir()) return ESP_ERR_INVALID_STATE;

    const std::uint16_t zemin = ekran_renk(0, 0, 0);
    const std::uint16_t vurgu = ekran_renk(GOZ_ACIK.r, GOZ_ACIK.g, GOZ_ACIK.b);
    const std::uint16_t golge = ekran_renk(10, 60, 60);

    const bool iniyor = (yuzde >= 0);
    const int y0 = (yuzde < 0) ? 0 : std::min(yuzde, 100);

    char surum_metni[24] = {};
    std::snprintf(surum_metni, sizeof(surum_metni), "%s",
                  (surum != nullptr && surum[0] != ' ') ? surum : "");

    char yuzde_metni[8] = {};
    std::snprintf(yuzde_metni, sizeof(yuzde_metni), "%%%d", y0);

    // Ilerleme cubugu
    constexpr int CUB_X = 30;
    constexpr int CUB_Y = 88;
    constexpr int CUB_GEN = 180;
    constexpr int CUB_YUK = 16;

    for (int y = 0; y < PATI_EKR_Y; y += EKRAN_SERIT_YUKSEK) {
        const int yuk = std::min(EKRAN_SERIT_YUKSEK, PATI_EKR_Y - y);
        g_serit = ekran_serit();
        g_serit_y0 = y;
        g_serit_yuk = yuk;

        for (int i = 0; i < PATI_EKR_G * yuk; ++i) g_serit[i] = zemin;

        metin_orta(14, iniyor ? "GÜNCELLENİYOR" : "YENİ SÜRÜM", 2, vurgu);
        if (surum_metni[0] != ' ') {
            metin_orta(44, surum_metni, 3, vurgu);
        }

        if (uyari != nullptr && uyari[0] != ' ') {
            // Dusuk pil hali: tus satirinin yerine uyari.
            metin_orta(96, uyari, 2, vurgu);
        } else if (iniyor) {
            cerceve(CUB_X, CUB_Y, CUB_GEN, CUB_YUK, 2, golge);
            const int dolgu = ((CUB_GEN - 6) * y0) / 100;
            if (dolgu > 0) kutu(CUB_X + 3, CUB_Y + 3, dolgu, CUB_YUK - 6, vurgu);
            metin_orta(112, yuzde_metni, 2, vurgu);
        } else {
            metin_orta(96, "MAVİ TUŞA BAS", 2, vurgu);
        }

        const esp_err_t hata = serit_bas_ve_nefes(y, yuk);
        if (hata != ESP_OK) return hata;
    }

    return ESP_OK;
}

}  // namespace pati
