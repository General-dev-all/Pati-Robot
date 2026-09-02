#include "pati_gozler.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "pati_ekran.hpp"
#include "pati_goz_uretilmis.h"
#include "pati_pinler.h"

namespace pati {
namespace {

constexpr const char* ETIKET = "gozler";

// 30 kare/sn hedefi. SPI hesabina gore tam ekranda 43 fps sinir; kirli
// dikdortgen sayesinde pratikte daha az veri gidiyor. 30 hem yumusak
// hem butcede pay birakiyor.
// 🔴 30 -> 20, GERCEK KARTTA OLCULEREK. Eskiden 30 yaziyordu ve bu bir
// TAHMINDI; kart gelmeden once olculecek bir sey yoktu.
//
// OLCULEN (31.07.2026, YD-ESP32-S3 @160 MHz):
//   bir kare 37-41 ms  =  sekil 30  +  renk 6  +  SPI 3
//
// -O2'ye gecmek bunu 52 ms'den indirdi (o ayar hic yoktu, bkz.
// sdkconfig.defaults) ama 33 ms'lik 30 fps butcesine yetmiyor. Cizim
// isi gercekten bu kadar: goz alani ~21.000 piksel ve cerceve tamponu
// olmadigi icin her karede tamami yeniden harmanlaniyor.
//
// NEDEN 24 DEGIL DE 20: FreeRTOS tiki 10 ms (CONFIG_FREERTOS_HZ=100),
// yani kare araligi sadece 10'un katlari olabiliyor. 24 fps istemek
// 41 ms demek, pdMS_TO_TICKS onu 40 ms'ye yuvarliyor ve 37-41 ms suren
// kareler yine tasiyor. 24 denendi: kareler %97 oraninda atlandi ve
// gercek hiz 22 fps'te kaldi — 30'daki ile ayni.
//
// 20 fps = 50 ms aralik. Kareler RAHAT siginiyor, guvenlik agindaki
// uyutma hic devreye girmiyor ve akis DUZGUN oluyor. Dalgali 22 yerine
// duzgun 20 tercih edildi: goz animasyonu gercek zamana bagli, sarsak
// aralik dogrudan goze carpiyor.
//
// Yukseltmenin iki yolu var, ikisi de OLCULMEDEN yapilmayacak:
//   1. cizimi ucuzlatmak (parlama katmanlari 3 kat, sabit nokta)
//   2. CONFIG_FREERTOS_HZ=1000 — o zaman 24-27 fps mumkun olur
constexpr int HEDEF_FPS = 20;
constexpr int KARE_MS = 1000 / HEDEF_FPS;

// ---------------------------------------------------------------------------
// Pilde dusuk kare hizi
// ---------------------------------------------------------------------------
//
// 🔴 GOZ CIZICI PATI'NIN EN BUYUK SUREKLI CPU MUSTERISI. Olculdu
// (02.09.2026, gercek kart): kare basina cizim 24-30 ms ve butce 50 ms,
// yani bir cekirdegin YARISINDAN FAZLASI kesintisiz doluyor. Isin %85'i
// sekil rasterlemesinde.
//
// Pilde brownout tam Pati konusmaya baslarken oluyor: amfi, telsiz ve
// PSRAM ayni anda akim cekiyor. CPU'nun o anda bos olmasi pay birakiyor.
//
// 100 ms = 10 fps. FreeRTOS tiki 100 Hz oldugu icin aralik yalnizca
// 10'un katlari olabiliyor; 10 fps bu yuzden secildi, 12 ya da 15 degil.
//
// GOZLE FARK EDILIYOR MU: evet, biraz. Gozler daha az akici. Ama
// kullanicinin oncelik sirasi net — once cokmeme, sonra ses, sonra
// gorunum — ve gozler pilde feda edilebilir tek yer: sese
// DOKUNULMUYOR.
//
// ⚠️ BU DEGER OLCUMLE SECILMEDI, denenmek uzere kondu. Olculecek olan
// cokme sikligi: pilde 20-60 saniyede bir cokuyordu (02.09.2026 gecesi).
// Azalmazsa goz yuku sebep degil demektir ve geri alinmali — gorunumu
// bosuna bozmus oluruz.
constexpr int PIL_FPS = 10;
constexpr int PIL_KARE_MS = 1000 / PIL_FPS;

// Calisma aninda degisiyor: guc kaynagi degisince app_main ayarliyor.
std::atomic<int> g_kare_ms{KARE_MS};

// ---------------------------------------------------------------------------
// Kucuk yardimcilar — gozler240.js'teki adlarla ayni
// ---------------------------------------------------------------------------

inline float rastgele()
{
    // esp_random() donanim uretici; [0,1) araligina cekiyoruz.
    return static_cast<float>(esp_random()) / 4294967296.0f;
}

inline float kolay(float t) { return t * t * (3.0f - 2.0f * t); }
inline float cikis_kolay(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }
inline float giris_kolay(float t) { return t * t; }

inline float kirp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// ---------------------------------------------------------------------------
// Durum
// ---------------------------------------------------------------------------

struct Sekil {
    float g, y, ust_kapak, alt_kapak, egim, alt_egim, kaydir_x, kaydir_y;
};

// Bir gozun bir karedeki gercek geometrisi (piksel).
struct Yerlesim {
    float sol, ust, gen, yuk, yaricap;
    // Kapak cizgileri: y = a + b * x
    float ust_a, ust_b, alt_a, alt_b;
    float bakis_x;   // cam parlamasinin kaymasi icin
};

struct Dikdortgen {
    int x0, y0, x1, y1;   // x1/y1 DISARIDA (ekran API'siyle ayni kural)
    bool bos() const { return x1 <= x0 || y1 <= y0; }
};

const GozDurumu* g_tanim = &GOZ_DURUMLARI[0];
std::atomic<const GozDurumu*> g_istenen{&GOZ_DURUMLARI[0]};

Sekil g_simdi[2];
Sekil g_hedef[2];

float g_bakis_x = 0.0f, g_bakis_y = 0.0f;
float g_sak_bas_x = 0.0f, g_sak_bas_y = 0.0f;
float g_sak_son_x = 0.0f, g_sak_son_y = 0.0f;
float g_sak_t = 1.0f, g_sak_sure = 0.12f;
float g_sonraki_sakkad = 0.0f;

float g_kirpma_t = -1.0f;
float g_kirpma_sure = 0.26f;
float g_sonraki_kirpma = 0.0f;
bool g_ikinci_kirpma = false;

float g_gerilme = 0.0f;
float g_onceki_bakis_x = 0.0f, g_onceki_bakis_y = 0.0f;

float g_egim = 0.0f, g_hedef_egim = 0.0f;
float g_vurgu = 0.0f;

std::int64_t g_baslangic_us = 0;
std::int64_t g_son_us = 0;

Dikdortgen g_onceki_kirli{0, 0, 0, 0};

std::atomic<std::uint32_t> g_kare{0};
std::atomic<std::uint32_t> g_piksel{0};
std::atomic<std::uint32_t> g_kare_us{0};
std::atomic<std::uint32_t> g_bilinmeyen{0};
std::atomic<std::uint32_t> g_atlanan{0};

// Karenin iki maliyeti AYRI sayiliyor — bkz. cerceve_bas.
std::atomic<std::uint32_t> g_ciz_us{0};
std::atomic<std::uint32_t> g_gonder_us{0};
std::atomic<std::uint32_t> g_alan{0};
std::atomic<std::uint32_t> g_sekil_us{0};
std::atomic<std::uint32_t> g_renk_us{0};

// Serit tamponunun o anki penceresi — cizim fonksiyonlari buna yaziyor.
std::uint16_t* g_serit = nullptr;
int g_serit_x0 = 0;      // seridin ekrandaki sol kenari
int g_serit_y0 = 0;      // seridin ekrandaki ust kenari
int g_serit_gen = 0;
int g_serit_yuk = 0;
std::uint32_t g_serit_piksel = 0;

// ---------------------------------------------------------------------------
// SATIR TAMPONU — alfa karistirmasi 8 bit uzerinde yapiliyor
// ---------------------------------------------------------------------------
//
// Karistirmayi dogrudan RGB565 uzerinde yapmak BANT uretiyor: 5 bitlik
// bir kanalda %16'lik alfa hicbir sey degistirmiyor, yani parlama
// katmanlari kayboluyor. Ayrica 565'i geri acmak icin bayt sirasini
// bilmek gerekiyor ve o bilgi pati_ekran.cpp'nin icinde kapali duruyor.
//
// Cozum: bir satirlik 8 bitlik ara tampon (240 x 3 = 720 bayt).
// Sira sabit ve tek yonlu:
//
//   1. satiri temizle (siyah)
//   2. o satira degen BUTUN katmanlari ciz — parlama, goz, cam
//   3. satiri bir kerede 565'e cevirip serit tamponuna yaz
//
// Boylece karistirma tam duyarlikta, 565'e cevirme tek gecişte, ve
// bayt sirasi tek bir yerde (ekran_renk) kaliyor.

struct Satir {
    std::uint8_t r[PATI_EKR_G];
    std::uint8_t g[PATI_EKR_G];
    std::uint8_t b[PATI_EKR_G];
};

Satir* g_satir = nullptr;

inline void satir_temizle(int gen)
{
    std::memset(g_satir->r, 0, static_cast<size_t>(gen));
    std::memset(g_satir->g, 0, static_cast<size_t>(gen));
    std::memset(g_satir->b, 0, static_cast<size_t>(gen));
}

// PIKSEL BASINA CAGRILAN her sey bu isaretle satir ici ACILMAYA
// ZORLANIYOR — `inline` anahtar kelimesi YETMIYOR, o sadece bir oneri.
//
// 🔴 31.07.2026, gercek kartta olculdu. Derleyici (O3'te bile)
// `satir_karistir`i ayri bir fonksiyon olarak birakmisti. Xtensa'da bu
// PENCERELI cagri demek: her piksel icin `entry` + `retw`, pencere
// dolunca da yigina tasma. Ustelik ayri fonksiyon oldugu icin 0.0f,
// 1.0f, 255.0f sabitleri ve g_satir isaretcisi her cagride flash'taki
// deger havuzundan `l32r` ile YENIDEN okunuyordu.
//
// Piksel basina 251 cevrim cikiyordu; asil sebep buydu.
#if defined(_MSC_VER)
#  define PATI_ICERI __forceinline
#else
#  define PATI_ICERI inline __attribute__((always_inline))
#endif

// Sicak fonksiyonlari IRAM'e almak DENENDI ve HICBIR SEY DEGISTIRMEDI
// (sekil 30,9 ms -> 30,3 ms, olcum gurultusu kadar). Yani flash komut
// onbellegi darbogaz degil; dahili RAM'i bosuna harcamamak icin geri
// alindi. Bu satir, ayni fikrin ikinci kez denenmemesi icin duruyor.
#define PATI_HIZLI

// 8 bite YUVARLA — kesme DEGIL.
//
// ⚠ BU BIR HATADAN CIKTI. Once `static_cast<std::uint8_t>` yaziyordu,
//   yani kesme. Tarayici tarafinda ayni is `Uint8ClampedArray`
//   atamasiyla yapiliyor ve o EN YAKINA YUVARLIYOR (beraberlikte
//   cift sayiya). Fark piksel basina 1 birim; RGB565'e inince bir
//   tam adima (8-9) donusuyor ve konak testi 16 durumun 16'sinda
//   ayrilma gosterdi.
//
//   `lrintf` varsayilan yuvarlama kipinde (FE_TONEAREST) tam olarak
//   bunu yapiyor. Ayni ders pati_olcum.cpp'de de vardi: bankaci
//   yuvarlamasi iki tarafta ayni olmali, yoksa sayilar sessizce
//   ayrilir.
// 🔴 IKINCI DERS — 31.07.2026, GERCEK KARTTA OLCULDU.
//
//   Burada `std::lrintf` vardi. Dogru sonucu veriyordu ama xtensa'da
//   satir ici ACILMIYOR: newlib'in sf_lrint.c'sine gercek bir cagri
//   oluyor. Bu fonksiyon piksel basina UC kez cagriliyor — tam bir
//   gecis karesi yuz binlerce kutuphane cagrisi demek.
//
//   Kartta bekci kopegi iki kez havladi (9,5 sn ve 14,5 sn) ve iki
//   backtrace'in de en ic karesi ayni yerdi: lrintf'in icinde.
//
//   COZUM: yuvarlamayi FPU'nun kendisine yaptirmak. 1,5 x 2^23
//   eklenince kesirli bitler mantisin disina tasiyor ve donanim
//   sonucu KENDI yuvarlama kipiyle tam sayiya oturtuyor — varsayilan
//   kip "en yakina, beraberlikte cift", yani lrintf'in kuralinin
//   BIREBIR aynisi. Sonra ayni sayi geri cikariliyor. Kutuphane
//   cagrisi yok; yerine iki FPU islemi var.
//
//   `volatile` SART: derleyici (x + S) - S ifadesini cebirsel olarak
//   x'e sadelestirebilir ve yuvarlama tamamen kaybolurdu. Konak testi
//   bunu YAKALAYAMAZDI — o test MSVC ile PC'de derleniyor, karttaki
//   kod uretimini hic gormuyor.
PATI_ICERI std::uint8_t sekiz_bit(float x)
{
    if (x <= 0.0f) return 0;
    if (x >= 255.0f) return 255;
    constexpr float SIHIR = 12582912.0f;   // 1,5 x 2^23
    volatile float y = x + SIHIR;
    return static_cast<std::uint8_t>(static_cast<int>(y - SIHIR));
}

// Satirdaki tek pikseli karistir. `i` satir icindeki dizin (0..gen-1).
//
// Renk FLOAT geliyor, tarayicidaki gibi. Once 8 bite indirip sonra
// karistirmak fazladan bir kesme daha ekliyordu — renk gecisinin
// oldugu yerlerde en buyuk sapma buradan geliyordu.
PATI_ICERI void satir_karistir(int i, float r, float g, float b, float a)
{
    if (a >= 1.0f) {
        g_satir->r[i] = sekiz_bit(r);
        g_satir->g[i] = sekiz_bit(g);
        g_satir->b[i] = sekiz_bit(b);
    } else {
        const float t = 1.0f - a;
        g_satir->r[i] = sekiz_bit(r * a + g_satir->r[i] * t);
        g_satir->g[i] = sekiz_bit(g * a + g_satir->g[i] * t);
        g_satir->b[i] = sekiz_bit(b * a + g_satir->b[i] * t);
    }
    ++g_serit_piksel;
}

// ---------------------------------------------------------------------------
// Yuvarlatilmis dikdortgen — gozler240.js'teki `yuvarlakDoldur` ile ayni
// ---------------------------------------------------------------------------
//
// Kapaklar EGIK cizgiler; egik cizgiyi piksel piksel test etmek yerine
// her SATIR icin x sinirina ceviriyoruz. Satir basina sabit is: bir
// karekok (kose) + iki bolme (kapaklar).

struct Kapak {
    float ust_a, ust_b, alt_a, alt_b;
};

// Tek bir satiri doldurur. `sy` ekran koordinatinda satir numarasi.
// Satirin yatay ARALIGINI hesaplar — doldurmaz.
//
// 🔴 NEDEN AYRILDI (23.08.2026): parlama katmanlari gozun ALTINA da
// ciziliyordu ve hemen ardindan opak goz hepsinin uzerine yaziyordu.
// Yani her karede binlerce piksel uc kez harmanlanip sonra atiliyordu.
// O pikselleri atlayabilmek icin gozun araligini, DOLDURMADAN once
// bilmek gerekiyor — fonksiyonun ikiye ayrilmasinin tek sebebi bu.
//
// Doner: false -> bu satirda sekil yok.
PATI_HIZLI bool satir_araligi(int sy, float x0, float y0, float gen, float yuk,
                              float r, const Kapak& kapak,
                              float& L_cik, float& R_cik)
{
    if (gen <= 0.0f || yuk <= 0.0f) {
        return false;
    }
    r = std::min({r, gen * 0.5f, yuk * 0.5f});

    const float y1 = y0 + yuk;
    const float ym = static_cast<float>(sy) + 0.5f;

    // ⚠ BURADA `if (ym < y0 || ym > y1) return;` YAZIYORDU VE YANLISTI.
    //
    // Tarayici satir araligini floor/ceil ile secliyor:
    //     bas = floor(y0),  son = ceil(y1) - 1
    // Bu aralikta ym, y0'dan KUCUK olabiliyor (y0 = 10,7 ise bas = 10,
    // ym = 10,5). Tarayici o satiri yine ciziyor — kose girintisi
    // buyuyor ama satir bos kalmiyor. Benim testim onu atiyordu.
    //
    // Sonuc: sekillerin en ust ve en alt satiri bazi ifadelerde
    // eksik kaliyordu. Konak testi bunu "az piksel, buyuk fark"
    // olarak gosterdi (kizgin 83 piksel, anlamadim 22) — gozle
    // fark edilmeyecek kadar kucuk, ifadeyi bozacak kadar buyuk.
    const int bas = std::max(0, static_cast<int>(std::floor(y0)));
    const int son = std::min(PATI_GOZ_EKRAN_Y - 1,
                             static_cast<int>(std::ceil(y1)) - 1);
    if (sy < bas || sy > son) {
        return false;
    }

    // --- kose girintisi
    const float ust_r = y0 + r;
    const float alt_r = y1 - r;
    float girinti = 0.0f;
    if (ym < ust_r) {
        const float d = ust_r - ym;
        girinti = r - std::sqrt(std::max(0.0f, r * r - d * d));
    } else if (ym > alt_r) {
        const float d = ym - alt_r;
        girinti = r - std::sqrt(std::max(0.0f, r * r - d * d));
    }

    float L = x0 + girinti;
    float R = x0 + gen - girinti;

    // --- ust kapak:  ym >= ust_a + ust_b * x
    if (kapak.ust_b == 0.0f) {
        if (ym < kapak.ust_a) {
            return false;
        }
    } else {
        const float x = (ym - kapak.ust_a) / kapak.ust_b;
        if (kapak.ust_b > 0.0f) {
            R = std::min(R, x);
        } else {
            L = std::max(L, x);
        }
    }

    // --- alt kapak:  ym <= alt_a + alt_b * x
    if (kapak.alt_b == 0.0f) {
        if (ym > kapak.alt_a) {
            return false;
        }
    } else {
        const float x = (ym - kapak.alt_a) / kapak.alt_b;
        if (kapak.alt_b > 0.0f) {
            L = std::max(L, x);
        } else {
            R = std::min(R, x);
        }
    }

    if (R <= L) {
        return false;
    }

    L_cik = L;
    R_cik = R;
    return true;

}

// Tek bir satiri doldurur. `sy` ekran koordinatinda satir numarasi.
//
// ort_bas..ort_son: uzerine SONRADAN opak bir sekil yazilacagi bilinen
// piksel araligi — atlanir. ort_bas > ort_son verilirse ortme yok.
PATI_HIZLI void satira_yuvarlak(int sy, float x0, float y0, float gen, float yuk,
                     float r, const Kapak& kapak,
                     const GozRenk& renk_ust, const GozRenk& renk_alt,
                     bool gecisli, float alfa,
                     int ort_bas = 1, int ort_son = 0)
{
    float L, R;
    if (!satir_araligi(sy, x0, y0, gen, yuk, r, kapak, L, R)) {
        return;
    }
    const float ym = static_cast<float>(sy) + 0.5f;

    // --- satirin rengi: dikey gecis, satir basina bir deger
    //
    // FLOAT kaliyor ve 8 bite ancak karistirmadan SONRA iniyor —
    // tarayicidaki sira boyle. Burada uint8'e indirmek fazladan bir
    // kesme ekliyordu.
    float rr = static_cast<float>(renk_alt.r);
    float gg = static_cast<float>(renk_alt.g);
    float bb = static_cast<float>(renk_alt.b);
    if (gecisli) {
        const float t = kirp01((ym - y0) / std::max(yuk, 1.0f));
        rr = renk_ust.r + (static_cast<float>(renk_alt.r) - renk_ust.r) * t;
        gg = renk_ust.g + (static_cast<float>(renk_alt.g) - renk_ust.g) * t;
        bb = renk_ust.b + (static_cast<float>(renk_alt.b) - renk_ust.b) * t;
    }

    // Sinirlar L/R'ye DOKUNMADAN uygulaniyor: kapsama (anti-aliasing)
    // gercek kenardan hesaplanmali. Once L'yi pencereye cekip sonra
    // kapsama hesaplasak, serit sinirindaki piksel yanlis alfa alirdi.
    // Tarayici da boyle yapiyor — sadece DIZINLERI kirpiyor.
    const int xb = std::max(g_serit_x0, static_cast<int>(std::floor(L)));
    const int xs = std::min(g_serit_x0 + g_serit_gen - 1,
                            static_cast<int>(std::ceil(R)) - 1);

    for (int x = xb; x <= xs; ++x) {
        // ORTULU PIKSEL: bu satirda opak goz zaten buranin uzerine
        // yazacak. Harmanlamak icin degil, ATLAMAK icin bakiyoruz —
        // testten once en buyuk maliyet kalemi buydu.
        if (x >= ort_bas && x <= ort_son) {
            continue;
        }
        const int i = x - g_serit_x0;
        if (i < 0 || i >= g_serit_gen) {
            continue;
        }
        float k = 1.0f;
#if PATI_GOZ_KENAR_YUMUSAT
        // Kapsama: pikselin ne kadari sekil icinde. Satir basina sadece
        // iki kenar pikseli 1'den kucuk cikiyor.
        k = std::min(R, static_cast<float>(x + 1))
            - std::max(L, static_cast<float>(x));
        if (k <= 0.0f) {
            continue;
        }
        if (k > 1.0f) {
            k = 1.0f;
        }
#else
        const float m = static_cast<float>(x) + 0.5f;
        if (m < L || m > R) {
            continue;
        }
#endif
        satir_karistir(i, rr, gg, bb, alfa * k);
    }
}

// ---------------------------------------------------------------------------
// Bir karenin geometrisi
// ---------------------------------------------------------------------------

void yerlesim_hesapla(float gecen, float kirp, float tit, Yerlesim y[2])
{
    // Nefes: fark edilmiyor ama olmayinca robot "olu" goruniyor.
    const float nefes = 1.0f + std::sin(gecen * 1.45f) * 0.018f;
    const float nefes_y = std::sin(gecen * 1.45f) * 1.5f;
    const float pop = 1.0f + g_vurgu;

    const float merkez_x = PATI_GOZ_EKRAN_G * 0.5f;
    const float merkez_y = PATI_GOZ_MERKEZ_Y + nefes_y;

    // KAFA EGIMI: cerceve dondurmek yerine goz basina dikey kaydirma.
    const float egim_kaydir =
        std::sin(g_egim * 0.20f)
        * ((PATI_GOZ_G + PATI_GOZ_ARALIK) * 0.5f) * PATI_GOZ_KAFA_EGIMI;

    for (int i = 0; i < 2; ++i) {
        const Sekil& e = g_simdi[i];
        const float yon = (i == 0) ? -1.0f : 1.0f;

        const float gen = PATI_GOZ_G * e.g * pop * nefes
                          * (1.0f + g_gerilme * 0.5f);
        const float yuk = PATI_GOZ_Y * e.y * pop * nefes
                          * (1.0f - g_gerilme * 0.28f)
                          * (1.0f - kirp * 0.94f);

        const float cx = merkez_x
                         + yon * (PATI_GOZ_G * 0.5f + PATI_GOZ_ARALIK * 0.5f)
                         + (g_bakis_x + tit) * PATI_GOZ_BAKIS_X
                         + e.kaydir_x * PATI_GOZ_EKRAN_G;
        const float cy = merkez_y
                         + (g_bakis_y + tit * 0.5f) * PATI_GOZ_BAKIS_Y
                         + e.kaydir_y * PATI_GOZ_EKRAN_Y
                         + yon * egim_kaydir;

        const float sol = cx - gen * 0.5f;
        const float ust = cy - yuk * 0.5f;
        const float g_yuk = std::max(yuk, 1.0f);

        // IC / DIS kenar: sol gozun IC tarafi SAG noktasi, sag gozun IC
        // tarafi SOL noktasi. egim > 0 ic kenari asagi indirir (kizgin),
        // < 0 yukari (uzgun). Ters yazilirsa ifade tamamen degisiyor ve
        // gozle fark edilmiyor — bu yuzden testi var.
        const float ust_yari = e.egim * g_yuk * 0.5f;
        const float alt_yari = e.alt_egim * g_yuk * 0.5f;
        const float d_ust = (yon < 0.0f) ? -ust_yari : ust_yari;
        const float d_ust2 = (yon < 0.0f) ? ust_yari : -ust_yari;
        const float d_alt = (yon < 0.0f) ? -alt_yari : alt_yari;
        const float d_alt2 = (yon < 0.0f) ? alt_yari : -alt_yari;

        const float ust_y = ust + g_yuk * e.ust_kapak;
        const float alt_y = ust + g_yuk * (1.0f - e.alt_kapak);

        const float ust_b = (d_ust2 - d_ust) / std::max(gen, 1.0f);
        const float alt_b = (d_alt2 - d_alt) / std::max(gen, 1.0f);

        y[i].sol = sol;
        y[i].ust = ust;
        y[i].gen = gen;
        y[i].yuk = g_yuk;
        y[i].yaricap = std::min({static_cast<float>(PATI_GOZ_YARICAP),
                                 gen * 0.42f, g_yuk * 0.5f});
        y[i].ust_a = ust_y + d_ust - ust_b * sol;
        y[i].ust_b = ust_b;
        y[i].alt_a = alt_y + d_alt - alt_b * sol;
        y[i].alt_b = alt_b;
        y[i].bakis_x = g_bakis_x;
    }
}

Dikdortgen kapsayan(const Yerlesim y[2])
{
    // Parlama katmanlari sekli buyutuyor; kirli alan onlari da
    // icermeli, yoksa parlamanin dis kenari ekranda kaliyor.
    const float pay = PATI_GOZ_PARLAMA_KAT * PATI_GOZ_PARLAMA_KALINLIK + 2.0f;

    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < 2; ++i) {
        x0 = std::min(x0, y[i].sol - pay);
        y0 = std::min(y0, y[i].ust - pay);
        x1 = std::max(x1, y[i].sol + y[i].gen + pay);
        y1 = std::max(y1, y[i].ust + y[i].yuk + pay);
    }
    Dikdortgen d;
    d.x0 = std::max(0, static_cast<int>(std::floor(x0)));
    d.y0 = std::max(0, static_cast<int>(std::floor(y0)));
    d.x1 = std::min(PATI_GOZ_EKRAN_G, static_cast<int>(std::ceil(x1)));
    d.y1 = std::min(PATI_GOZ_EKRAN_Y, static_cast<int>(std::ceil(y1)));
    return d;
}

Dikdortgen birlesim(const Dikdortgen& a, const Dikdortgen& b)
{
    if (a.bos()) return b;
    if (b.bos()) return a;
    Dikdortgen d;
    d.x0 = std::min(a.x0, b.x0);
    d.y0 = std::min(a.y0, b.y0);
    d.x1 = std::max(a.x1, b.x1);
    d.y1 = std::max(a.y1, b.y1);
    return d;
}

// ---------------------------------------------------------------------------
// Bir satiri ciz: butun katmanlar
// ---------------------------------------------------------------------------

PATI_HIZLI void satiri_ciz(int sy, const Yerlesim y[2], float kirp)
{
    satir_temizle(g_serit_gen);

    for (int i = 0; i < 2; ++i) {
        const Yerlesim& v = y[i];
        const Kapak kapak{v.ust_a, v.ust_b, v.alt_a, v.alt_b};

        // GOZUN ARALIGI ONCE hesaplaniyor — cizim icin degil, parlama
        // katmanlarinin neyi ATLAYABILECEGINI bilmek icin.
        //
        // Goz alfa 1.0 ile ciziliyor, yani TAM kapsanan piksellerde
        // satir_karistir dogrudan atama yapiyor ve altindaki her sey
        // siliniyor. O pikselleri uc parlama katmaninda harmanlamak
        // bosa is: hesaplanip atiliyorlar.
        //
        // ⚠️ SADECE TAM kapsanan pikseller. Kenardaki kismi pikseller
        // (kapsama k < 1) altindakiyle karisiyor — onlar atlanirsa
        // gozun cevresinde bir piksellik halka bozulur.
        int ort_bas = 1, ort_son = 0;      // bos aralik = ortme yok
        float gL, gR;
        if (satir_araligi(sy, v.sol, v.ust, v.gen, v.yuk, v.yaricap,
                          kapak, gL, gR)) {
#if PATI_GOZ_KENAR_YUMUSAT
            // k == 1 kosulu: x >= L ve x + 1 <= R
            ort_bas = static_cast<int>(std::ceil(gL));
            ort_son = static_cast<int>(std::floor(gR)) - 1;
#else
            // Merkez testi: x + 0,5 araligin icinde
            ort_bas = static_cast<int>(std::ceil(gL - 0.5f));
            ort_son = static_cast<int>(std::floor(gR - 0.5f));
#endif
        }

        // --- parlama: ana seklin buyugu, dusuk alfa, birkac kat
        for (int k = PATI_GOZ_PARLAMA_KAT; k >= 1; --k) {
            const float b = static_cast<float>(k) * PATI_GOZ_PARLAMA_KALINLIK;
            const Kapak dis{kapak.ust_a - b, kapak.ust_b,
                            kapak.alt_a + b, kapak.alt_b};
            satira_yuvarlak(sy, v.sol - b, v.ust - b,
                            v.gen + b * 2.0f, v.yuk + b * 2.0f,
                            v.yaricap + b, dis,
                            GOZ_KOYU, GOZ_KOYU, false,
                            PATI_GOZ_PARLAMA_ALFA / static_cast<float>(k),
                            ort_bas, ort_son);
        }

        // --- gozun kendisi
        satira_yuvarlak(sy, v.sol, v.ust, v.gen, v.yuk, v.yaricap, kapak,
                        GOZ_ACIK, GOZ_KOYU,
                        PATI_GOZ_EGIM_RENK != 0, 1.0f);

        // --- cam parlamasi
#if PATI_GOZ_CAM_PARLAMASI
        if (kirp < 0.5f && v.yuk > 14.0f) {
            const float px = v.sol + v.gen * (0.14f - v.bakis_x * 0.04f);
            satira_yuvarlak(sy, px, v.ust + v.yuk * 0.10f,
                            v.gen * 0.26f, std::max(v.yuk * 0.20f, 3.0f),
                            v.yaricap * 0.5f, kapak,
                            GOZ_PARLAK, GOZ_PARLAK, false, 0.85f);
        }
#endif
    }
}

// ---------------------------------------------------------------------------
// Cerceveyi ekrana bas — SADECE cizim, animasyon yok
// ---------------------------------------------------------------------------
//
// Animasyondan ayri duruyor cunku ikisi ayri soru: "ne cizdik" ile
// "neden o hale geldik". Ayrilmasinin ikinci faydasi konak testi
// (test/goz_karsilastir.cpp): animasyon zamana ve rastgeleye bagli,
// cizim degil — yani bu fonksiyon ayni girdiyle her zaman ayni
// pikselleri uretiyor ve tarayicidaki karsiligiyla karsilastirilabiliyor.
void cerceve_bas(float gecen, float kirp, float tit)
{
    Yerlesim y[2];
    yerlesim_hesapla(gecen, kirp, tit, y);

    // KIRLI DIKDORTGEN: onceki karenin alani + simdiki karenin alani.
    // Eskisi de silinmeli, yoksa buyuk bir gozden kucuk bir goze
    // gecerken eski kenar ekranda kaliyor.
    const Dikdortgen simdiki = kapsayan(y);
    const Dikdortgen kirli = birlesim(g_onceki_kirli, simdiki);
    g_onceki_kirli = simdiki;

    if (kirli.bos()) {
        return;
    }

    g_serit_piksel = 0;
    g_serit_x0 = kirli.x0;
    g_serit_gen = kirli.x1 - kirli.x0;

    // Karenin IKI maliyeti ayri olculuyor.
    //
    // 31.07.2026, gercek kart: kare 56 ms cikti, butce 33 ms. Tek bir
    // toplam sayi hangisinin sucu oldugunu SOYLEMIYOR — piksel uretimi
    // mi, SPI'ye gonderim mi? Ikisinin caresi bambaska (biri matematik,
    // digeri saat hizi/ustuste bindirme), o yuzden ayri sayiliyor.
    std::uint32_t ciz = 0;
    std::uint32_t gonder = 0;
    // Cizim de kendi icinde ikiye bolunuyor: sekil rasterizasyonu
    // (satiri_ciz) ve 8 bit -> RGB565 cevrimi. 31.07.2026'da toplam
    // 49 ms cikti ve hangisinin oldugu bilinmiyordu.
    std::uint32_t sekil = 0;
    std::uint32_t renk = 0;

    for (int sy = kirli.y0; sy < kirli.y1; sy += EKRAN_SERIT_YUKSEK) {
        const int yuk = std::min(EKRAN_SERIT_YUKSEK, kirli.y1 - sy);
        g_serit = ekran_serit();
        g_serit_y0 = sy;
        g_serit_yuk = yuk;

        const std::int64_t t0 = esp_timer_get_time();
        for (int i = 0; i < yuk; ++i) {
            const std::int64_t ta = esp_timer_get_time();
            satiri_ciz(sy + i, y, kirp);
            const std::int64_t tb = esp_timer_get_time();
            // 8 bitlik satiri 565'e cevir ve serit tamponuna yaz.
            std::uint16_t* hedef = g_serit + static_cast<size_t>(i) * g_serit_gen;
            for (int x = 0; x < g_serit_gen; ++x) {
                hedef[x] = ekran_renk(g_satir->r[x], g_satir->g[x],
                                      g_satir->b[x]);
            }
            const std::int64_t tc = esp_timer_get_time();
            sekil += static_cast<std::uint32_t>(tb - ta);
            renk += static_cast<std::uint32_t>(tc - tb);
        }
        const std::int64_t t1 = esp_timer_get_time();
        ekran_serit_bas(kirli.x0, sy, g_serit_gen, yuk);
        const std::int64_t t2 = esp_timer_get_time();

        ciz += static_cast<std::uint32_t>(t1 - t0);
        gonder += static_cast<std::uint32_t>(t2 - t1);
    }

    g_piksel.store(g_serit_piksel, std::memory_order_relaxed);
    g_ciz_us.store(ciz, std::memory_order_relaxed);
    g_gonder_us.store(gonder, std::memory_order_relaxed);
    g_sekil_us.store(sekil, std::memory_order_relaxed);
    g_renk_us.store(renk, std::memory_order_relaxed);
    g_alan.store(static_cast<std::uint32_t>(
                     (kirli.x1 - kirli.x0) * (kirli.y1 - kirli.y0)),
                 std::memory_order_relaxed);
}

// Yumusatmayi atlayip durumu hedefe OTURTUR.
//
// Acilista kullaniliyor (gozler sifirdan buyuyerek gelmesin) ve konak
// testinde (belirlenimci kare icin).
void hedefe_otur(const GozDurumu* d)
{
    g_tanim = d;
    g_istenen.store(d, std::memory_order_relaxed);
    for (int i = 0; i < 2; ++i) {
        g_simdi[i] = Sekil{d->goz[i].g, d->goz[i].y,
                           d->goz[i].ust_kapak, d->goz[i].alt_kapak,
                           d->goz[i].egim, d->goz[i].alt_egim,
                           d->goz[i].kaydir_x, d->goz[i].kaydir_y};
        g_hedef[i] = g_simdi[i];
    }
    g_hedef_egim = d->egim_kafa;
    g_egim = d->egim_kafa;
    g_vurgu = 0.0f;
    g_gerilme = 0.0f;
    g_bakis_x = d->bakis_var ? d->bakis_x : 0.0f;
    g_bakis_y = d->bakis_var ? d->bakis_y : 0.0f;
    g_onceki_bakis_x = g_bakis_x;
    g_onceki_bakis_y = g_bakis_y;
    g_sak_t = 1.0f;
}

// ---------------------------------------------------------------------------
// Kare
// ---------------------------------------------------------------------------

void kare_ciz()
{
    const std::int64_t simdi_us = esp_timer_get_time();
    const float simdi = static_cast<float>(simdi_us) / 1e6f;
    const float gecen = static_cast<float>(simdi_us - g_baslangic_us) / 1e6f;
    float dt = static_cast<float>(simdi_us - g_son_us) / 1e6f;
    g_son_us = simdi_us;
    dt = std::min(dt, 0.05f);

    // --- istenen durum degistiyse gec
    const GozDurumu* istenen = g_istenen.load(std::memory_order_relaxed);
    if (istenen != g_tanim) {
        g_tanim = istenen;
        for (int i = 0; i < 2; ++i) {
            g_hedef[i] = Sekil{istenen->goz[i].g, istenen->goz[i].y,
                               istenen->goz[i].ust_kapak,
                               istenen->goz[i].alt_kapak,
                               istenen->goz[i].egim,
                               istenen->goz[i].alt_egim,
                               istenen->goz[i].kaydir_x,
                               istenen->goz[i].kaydir_y};
        }
        g_hedef_egim = istenen->egim_kafa;
        g_vurgu = istenen->giris_vurgusu;

        // Ifade degisiminde sakkad + (cogu zaman) kirpma. Insanlar da
        // yuz ifadesi degistirirken kirpar.
        const float bx = istenen->bakis_var ? istenen->bakis_x : 0.0f;
        const float by = istenen->bakis_var ? istenen->bakis_y : 0.0f;
        g_sak_bas_x = g_bakis_x;
        g_sak_bas_y = g_bakis_y;
        g_sak_son_x = bx;
        g_sak_son_y = by;
        g_sak_t = 0.0f;
        g_sak_sure = 0.13f;
        g_sonraki_sakkad = simdi + 0.9f;
        if (rastgele() < 0.55f && g_kirpma_t < 0.0f) {
            g_kirpma_t = 0.0f;
            g_kirpma_sure = 0.20f + rastgele() * 0.09f;
            g_ikinci_kirpma = rastgele() < 0.18f;
        }
    }

    // --- yumusatma
    const float k = 1.0f - std::pow(0.001f, dt * 3.2f);
    for (int i = 0; i < 2; ++i) {
        Sekil& s = g_simdi[i];
        const Sekil& h = g_hedef[i];
        s.g += (h.g - s.g) * k;
        s.y += (h.y - s.y) * k;
        s.ust_kapak += (h.ust_kapak - s.ust_kapak) * k;
        s.alt_kapak += (h.alt_kapak - s.alt_kapak) * k;
        s.egim += (h.egim - s.egim) * k;
        s.alt_egim += (h.alt_egim - s.alt_egim) * k;
        s.kaydir_x += (h.kaydir_x - s.kaydir_x) * k;
        s.kaydir_y += (h.kaydir_y - s.kaydir_y) * k;
    }
    g_egim += (g_hedef_egim - g_egim) * (1.0f - std::pow(0.001f, dt * 2.4f));
    g_vurgu *= std::pow(0.02f, dt);

    // --- sakkad: goz kaymaz, SICRAR
    if (g_sak_t < 1.0f) {
        g_sak_t = std::min(1.0f, g_sak_t + dt / g_sak_sure);
        const float p = cikis_kolay(g_sak_t);
        g_bakis_x = g_sak_bas_x + (g_sak_son_x - g_sak_bas_x) * p;
        g_bakis_y = g_sak_bas_y + (g_sak_son_y - g_sak_bas_y) * p;
    } else if (simdi > g_sonraki_sakkad) {
        const bool sabit = g_tanim->bakis_var;
        const float yay = sabit ? 0.22f : 0.85f;
        const float mx = sabit ? g_tanim->bakis_x : 0.0f;
        const float my = sabit ? g_tanim->bakis_y : 0.0f;
        const float hx = mx + (rastgele() - 0.5f) * 2.0f * yay;
        const float hy = my + (rastgele() - 0.5f) * yay * 0.55f;
        g_sak_bas_x = g_bakis_x;
        g_sak_bas_y = g_bakis_y;
        g_sak_son_x = hx;
        g_sak_son_y = hy;
        g_sak_t = 0.0f;
        g_sak_sure = 0.075f + std::hypot(hx - g_bakis_x, hy - g_bakis_y) * 0.05f;
        const float h = g_tanim->hareketlilik > 0.0f ? g_tanim->hareketlilik : 1.0f;
        g_sonraki_sakkad = simdi + (0.9f + rastgele() * 2.6f) / h;
    }

    // Mikro titreme: goz sabitken bile tamamen durmuyor.
    const float tit = std::sin(gecen * 21.0f) * 0.006f
                      + std::sin(gecen * 13.7f) * 0.004f;

    // --- squash & stretch
    const float hiz = std::hypot(g_bakis_x - g_onceki_bakis_x,
                                 g_bakis_y - g_onceki_bakis_y)
                      / std::max(dt, 0.001f);
    g_onceki_bakis_x = g_bakis_x;
    g_onceki_bakis_y = g_bakis_y;
    g_gerilme += (std::min(hiz * 0.035f, 0.28f) - g_gerilme)
                 * (1.0f - std::pow(0.001f, dt * (hiz > 0.5f ? 8.0f : 3.0f)));

    // --- kirpma: kapanma hizli, acilma yavas
    float kirp = 0.0f;
    if (g_kirpma_t >= 0.0f) {
        g_kirpma_t += dt / g_kirpma_sure;
        const float t = g_kirpma_t;
        if (t < 0.34f) {
            kirp = giris_kolay(t / 0.34f);
        } else if (t < 0.44f) {
            kirp = 1.0f;
        } else if (t < 1.0f) {
            kirp = 1.0f - kolay((t - 0.44f) / 0.56f);
        }
        if (g_kirpma_t >= 1.0f) {
            g_kirpma_t = -1.0f;
            if (g_ikinci_kirpma) {
                g_ikinci_kirpma = false;
                g_kirpma_t = 0.0f;
                g_kirpma_sure = 0.16f;
            } else {
                const float h = g_tanim->kirpma_hizi > 0.0f
                                    ? g_tanim->kirpma_hizi : 1.0f;
                g_sonraki_kirpma = simdi + (1.6f + rastgele() * 4.2f) / h;
            }
        }
    } else if (simdi > g_sonraki_kirpma) {
        g_kirpma_t = 0.0f;
        g_kirpma_sure = 0.20f + rastgele() * 0.09f;
        g_ikinci_kirpma = rastgele() < 0.18f;
    }

    cerceve_bas(gecen, kirp, tit);

    g_kare.fetch_add(1, std::memory_order_relaxed);
    g_kare_us.store(static_cast<std::uint32_t>(esp_timer_get_time() - simdi_us),
                    std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------

void gozler_gorevi(void*)
{
    g_baslangic_us = esp_timer_get_time();
    g_son_us = g_baslangic_us;

    // Ilk kare hedefe OTURTULUYOR, yumusatilmiyor: acilista gozler
    // sifirdan buyuyerek gelmesin.
    hedefe_otur(g_istenen.load(std::memory_order_relaxed));

    TickType_t son_uyanma = xTaskGetTickCount();
    while (true) {
        kare_ciz();
        // vTaskDelayUntil: kare suresi degisse bile FPS sabit kaliyor.
        // vTaskDelay olsa cizim suresi ustune eklenir ve hiz dalgalanir.
        const int kare_ms = g_kare_ms.load(std::memory_order_relaxed);
        if (!xTaskDelayUntil(&son_uyanma, pdMS_TO_TICKS(kare_ms))) {
            // Gecikti: kare butcesi asildi.
            g_atlanan.fetch_add(1, std::memory_order_relaxed);

            // 🔴 31.07.2026 — GERCEK KARTTA GORULDU, PC'de gorunmuyordu.
            //
            // xTaskDelayUntil takvim gectiyse BLOKLAMADAN donuyor. Kareler
            // ust uste butceyi asinca bu gorev islemciyi HIC birakmiyor:
            // cekirdegin IDLE gorevi calisamiyor ve 5 saniyede bekci
            // kopegi havliyor. Acilis gecisinde iki kez oldu.
            //
            // Takvimi simdiye cekiyoruz — yoksa biriken "borc" yuzunden
            // sonraki kareler de bloklamadan donup ayni cukura dusuyor.
            // Sonra bir tik uyuyoruz: bir kare gec kalmak, cekirdegi
            // kilitlemekten iyidir.
            son_uyanma = xTaskGetTickCount();
            vTaskDelay(1);
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------

void gozler_durum(const char* ad)
{
    if (ad == nullptr) {
        return;
    }
    for (int i = 0; i < GOZ_DURUM_SAYISI; ++i) {
        if (std::strcmp(GOZ_DURUMLARI[i].ad, ad) == 0) {
            g_istenen.store(&GOZ_DURUMLARI[i], std::memory_order_relaxed);
            return;
        }
    }
    // Tabloda yok. Sessizce yutmuyoruz — model listede olmayan bir sey
    // uyduruyorsa ya biz yanlis ad gonderiyorsak bu sayida gorunur.
    g_bilinmeyen.fetch_add(1, std::memory_order_relaxed);
    ESP_LOGW(ETIKET, "bilinmeyen ifade: %s", ad);
}

void gozler_bos() { gozler_durum("bos"); }
void gozler_dinliyor() { gozler_durum("dinliyor"); }
void gozler_dusunuyor() { gozler_durum("dusunuyor"); }
void gozler_konusuyor() { gozler_durum("konusuyor"); }

std::uint32_t gozler_kare() { return g_kare.load(std::memory_order_relaxed); }
std::uint32_t gozler_piksel() { return g_piksel.load(std::memory_order_relaxed); }
int gozler_hedef_fps() { return 1000 / g_kare_ms.load(std::memory_order_relaxed); }

void gozler_pil_kipi(bool pilde)
{
    g_kare_ms.store(pilde ? PIL_KARE_MS : KARE_MS, std::memory_order_relaxed);
}
std::uint32_t gozler_kare_us() { return g_kare_us.load(std::memory_order_relaxed); }
std::uint32_t gozler_ciz_us() { return g_ciz_us.load(std::memory_order_relaxed); }
std::uint32_t gozler_gonder_us()
{
    return g_gonder_us.load(std::memory_order_relaxed);
}
std::uint32_t gozler_alan() { return g_alan.load(std::memory_order_relaxed); }
std::uint32_t gozler_sekil_us()
{
    return g_sekil_us.load(std::memory_order_relaxed);
}
std::uint32_t gozler_renk_us() { return g_renk_us.load(std::memory_order_relaxed); }
std::uint32_t gozler_bilinmeyen_durum()
{
    return g_bilinmeyen.load(std::memory_order_relaxed);
}
std::uint32_t gozler_atlanan_kare()
{
    return g_atlanan.load(std::memory_order_relaxed);
}

const char* gozler_su_anki()
{
    return g_istenen.load(std::memory_order_relaxed)->ad;
}

esp_err_t gozler_baslat()
{
    const esp_err_t s = ekran_baslat();
    if (s != ESP_OK) {
        ESP_LOGE(ETIKET, "ekran acilmadi, gozler yok: %s", esp_err_to_name(s));
        return s;
    }

    // Satir tamponu: 240 x 3 bayt. Ic RAM'de ama DMA gerekmiyor —
    // buradan serit tamponuna kopyalaniyor.
    g_satir = new (std::nothrow) Satir{};
    if (g_satir == nullptr) {
        ESP_LOGE(ETIKET, "satir tamponu ayrilamadi");
        return ESP_ERR_NO_MEM;
    }

    ekran_test_deseni();
    vTaskDelay(pdMS_TO_TICKS(2500));   // desene bakacak kadar dursun
    ekran_doldur(ekran_renk(0, 0, 0));

    // Onceki kirli alani sifirla: test deseni butun ekrani boyadi ama
    // artik siyah, yani silinecek bir sey yok.
    g_onceki_kirli = Dikdortgen{0, 0, 0, 0};

    // Onceligi DUSUK. Ses hattinin onune gecmemesi gerekiyor: gecikme
    // olcumu 1500 ms kriterine bagli ve cizim orada yer almamali.
    const BaseType_t ok = xTaskCreate(gozler_gorevi, "gozler", 4096, nullptr,
                                      3, nullptr);
    if (ok != pdPASS) {
        ESP_LOGE(ETIKET, "cizim gorevi acilamadi");
        return ESP_FAIL;
    }

    ESP_LOGI(ETIKET, "gozler basladi: %d ifade, %d fps hedefi",
             GOZ_DURUM_SAYISI, HEDEF_FPS);
    return ESP_OK;
}

}  // namespace pati
