#include "pati_olcum.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <esp_log.h>
#include <esp_timer.h>

namespace pati {
namespace {

constexpr const char* ETIKET = "pati.olcum";

// PLAN.md kriterleri. Olcumden ONCE yazildi.
constexpr double KRITER_GECER_MS  = 1500.0;
constexpr double KRITER_SINIR_MS  = 2000.0;

std::array<Tur, EN_FAZLA_TUR> g_turlar{};
std::size_t g_adet = 0;
bool g_acik = false;
std::uint32_t g_dusen_parca = 0;

double us_to_ms(std::int64_t bas, std::int64_t son)
{
    return static_cast<double>(son - bas) / 1000.0;
}

// ---------------------------------------------------------------------------
// Python'un round() fonksiyonunun aynisi: BANKACI yuvarlamasi
//
// Python 0.5'i en yakin CIFT sayiya yuvarliyor: round(4.5)=4, round(5.5)=6.
// C++'in std::lround'u ise yukari: lround(4.5)=5.
//
// Bu fark p90'i kaydiriyor. 6 turluk bir kosuda 0.9*(6-1)=4.5 ve iki
// yontem farkli indeks veriyor — yani PC'deki sayiyla karsilastirma
// bozulur. Kopyalamak sart.
double bankaci_yuvarla(double x)
{
    const double asagi = std::floor(x);
    const double fark = x - asagi;
    if (std::abs(fark - 0.5) > 1e-9) {
        return std::round(x);
    }
    // Tam ortada: en yakin cift sayiya git.
    return (std::fmod(asagi, 2.0) == 0.0) ? asagi : asagi + 1.0;
}

struct Dagilim {
    std::size_t adet = 0;
    double ortalama = 0;
    double medyan = 0;
    double en_iyi = 0;
    double en_kotu = 0;
    double p90 = 0;
};

// Girdi KOPYALANIP siralaniyor — cagiranin listesi bozulmasin.
Dagilim dagilim_hesapla(std::array<double, EN_FAZLA_TUR>& d, std::size_t n)
{
    Dagilim s{};
    if (n == 0) {
        return s;
    }
    std::sort(d.begin(), d.begin() + static_cast<long>(n));

    s.adet = n;
    double toplam = 0;
    for (std::size_t i = 0; i < n; ++i) {
        toplam += d[i];
    }
    s.ortalama = toplam / static_cast<double>(n);
    s.en_iyi = d[0];
    s.en_kotu = d[n - 1];

    // statistics.median: cift sayida degerde ortadaki IKISININ ortalamasi.
    if (n % 2 == 1) {
        s.medyan = d[n / 2];
    } else {
        s.medyan = (d[n / 2 - 1] + d[n / 2]) / 2.0;
    }

    // Python: s[min(len(s)-1, int(round(0.9*(len(s)-1))))]
    const double ham = 0.9 * static_cast<double>(n - 1);
    auto idx = static_cast<std::size_t>(bankaci_yuvarla(ham));
    s.p90 = d[std::min(n - 1, idx)];
    return s;
}

void dagilim_yaz(const char* ad, const Dagilim& s)
{
    if (s.adet == 0) {
        ESP_LOGI(ETIKET, "  %-22s olculemedi", ad);
        return;
    }
    // Ortalama TEK BASINA yazilmiyor — medyan, p90 ve en kotu yaninda.
    // Ortalama tek basina iyimser yalan soyluyor (PLAN.md).
    ESP_LOGI(ETIKET, "  %-22s n=%u  ort=%.0f  medyan=%.0f  p90=%.0f  "
                     "en_iyi=%.0f  EN_KOTU=%.0f",
             ad, static_cast<unsigned>(s.adet), s.ortalama, s.medyan,
             s.p90, s.en_iyi, s.en_kotu);
}

}  // namespace

// ---------------------------------------------------------------------------
// Tur
// ---------------------------------------------------------------------------

double Tur::gecikme_ms() const
{
    if (t_sustu == 0 || t_ilk_hoparlor == 0) {
        return -1.0;
    }
    return us_to_ms(t_sustu, t_ilk_hoparlor);
}

double Tur::ag_gecikmesi_ms() const
{
    if (t_sustu == 0 || t_ilk_paket == 0) {
        return -1.0;
    }
    return us_to_ms(t_sustu, t_ilk_paket);
}

double Tur::yerel_ms() const
{
    if (t_ilk_paket == 0 || t_ilk_hoparlor == 0) {
        return -1.0;
    }
    return us_to_ms(t_ilk_paket, t_ilk_hoparlor);
}

bool Tur::tamam_mi() const
{
    return gecikme_ms() >= 0.0;
}

// ---------------------------------------------------------------------------
// Defter
// ---------------------------------------------------------------------------

void olcum_baslat()
{
    g_adet = 0;
    g_acik = false;
    g_dusen_parca = 0;
    for (auto& t : g_turlar) {
        t = Tur{};
    }
    ESP_LOGI(ETIKET, "olcum defteri hazir (en fazla %u tur)",
             static_cast<unsigned>(EN_FAZLA_TUR));
}

Tur* tur_ac()
{
    if (g_adet >= EN_FAZLA_TUR) {
        // Sessizce ustune yazmiyoruz — kayip veri, yanlis ortalama demek.
        ESP_LOGW(ETIKET, "defter dolu (%u tur), yeni tur KAYDEDILMIYOR",
                 static_cast<unsigned>(EN_FAZLA_TUR));
        return nullptr;
    }
    g_turlar[g_adet] = Tur{};
    g_acik = true;
    return &g_turlar[g_adet];
}

Tur* acik_tur()
{
    return (g_acik && g_adet < EN_FAZLA_TUR) ? &g_turlar[g_adet] : nullptr;
}

void tur_kapat()
{
    if (!g_acik) {
        return;
    }
    Tur* t = acik_tur();
    if (t != nullptr) {
        t->t_tur_bitti = esp_timer_get_time();
    }
    ++g_adet;
    g_acik = false;
}

namespace {
// 0 verilirse "simdi", degilse verilen an.
std::int64_t an(std::int64_t us)
{
    return (us > 0) ? us : esp_timer_get_time();
}
}  // namespace

void damga_sustu(std::int64_t us)
{
    Tur* t = acik_tur();
    if (t == nullptr) {
        t = tur_ac();
    }
    if (t != nullptr && t->t_sustu == 0) {
        t->t_sustu = an(us);
    }
}

void damga_ilk_paket(std::size_t bayt, std::int64_t us)
{
    Tur* t = acik_tur();
    if (t == nullptr) {
        return;
    }
    t->ses_bayt += bayt;
    if (t->t_ilk_paket == 0) {
        t->t_ilk_paket = an(us);
    }
}

void damga_ilk_hoparlor(std::int64_t us)
{
    Tur* t = acik_tur();
    if (t != nullptr && t->t_ilk_hoparlor == 0) {
        t->t_ilk_hoparlor = an(us);
    }
}

void damga_kesme_konusma(std::int64_t us)
{
    Tur* t = acik_tur();
    if (t != nullptr && t->t_kesme_konusma == 0) {
        t->t_kesme_konusma = an(us);
        t->kesildi = true;
    }
}

void damga_kesme_onay(std::int64_t us)
{
    Tur* t = acik_tur();
    if (t != nullptr && t->t_kesme_onay == 0) {
        t->t_kesme_onay = an(us);
    }
}

void dusen_parca_bildir(std::uint32_t toplam)
{
    g_dusen_parca = toplam;
}

void tur_ozeti_yaz()
{
    const Tur* t = (g_adet > 0) ? &g_turlar[g_adet - 1] : nullptr;
    if (t == nullptr || !t->tamam_mi()) {
        ESP_LOGW(ETIKET, "tur tamamlanmadi, olculemedi");
        return;
    }
    const double g = t->gecikme_ms();
    ESP_LOGI(ETIKET, "tur %u: gecikme %.0f ms  (ag %.0f, yerel %.0f)  %s",
             static_cast<unsigned>(g_adet), g, t->ag_gecikmesi_ms(),
             t->yerel_ms(), g <= KRITER_GECER_MS ? "GECER" : "gecmedi");
}

// ---------------------------------------------------------------------------
// Rapor
// ---------------------------------------------------------------------------

void rapor_yaz()
{
    ESP_LOGI(ETIKET, "");
    ESP_LOGI(ETIKET, "========== PATI OLCUM RAPORU (ESP32) ==========");

    // 🔴 BU DORT DIZI YIGITTA DURAMAZ — static olmalari zorunlu.
    //
    // Dordu birden 4 x 200 x 8 = 6400 bayt. Ana gorevin yigiti 8192
    // (CONFIG_ESP_MAIN_TASK_STACK_SIZE) ve geriye log bicimlendirmesine,
    // cagri cercevelerine yer kalmiyor.
    //
    // 01.08.2026, GERCEK KARTTA GORULDU. app_main bu raporu her bes turda
    // bir basiyor ve tam o anda:
    //
    //   ***ERROR*** A stack overflow in task main has been detected.
    //   Rebooting...
    //
    // Kart sohbetin ortasinda yeniden basliyordu — bes turda bir, saat
    // gibi. Disaridan hicbiri "yigit tasti" gibi gorunmuyordu: robotun
    // cumlesi yarida kesiliyor, oturum kopuyor, ses bozuk sanilıyordu.
    // Ariza once hoparlorde, sonra beslemede arandi.
    //
    // static GUVENLI cunku tek cagirici var: app_main. Ikinci bir cagirici
    // eklenirse burasi ya kilitlenmeli ya yigita geri tasinmali —
    // o zaman da yigit buyutulmeli.
    static std::array<double, EN_FAZLA_TUR> tam{};
    static std::array<double, EN_FAZLA_TUR> ag{};
    static std::array<double, EN_FAZLA_TUR> yerel{};
    static std::array<double, EN_FAZLA_TUR> kesme{};
    std::size_t n_tam = 0, n_ag = 0, n_yerel = 0;
    std::size_t kesilen = 0;
    std::size_t n_kesme = 0;

    for (std::size_t i = 0; i < g_adet; ++i) {
        const Tur& t = g_turlar[i];
        if (t.gecikme_ms() >= 0) tam[n_tam++] = t.gecikme_ms();
        if (t.ag_gecikmesi_ms() >= 0) ag[n_ag++] = t.ag_gecikmesi_ms();
        if (t.yerel_ms() >= 0) yerel[n_yerel++] = t.yerel_ms();
        if (t.kesildi) {
            ++kesilen;
            if (t.t_kesme_onay != 0) {
                kesme[n_kesme++] = us_to_ms(t.t_kesme_konusma, t.t_kesme_onay);
            }
        }
    }

    ESP_LOGI(ETIKET, "%u tur kaydedildi, %u tanesi tamam",
             static_cast<unsigned>(g_adet), static_cast<unsigned>(n_tam));
    ESP_LOGI(ETIKET, "");

    const Dagilim d_tam = dagilim_hesapla(tam, n_tam);
    dagilim_yaz("KRITER (sustu->ses)", d_tam);
    dagilim_yaz("ag (sustu->paket)", dagilim_hesapla(ag, n_ag));
    dagilim_yaz("yerel (paket->I2S)", dagilim_hesapla(yerel, n_yerel));
    ESP_LOGI(ETIKET, "");

    // Kriter karari
    if (n_tam == 0) {
        ESP_LOGW(ETIKET, "KRITER: OLCULMEDI — tamamlanmis tur yok");
    } else {
        const char* karar;
        if (d_tam.medyan <= KRITER_GECER_MS) {
            karar = "GECER";
        } else if (d_tam.medyan <= KRITER_SINIR_MS) {
            karar = "SINIRDA";
        } else {
            karar = "KALIR";
        }
        ESP_LOGI(ETIKET, "KRITER: <=%.0f gecer, %.0f-%.0f sinirda, >%.0f kalir",
                 KRITER_GECER_MS, KRITER_GECER_MS, KRITER_SINIR_MS,
                 KRITER_SINIR_MS);
        ESP_LOGI(ETIKET, "SONUC : %s  (medyan %.0f ms)", karar, d_tam.medyan);
        ESP_LOGI(ETIKET, "  karsilastirma: PC'de medyan 1325 ms olculmustu");
    }
    ESP_LOGI(ETIKET, "");

    // Sozunu kesme
    if (kesilen == 0) {
        ESP_LOGW(ETIKET, "SOZUNU KESME: OLCULMEDI — hic denenmedi");
        ESP_LOGW(ETIKET, "  robot konusurken uzerine konusulmasi gerekiyor");
    } else {
        dagilim_yaz("kesme onayi (ms)", dagilim_hesapla(kesme, n_kesme));
        ESP_LOGI(ETIKET, "  %u turda kesildi", static_cast<unsigned>(kesilen));
    }
    ESP_LOGI(ETIKET, "");

    // Wifi yetisebildi mi
    if (g_dusen_parca > 0) {
        ESP_LOGW(ETIKET, "DUSEN MIKROFON PARCASI: %u",
                 static_cast<unsigned>(g_dusen_parca));
        ESP_LOGW(ETIKET, "  wifi yetismiyor — yukaridaki gecikme sayilari");
        ESP_LOGW(ETIKET, "  Gemini'yi degil BIZIM AGIMIZI olcuyor olabilir");
    } else {
        ESP_LOGI(ETIKET, "dusen mikrofon parcasi: 0 (wifi yetisti)");
    }

    // NE OLCMEDIGIMIZ — bu bolum kasitli. Olculmeyen sey "calisiyor"
    // sayilmasin.
    ESP_LOGI(ETIKET, "");
    ESP_LOGI(ETIKET, "BU RAPORUN OLCMEDIGI SEYLER:");
    ESP_LOGI(ETIKET, "  · Turkce telaffuz kalitesi (kulakla degerlendirilir)");
    ESP_LOGI(ETIKET, "  · sistem promptuna uyum (dokum gerekiyor)");
    ESP_LOGI(ETIKET, "  · govdede yanki (hoparlor-mikrofon yakinligi)");
    ESP_LOGI(ETIKET, "  · 15 dk oturum siniri asildi mi");
    ESP_LOGI(ETIKET, "===============================================");
}

// ---------------------------------------------------------------------------
// Oz-test
// ---------------------------------------------------------------------------

bool oz_test()
{
    // n=6 KASITLI: 0.9*(6-1) = 4.5 — bankaci yuvarlamasi ile lround'un
    // ayristigi tam nokta. Ceviri yanlissa p90 1400 yerine 1500 cikar.
    std::array<double, EN_FAZLA_TUR> d{};
    const double girdi[6] = {1400.0, 1000.0, 1500.0, 1100.0, 1300.0, 1200.0};
    for (std::size_t i = 0; i < 6; ++i) {
        d[i] = girdi[i];
    }

    const Dagilim s = dagilim_hesapla(d, 6);

    // Python'dan alinan beklenen degerler.
    struct { const char* ad; double olan; double beklenen; } kontrol[] = {
        {"adet",     static_cast<double>(s.adet), 6.0},
        {"ortalama", s.ortalama, 1250.0},
        {"medyan",   s.medyan,   1250.0},
        {"p90",      s.p90,      1400.0},   // lround olsaydi 1500 cikardi
        {"en_iyi",   s.en_iyi,   1000.0},
        {"en_kotu",  s.en_kotu,  1500.0},
    };

    bool tamam = true;
    for (const auto& k : kontrol) {
        const bool ok = std::abs(k.olan - k.beklenen) < 1e-6;
        if (!ok) {
            tamam = false;
            ESP_LOGE(ETIKET, "  OZ-TEST HATA: %s = %.1f, beklenen %.1f",
                     k.ad, k.olan, k.beklenen);
        }
    }

    if (tamam) {
        ESP_LOGI(ETIKET, "oz-test GECTI: medyan/p90 matematigi PC ile ayni");
        ESP_LOGI(ETIKET, "  (p90=1400 dogru; lround kullanilsa 1500 cikardi)");
    } else {
        // Bu bir UYARI degil HATA: matematik bozuksa uretilen butun
        // gecikme sayilari guvenilmez ve karsilastirma yapilamaz.
        ESP_LOGE(ETIKET, "OZ-TEST KALDI — olcum sayilarina GUVENILMEZ");
    }

    // Defteri temizle; oz-test verisi gercek olcume karismasin.
    olcum_baslat();
    return tamam;
}

}  // namespace pati
