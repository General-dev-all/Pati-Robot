// MSVC fopen uyarisi — konak testi, dosya adlari bizden geliyor.
#define _CRT_SECURE_NO_WARNINGS
// ===========================================================================
// SES — yeniden ornekleyicinin konak sinamasi
// ===========================================================================
//
// NEDEN VAR: bu kod Pati'nin SESINI uretiyor. Bozuldugunda "hata" diye
// degil "kotu ses" diye duyuluyor — cizirti, tik, yanlis perde. Ve
// StickS3 kartlari Eylul'de gelecek, yani gercek donanimda denemenin
// yolu yok.
//
// SINANAN SEY GERCEK KOD: ../main/pati_ornekleyici.hpp dogrudan ice
// aliniyor, kopyasi cikarilmiyor. Ayni yaklasim goz cizicide uc gercek
// hata bulmustu.
//
// ---------------------------------------------------------------------------
// NE SINANIYOR
// ---------------------------------------------------------------------------
//
//   1. ADIM         carpan / 2 formulu ve 1'den kucuk kalmasi
//   2. PERDE        1000 Hz giren ses 1300 Hz cikiyor mu
//   3. SURE         ses %30 kisaliyor mu
//   4. PARCA SINIRI  <-- EN ONEMLISI
//   5. FAZ KAYMASI  uzun akista birikme var mi
//   6. SINIRLAYICI  tasma ve isaret donmesi
//
// 4 NEDEN EN ONEMLISI: Gemini sesi 200-280 ms'lik dilimler halinde
// geliyor ve her dilim ayri bir cagri. Yeniden ornekleyici fazi
// dilimler arasinda tasimazsa her sinirda dalga sicriyor — saniyede
// birkac kez duyulan bir tik. Kulakla "cihaz bozuk" gibi geliyor ve
// koda bakarak gorulmesi cok zor. Sinama bunu kesin olarak yakaliyor:
// ses TEK PARCA halinde ve DUZENSIZ DILIMLER halinde islenip iki cikti
// bayt bayt karsilastiriliyor. Fazi tasimayan bir kod bu sinamayi
// geceMEZ.
//
// ---------------------------------------------------------------------------
// KULAKLA DINLEME
// ---------------------------------------------------------------------------
//
//     ses_karsilastir.exe --pcm konusma.pcm --cikti .
//
// Iki WAV yaziyor:
//
//   ses_devkit.wav    24 kHz ornekler, basligi 31.200 Hz diyor.
//                     Onceki kartin CALDIGI seyin ta kendisi — orada
//                     hiz hilesi I2S saatindeydi, yani ornekler hic
//                     degismeden farkli hizda calniyordu.
//
//   ses_sticks3.wav   Yeni yolun ciktisi: 48 kHz'e yeniden ornekleme.
//
// IKISI AYNI DUYULMALI. Farkli duyuluyorsa Pati'nin sesi bozulmus
// demektir ve bu, kabul edilmeyecek tek sonuc.

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "../main/pati_ornekleyici.hpp"

namespace {

// Kart tarafindaki sabitlerin aynisi. pati_pinler.h'yi ice almiyoruz:
// o dosya driver/gpio.h istiyor ve konakta yok.
constexpr int GEMINI_CIKIS_HZ = 24000;
constexpr int SES_HZ = 48000;
constexpr float CARPAN = 1.30f;

int g_hata = 0;

void gecti(const char* ad, const std::string& ayrinti)
{
    std::printf("  OK    %-22s %s\n", ad, ayrinti.c_str());
}

void kaldi(const char* ad, const std::string& ayrinti)
{
    std::printf("  HATA  %-22s %s\n", ad, ayrinti.c_str());
    g_hata = 1;
}

void sina(const char* ad, bool kosul, const std::string& ayrinti)
{
    if (kosul) gecti(ad, ayrinti); else kaldi(ad, ayrinti);
}

std::string biciml(const char* kalip, ...)
{
    char tampon[256];
    va_list liste;
    va_start(liste, kalip);
    std::vsnprintf(tampon, sizeof(tampon), kalip, liste);
    va_end(liste);
    return std::string(tampon);
}

float adim_hesapla(float carpan)
{
    return carpan * static_cast<float>(GEMINI_CIKIS_HZ) /
           static_cast<float>(SES_HZ);
}

// Ornekleyiciyi calistirip butun ciktiyi toplar.
std::vector<std::int16_t> calistir(pati::YenidenOrnekleyici& o,
                                   std::span<const std::int16_t> kaynak,
                                   float adim, float seviye,
                                   std::size_t blok = 512)
{
    std::vector<std::int16_t> cikti;
    std::vector<std::int16_t> tampon(blok);
    o.isle(kaynak, adim, seviye, tampon,
           [&](std::span<const std::int16_t> b) {
               cikti.insert(cikti.end(), b.begin(), b.end());
               return true;
           });
    return cikti;
}

std::vector<std::int16_t> sinus(int hz, int ornek_hz, int ornek, double genlik)
{
    std::vector<std::int16_t> s(static_cast<std::size_t>(ornek));
    for (int i = 0; i < ornek; ++i) {
        const double t = static_cast<double>(i) / ornek_hz;
        s[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(
            std::lround(genlik * std::sin(2.0 * 3.14159265358979 * hz * t)));
    }
    return s;
}

// Sifir gecislerinden frekans olcer. Ara deger ile alt-ornek hassasiyet.
double frekans_olc(std::span<const std::int16_t> s, int ornek_hz)
{
    double ilk = -1.0, son = -1.0;
    int gecis = 0;
    for (std::size_t i = 1; i < s.size(); ++i) {
        if (s[i - 1] < 0 && s[i] >= 0) {
            const double a = static_cast<double>(s[i - 1]);
            const double b = static_cast<double>(s[i]);
            const double kesir = (b != a) ? (-a / (b - a)) : 0.0;
            const double konum = static_cast<double>(i - 1) + kesir;
            if (ilk < 0) ilk = konum; else son = konum;
            if (ilk >= 0) ++gecis;
        }
    }
    if (gecis < 2 || son < 0) return 0.0;
    const double sure = (son - ilk) / ornek_hz;
    return (gecis - 1) / sure;
}

// ---------------------------------------------------------------------------
// WAV yazma
// ---------------------------------------------------------------------------
void wav_yaz(const std::string& yol, std::span<const std::int16_t> s, int hz)
{
    std::FILE* d = std::fopen(yol.c_str(), "wb");
    if (d == nullptr) {
        std::printf("  HATA  yazilamadi: %s\n", yol.c_str());
        g_hata = 1;
        return;
    }
    const std::uint32_t veri = static_cast<std::uint32_t>(s.size() * 2);
    const std::uint32_t bayt_hz = static_cast<std::uint32_t>(hz) * 2;

    auto u32 = [&](std::uint32_t v) { std::fwrite(&v, 4, 1, d); };
    auto u16 = [&](std::uint16_t v) { std::fwrite(&v, 2, 1, d); };

    std::fwrite("RIFF", 1, 4, d); u32(36 + veri);
    std::fwrite("WAVE", 1, 4, d);
    std::fwrite("fmt ", 1, 4, d); u32(16); u16(1); u16(1);
    u32(static_cast<std::uint32_t>(hz)); u32(bayt_hz); u16(2); u16(16);
    std::fwrite("data", 1, 4, d); u32(veri);
    std::fwrite(s.data(), 2, s.size(), d);
    std::fclose(d);

    std::printf("  yazildi: %-22s %6zu ornek @ %d Hz  (%.2f sn)\n",
                yol.c_str(), s.size(), hz,
                static_cast<double>(s.size()) / hz);
}

}  // namespace

int main(int argc, char** argv)
{
    std::string pcm, cikti_dizin = ".";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--pcm") == 0 && i + 1 < argc) pcm = argv[++i];
        else if (std::strcmp(argv[i], "--cikti") == 0 && i + 1 < argc) cikti_dizin = argv[++i];
    }

    std::printf("\n=== SES — yeniden ornekleyici ===\n\n");

    const float adim = adim_hesapla(CARPAN);

    // -- 1. ADIM ------------------------------------------------------------
    sina("adim = carpan/2",
         std::fabs(adim - CARPAN / 2.0f) < 1e-6f,
         biciml("carpan %.2f -> adim %.4f", CARPAN, adim));

    // Katlanma korumasi: carpan araliginin TAMAMINDA adim < 1 olmali.
    // Buyuk olsaydi seyreltme yapardik ve tiz sesler cizirtiya donerdi.
    bool hepsi_kucuk = true;
    for (float c = 0.80f; c <= 1.601f; c += 0.05f) {
        if (adim_hesapla(c) >= 1.0f) hepsi_kucuk = false;
    }
    sina("adim her zaman < 1", hepsi_kucuk,
         biciml("0.80..1.60 araliginda en buyuk adim %.4f",
                adim_hesapla(1.60f)));

    // -- 2. PERDE -----------------------------------------------------------
    {
        const auto kaynak = sinus(1000, GEMINI_CIKIS_HZ, 24000, 12000.0);
        pati::YenidenOrnekleyici o;
        const auto c = calistir(o, kaynak, adim, 1.0f);
        const double f = frekans_olc(c, SES_HZ);
        const double beklenen = 1000.0 * CARPAN;
        sina("perde 1000 -> 1300 Hz",
             std::fabs(f - beklenen) < 2.0,
             biciml("olculen %.1f Hz, beklenen %.1f Hz", f, beklenen));
    }

    // -- 3. SURE ------------------------------------------------------------
    {
        const auto kaynak = sinus(440, GEMINI_CIKIS_HZ, 24000, 12000.0);
        pati::YenidenOrnekleyici o;
        const auto c = calistir(o, kaynak, adim, 1.0f);
        const double giris_sn = 24000.0 / GEMINI_CIKIS_HZ;
        const double cikis_sn = static_cast<double>(c.size()) / SES_HZ;
        const double oran = giris_sn / cikis_sn;
        sina("sure %30 kisaliyor",
             std::fabs(oran - CARPAN) < 0.005,
             biciml("%.3f sn -> %.3f sn  (oran %.4f)",
                    giris_sn, cikis_sn, oran));
    }

    // -- 4. PARCA SINIRI — EN ONEMLI SINAMA ---------------------------------
    {
        const auto kaynak = sinus(700, GEMINI_CIKIS_HZ, 40000, 12000.0);

        pati::YenidenOrnekleyici tek_o;
        const auto tek = calistir(tek_o, kaynak, adim, 1.0f);

        // Gemini'nin gercekte gonderdigi gibi DUZENSIZ dilimler:
        // 200-280 ms araligi 24 kHz'de 4800-6720 ornek.
        const std::size_t dilimler[] = {4800, 6720, 5000, 6100, 4800,
                                        6720, 5880, 6000};
        pati::YenidenOrnekleyici parca_o;
        std::vector<std::int16_t> parcali;
        std::size_t konum = 0, k = 0;
        while (konum < kaynak.size()) {
            const std::size_t n =
                std::min(dilimler[k++ % 8], kaynak.size() - konum);
            const auto b = calistir(
                parca_o,
                std::span<const std::int16_t>(kaynak.data() + konum, n),
                adim, 1.0f);
            parcali.insert(parcali.end(), b.begin(), b.end());
            konum += n;
        }

        const bool ayni = (tek.size() == parcali.size()) &&
                          std::equal(tek.begin(), tek.end(), parcali.begin());

        std::size_t ilk_fark = 0;
        if (!ayni) {
            const std::size_t n = std::min(tek.size(), parcali.size());
            while (ilk_fark < n && tek[ilk_fark] == parcali[ilk_fark]) ++ilk_fark;
        }

        sina("parca siniri surekli", ayni,
             ayni ? biciml("%zu ornek, tek parca ile 8 duzensiz dilim "
                           "BIREBIR ayni", tek.size())
                  : biciml("AYRISIYOR: tek %zu, parcali %zu, ilk fark %zu. "
                           "Bu, her dilim sinirinda TIK sesi demek.",
                           tek.size(), parcali.size(), ilk_fark));
    }

    // -- 5. FAZ KAYMASI -----------------------------------------------------
    //
    // faz her dilimde `t - N` ile yeniden hesaplaniyor ve t bir float.
    // Uzun akista bu cikarma hassasiyet kaybediyor. Kayma birikirse ses
    // yavas yavas kayar. Olculuyor cunku akil yurutmekle kesinlesmiyor.
    {
        const auto dilim = sinus(500, GEMINI_CIKIS_HZ, 6000, 8000.0);
        pati::YenidenOrnekleyici o;
        std::size_t toplam_cikis = 0;
        constexpr int TUR = 2000;  // 6000 ornek x 2000 = 500 saniye ses
        for (int i = 0; i < TUR; ++i) {
            toplam_cikis += calistir(o, dilim, adim, 1.0f).size();
        }
        const double beklenen =
            static_cast<double>(dilim.size()) * TUR / adim;
        const double sapma = std::fabs(static_cast<double>(toplam_cikis) - beklenen);
        sina("uzun akista kayma yok",
             sapma < 4.0,
             biciml("%d sn ses, %zu ornek uretildi, beklenen %.0f, "
                    "sapma %.1f ornek",
                    static_cast<int>(dilim.size()) * TUR / GEMINI_CIKIS_HZ,
                    toplam_cikis, beklenen, sapma));
    }

    // -- 6. SINIRLAYICI -----------------------------------------------------
    {
        // Tam genlikli ses, ustune en yuksek seviye. Kirpma olsaydi
        // isaret donerdi: tepe noktasi ANI olarak ters isarete atlar ve
        // hoparlorde CIT diye vurur.
        const auto kaynak = sinus(300, GEMINI_CIKIS_HZ, 8000, 32000.0);
        pati::YenidenOrnekleyici o;
        const auto c = calistir(o, kaynak, adim, 2.0f);

        int isaret_donmesi = 0;
        for (std::size_t i = 1; i < c.size(); ++i) {
            // Komsu ornekler arasinda tam genlikte isaret degisimi
            const bool buyuk = std::abs(c[i - 1]) > 24000 && std::abs(c[i]) > 24000;
            if (buyuk && ((c[i - 1] > 0) != (c[i] > 0))) ++isaret_donmesi;
        }
        sina("sinirlayici tasmiyor", isaret_donmesi == 0,
             biciml("seviye 2.00, tam genlikli giris, %d isaret donmesi",
                    isaret_donmesi));

        // Sinirlayici MONOTON olmali: buyuyen girdi buyuyen cikti
        // vermeli, yoksa dalga bicimi bozulur.
        bool monoton = true;
        std::int16_t onceki = pati::yumusak_sinirla(0.0f);
        for (float v = 0.0f; v < 90000.0f; v += 137.0f) {
            const std::int16_t y = pati::yumusak_sinirla(v);
            if (y < onceki) monoton = false;
            onceki = y;
        }
        sina("sinirlayici monoton", monoton,
             biciml("0..90000 arasi, tavan %d",
                    static_cast<int>(pati::yumusak_sinirla(1e6f))));
    }

    // -- 7. KULAKLA DINLEME ICIN WAV ---------------------------------------
    if (!pcm.empty()) {
        std::FILE* d = std::fopen(pcm.c_str(), "rb");
        if (d == nullptr) {
            std::printf("\n  HATA  pcm acilamadi: %s\n", pcm.c_str());
            g_hata = 1;
        } else {
            std::fseek(d, 0, SEEK_END);
            const long boy = std::ftell(d);
            std::fseek(d, 0, SEEK_SET);
            std::vector<std::int16_t> kaynak(static_cast<std::size_t>(boy / 2));
            std::fread(kaynak.data(), 2, kaynak.size(), d);
            std::fclose(d);

            std::printf("\n  --- kulakla dinleme ---\n");

            // Onceki kartin caldigi sey: ornekler AYNI, saat farkli.
            const int devkit_hz =
                static_cast<int>(GEMINI_CIKIS_HZ * CARPAN + 0.5f);
            wav_yaz(cikti_dizin + "/ses_devkit.wav", kaynak, devkit_hz);

            // Yeni yol: 48 kHz'e yeniden ornekleme.
            pati::YenidenOrnekleyici o;
            const auto yeni = calistir(o, kaynak, adim, 1.0f);
            wav_yaz(cikti_dizin + "/ses_sticks3.wav", yeni, SES_HZ);

            const double a = static_cast<double>(kaynak.size()) / devkit_hz;
            const double b = static_cast<double>(yeni.size()) / SES_HZ;
            sina("iki dosya ayni uzunlukta", std::fabs(a - b) < 0.002,
                 biciml("devkit %.3f sn, sticks3 %.3f sn", a, b));

            std::printf("\n  IKISI AYNI DUYULMALI. Farkliysa Pati'nin sesi\n");
            std::printf("  bozulmus demektir.\n");
        }
    }

    std::printf("\n");
    if (g_hata) {
        std::printf("  SES: BASARISIZ\n\n");
    } else {
        std::printf("  YENIDEN ORNEKLEYICI DOGRU CALISIYOR\n\n");
    }
    return g_hata;
}
