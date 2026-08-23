// URETILMIS DOSYA — ELLE DEGISTIRILMEZ.
//
// Kaynak : panel/gozler240.js
// Ureten : firmware/goz_uret.mjs   (cd firmware && node goz_uret.mjs)
//
// Buraya elle yazilan her sey bir sonraki uretimde silinir. Goz
// olculerini degistirmek icin gelistirici.html studyosunu kullan,
// begendigin degerleri gozler240.js icindeki AYAR blogua yaz, sonra
// bu ureteci calistir. Boylece tarayicida gorulen sey ile ekranda
// cizilen sey AYNI KALIR.

#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// Olculer — hepsi PIKSEL
// ---------------------------------------------------------------------------

#define PATI_GOZ_EKRAN_G        240
#define PATI_GOZ_EKRAN_Y        135

#define PATI_GOZ_G              68
#define PATI_GOZ_Y              64
#define PATI_GOZ_ARALIK         28
#define PATI_GOZ_YARICAP        22
#define PATI_GOZ_MERKEZ_Y       67
#define PATI_GOZ_BAKIS_X        14
#define PATI_GOZ_BAKIS_Y        7

// Parlama: gercek bulanik golge ESP32"de yok. Ana seklin biraz
// buyugu, dusuk alfayla, birkac kat halinde ciziliyor. Olculdu:
// cizim maliyetinin %81"i bu katmanlar (bkz. gozler_test.mjs).
#define PATI_GOZ_PARLAMA_KAT       3
#define PATI_GOZ_PARLAMA_KALINLIK  3
#define PATI_GOZ_PARLAMA_ALFA      0.16f

// Kafa egimi ESP32"de goz basina DIKEY KAYDIRMA olarak yapiliyor.
// Cerceve tamponunu gercekten dondurmek piksel basina ters donusum
// demek; bu boyutta egim 2 dereceyi gecmedigi icin fark okunmuyor.
#define PATI_GOZ_KAFA_EGIMI     1.0f

#define PATI_GOZ_CAM_PARLAMASI  1
#define PATI_GOZ_EGIM_RENK      1
#define PATI_GOZ_KENAR_YUMUSAT  1

#define PATI_GOZ_GIRIS_VURGUSU_VARSAYILAN 0.08f

namespace pati {

struct GozRenk { std::uint8_t r, g, b; };

// Turkuaz — prototype/arayuz/gozler.js ile ayni iki uc.
inline constexpr GozRenk GOZ_ACIK   { 93, 242, 242 };
inline constexpr GozRenk GOZ_KOYU   { 23, 196, 196 };
inline constexpr GozRenk GOZ_PARLAK { 225, 255, 255 };

// Bir gozun sekli. Alanlarin anlami gozler240.js"teki NOTR ile ayni:
//   g / y            : taban olcunun carpani
//   ustKapak/altKapak: gozun ustunden/altindan kapanan oran
//   egim / altEgim   : IC kenari asagi (+) ya da yukari (-) kaydirir
//   kaydirX/kaydirY  : ekran boyunun orani olarak kayma
struct GozSekli {
    float g, y, ust_kapak, alt_kapak, egim, alt_egim, kaydir_x, kaydir_y;
};

struct GozDurumu {
    const char* ad;
    GozSekli    goz[2];        // 0 = sol, 1 = sag
    float       kirpma_hizi;   // 1 = normal, buyuk = daha sik kirpar
    float       hareketlilik;  // bakisin siklik ve genisligi
    float       egim_kafa;
    bool        bakis_var;     // sabit bir bakis yonu tanimli mi
    float       bakis_x, bakis_y;
    float       giris_vurgusu; // duruma girerken atilan kisa abartma
};

inline constexpr int GOZ_DURUM_SAYISI = 16;

inline constexpr GozDurumu GOZ_DURUMLARI[GOZ_DURUM_SAYISI] = {
    { "bos",
      { { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
      1.0f, 1.0f, 0.0f,
      false, 0.0f, 0.0f, 0.08f },
    { "notr",
      { { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
      1.0f, 1.0f, 0.0f,
      false, 0.0f, 0.0f, 0.08f },
    { "dinliyor",
      { { 1.05f, 1.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 1.05f, 1.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
      0.7f, 0.5f, 0.0f,
      false, 0.0f, 0.0f, 0.08f },
    { "dusunuyor",
      { { 1.0f, 0.84f, 0.16f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.84f, 0.16f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
      0.6f, 0.3f, 0.05f,
      true, -0.55f, -0.5f, 0.08f },
    { "konusuyor",
      { { 1.0f, 1.02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 1.02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
      1.2f, 1.3f, 0.0f,
      false, 0.0f, 0.0f, 0.08f },
    { "mutlu",
      { { 1.0f, 1.05f, 0.0f, 0.46f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 1.05f, 0.0f, 0.46f, 0.0f, 0.0f, 0.0f, 0.0f } },
      1.5f, 1.4f, 0.0f,
      false, 0.0f, 0.0f, 0.12f },
    { "cok_mutlu",
      { { 1.1f, 1.16f, 0.0f, 0.56f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 1.1f, 1.16f, 0.0f, 0.56f, 0.0f, 0.0f, 0.0f, 0.0f } },
      2.0f, 1.8f, 0.0f,
      false, 0.0f, 0.0f, 0.22f },
    { "saskin",
      { { 1.12f, 1.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 1.12f, 1.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
      0.4f, 0.4f, 0.0f,
      false, 0.0f, 0.0f, 0.3f },
    { "uzgun",
      { { 1.0f, 0.78f, 0.34f, 0.1f, -0.55f, 0.0f, 0.0f, 0.12f },
        { 1.0f, 0.78f, 0.34f, 0.1f, -0.55f, 0.0f, 0.0f, 0.12f } },
      0.6f, 0.35f, 0.0f,
      true, 0.0f, 0.3f, 0.08f },
    { "kizgin",
      { { 1.0f, 0.88f, 0.24f, 0.0f, 0.48f, 0.1f, 0.0f, 0.0f },
        { 1.0f, 0.88f, 0.24f, 0.0f, 0.48f, 0.1f, 0.0f, 0.0f } },
      0.9f, 0.7f, 0.0f,
      false, 0.0f, 0.0f, 0.14f },
    { "somurtkan",
      { { 1.04f, 1.06f, 0.06f, 0.28f, 0.3f, -0.1f, 0.0f, 0.0f },
        { 1.04f, 1.06f, 0.06f, 0.28f, 0.3f, -0.1f, 0.0f, 0.0f } },
      1.7f, 1.9f, 0.09f,
      true, -0.3f, 0.12f, 0.26f },
    { "uykulu",
      { { 1.0f, 0.5f, 0.79f, 0.08f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.5f, 0.79f, 0.08f, 0.0f, 0.0f, 0.0f, 0.0f } },
      0.2f, 0.1f, 0.05f,
      true, 0.0f, 0.5f, 0.08f },
    { "meraklı",
      { { 1.0f, 1.22f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.86f, 0.2f, 0.0f, -0.18f, 0.0f, 0.0f, 0.0f } },
      1.1f, 1.5f, 0.13f,
      false, 0.0f, 0.0f, 0.14f },
    { "anlamadim",
      { { 1.0f, 0.9f, 0.2f, 0.0f, 0.2f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 1.14f, 0.02f, 0.0f, -0.22f, 0.0f, 0.0f, 0.0f } },
      1.3f, 1.2f, 0.15f,
      false, 0.0f, 0.0f, 0.08f },
    { "afacan",
      { { 1.0f, 0.7f, 0.0f, 0.44f, 0.26f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 1.1f, 0.02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
      1.8f, 2.0f, 0.1f,
      true, 0.35f, 0.0f, 0.18f },
    { "haylaz",
      { { 1.04f, 0.46f, 0.3f, 0.16f, 0.3f, 0.0f, 0.0f, 0.0f },
        { 1.04f, 0.46f, 0.3f, 0.16f, 0.3f, 0.0f, 0.0f, 0.0f } },
      1.6f, 2.2f, 0.06f,
      false, 0.0f, 0.0f, 0.2f },
};

}  // namespace pati
