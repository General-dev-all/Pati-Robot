#include "pati_ses.hpp"

#include <algorithm>
#include <array>

#include <driver/i2s_std.h>
#include <esp_log.h>

#include "pati_pinler.h"

namespace pati {
namespace {

constexpr const char* ETIKET = "pati.ses";

i2s_chan_handle_t g_mik = nullptr;
i2s_chan_handle_t g_hop = nullptr;

float g_seviye = SES_SEVIYESI_BASLANGIC;

// Hoparlorun SU ANKI calma frekansi. Kaynak her zaman 24 kHz;
// bu ondan buyukse ses tiz ve hizli caliyor (bkz. hoparlor_hiz_ayarla).
std::uint32_t g_cikis_hz = PATI_HOP_HZ;

// INMP441'in ham 32 bitlik verisi icin tampon.
//
// Neden static ve neden bu boyutta: her okuma cagrisinda yigitta 1.3 KB
// ayirmak istemiyoruz — bu fonksiyon saniyede 50 kez cagriliyor. Tek
// gorevden cagrildigi icin paylasimi guvenli (ses gorevi birden fazla
// olmayacak; olursa burasi kilitlenmeli).
std::array<std::int32_t, PATI_OKUMA_ORNEK> g_ham{};

// YUMUSAK SINIRLAYICI — 1.0 ustu ses seviyesini mumkun kilan sey.
//
// Esigin ALTINDA hicbir sey yapmiyor: sesin buyuk kismi dokunulmadan
// geciyor. Ustunde artan bir oranla sikisip 32767'ye ASIMPTOT olarak
// yaklasiyor, yani girdi ne kadar buyurse buyusun cikti tavani ASMIYOR.
//
// NEDEN DUZ KIRPMA DEGIL: kirpma dalga tepesini duz keser ve bu, kulaga
// catirti gibi gelir — cocuk icin en rahatsiz edici bozulma bicimi.
// Asimptotik sikisma tepeyi YUVARLAR; yuksek seviyede hafif bir
// tokluk birakir, catirti birakmaz.
//
// Esik 28000 (~0,85 tam genlik) bilincli: konusmanin buyuk bolumu
// bunun altinda kaliyor ve hic sekillendirilmiyor.
constexpr float SINIR_ESIK  = 28000.0f;
constexpr float SINIR_TAVAN = 32767.0f;

inline std::int16_t yumusak_sinirla(float v)
{
    const float a = (v < 0.0f) ? -v : v;
    if (a <= SINIR_ESIK) {
        return static_cast<std::int16_t>(v);
    }
    constexpr float PAY = SINIR_TAVAN - SINIR_ESIK;
    const float fazla = a - SINIR_ESIK;
    const float y = SINIR_ESIK + PAY * (fazla / (fazla + PAY));
    return static_cast<std::int16_t>((v < 0.0f) ? -y : y);
}

}  // namespace

// ---------------------------------------------------------------------------
// Mikrofon
// ---------------------------------------------------------------------------

esp_err_t mikrofon_baslat()
{
    i2s_chan_config_t kanal = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    // Kuyruk derinligi: 6 x 240 kare varsayilan. 16 kHz'de ~90 ms tampon
    // demek. Wifi yuzunden bir gorev geciktiginde ses kaybetmemek icin
    // varsayilan yeterli; artirirsak gecikme artar.
    esp_err_t hata = i2s_new_channel(&kanal, nullptr, &g_mik);
    if (hata != ESP_OK) {
        ESP_LOGE(ETIKET, "mikrofon kanali acilamadi: %s", esp_err_to_name(hata));
        return hata;
    }

    // ⚠️ 32 BIT isteniyor, 16 degil. INMP441 24 bitlik veriyi 32 bitlik
    // yuvanin soluna yaziyor. 16 bit istenirse ses ya cok kisik ya
    // gurultu cikiyor — I2S mikrofonlarinda en sik hata bu.
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(PATI_MIK_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,   // INMP441 ana saat istemiyor
            .bclk = PATI_MIK_SCK,
            .ws   = PATI_MIK_WS,
            .dout = I2S_GPIO_UNUSED,   // mikrofon sadece okunuyor
            .din  = PATI_MIK_SD,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };

    // SOL kanal: modulun L/R pini GND'ye bagli oldugu icin veri solda.
    // L/R bosta kalirsa bu esleme tutmaz ve hic veri gelmez — kablolama
    // tuzagi #3 tam bu.
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    hata = i2s_channel_init_std_mode(g_mik, &std_cfg);
    if (hata != ESP_OK) {
        ESP_LOGE(ETIKET, "mikrofon std kurulumu: %s", esp_err_to_name(hata));
        return hata;
    }

    hata = i2s_channel_enable(g_mik);
    if (hata != ESP_OK) {
        ESP_LOGE(ETIKET, "mikrofon etkinlestirilemedi: %s", esp_err_to_name(hata));
        return hata;
    }

    ESP_LOGI(ETIKET, "mikrofon hazir: %d Hz, 32 bit yuva -> int16, "
                     "SCK=%d WS=%d SD=%d",
             PATI_MIK_HZ, PATI_MIK_SCK, PATI_MIK_WS, PATI_MIK_SD);
    return ESP_OK;
}

size_t mikrofon_oku(std::span<std::int16_t> hedef, uint32_t timeout_ms)
{
    if (g_mik == nullptr || hedef.empty()) {
        return 0;
    }

    const size_t istenen = std::min(hedef.size(), g_ham.size());
    size_t okunan_bayt = 0;

    const esp_err_t hata = i2s_channel_read(
        g_mik, g_ham.data(), istenen * sizeof(std::int32_t),
        &okunan_bayt, timeout_ms);

    if (hata != ESP_OK && hata != ESP_ERR_TIMEOUT) {
        ESP_LOGW(ETIKET, "mikrofon okuma: %s", esp_err_to_name(hata));
        return 0;
    }

    const size_t ornek = okunan_bayt / sizeof(std::int32_t);

    // 32 bitlik yuvadan int16'ya indir.
    //
    // INMP441'in 24 bitlik verisi yuvanin ust bitlerinde. 16 bit istiyoruz,
    // yani 16 bit saga kaydirmak gerekiyor.
    //
    // NEDEN 11 DEGIL 16: bazi ornek kodlar 11-14 bit kaydirip "ses daha
    // gur olsun" diyor — o kazanc degil KIRPMA. 16 bit kaydirmak orneklerin
    // gercek genligini koruyor; ses kisikligi varsa cozumu amfinin GAIN
    // pini ya da yazilim carpani, kaydirmayla oynamak degil.
    for (size_t i = 0; i < ornek; ++i) {
        hedef[i] = static_cast<std::int16_t>(g_ham[i] >> 16);
    }
    return ornek;
}

// ---------------------------------------------------------------------------
// Hoparlor
// ---------------------------------------------------------------------------

esp_err_t hoparlor_baslat()
{
    i2s_chan_config_t kanal = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    // auto_clear: kuyruk bosaldiginda DMA eski veriyi tekrar calmasin.
    // Kapali olsa robot sustugunda son parca vizilti gibi tekrarliyor.
    kanal.auto_clear_after_cb = true;

    // DMA TAMPONU — varsayilan 6 x 240 kare, yani 24 kHz'de yalnizca
    // 60 ms. Ses wifi uzerinden gercek zamanli akiyor ve ag her zaman
    // duzgun aralikli paket vermiyor; 60 ms'lik bir gecikme tamponu
    // kurutuyor ve konusmanin ortasinda bosluk duyuluyor.
    //
    // 🔴 TAMPON GEMINI'NIN PARCA BOYUNA GORE SECILDI, tahminle degil.
    //
    // 23.08.2026'da gercek kartta olculdu: Gemini ses parcalarini
    // 200-280 ms'lik bloklar halinde gonderiyor (uc turda 24/4820,
    // 27/7600, 40/11160 ms). Onceki 200 ms'lik tampon, TEK BIR PARCA
    // kadardi — yani marj sifirdi ve bir parca gec kalinca aninda
    // bosluk duyuluyordu. Olculen tur 1: parca arasi 404 ms, ses acigi
    // 262 ms.
    //
    // 32 x 512 = 16.384 kare = 683 ms kaynak (calmada ~525 ms), yani
    // yaklasik UC parcalik marj.
    //
    // Ilk sesin gecikmesini ARTIRMIYOR: tampon bir ust sinir, sabit bir
    // bekleme degil — ilk parca DMA'ya verilir verilmez calmaya
    // basliyor. Sozunu kesmeyi de bozmuyor: hoparlor_temizle() kanali
    // kapatip aciyor ve tamponu boyu ne olursa olsun ANINDA bosaltiyor.
    //
    // Bedeli ~32 KB ic RAM. Olcumde bos dahili SRAM 289.791 bayt'ti.
    kanal.dma_desc_num = 32;
    kanal.dma_frame_num = 512;

    esp_err_t hata = i2s_new_channel(&kanal, &g_hop, nullptr);
    if (hata != ESP_OK) {
        ESP_LOGE(ETIKET, "hoparlor kanali acilamadi: %s", esp_err_to_name(hata));
        return hata;
    }

    // MAX98357 16 bit istiyor; Gemini de 24 kHz int16 gonderiyor, tam uyuyor.
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(PATI_HOP_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PATI_AMFI_BCLK,
            .ws   = PATI_AMFI_LRC,
            .dout = PATI_AMFI_DIN,
            .din  = I2S_GPIO_UNUSED,   // amfiden okuma yok
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };

    hata = i2s_channel_init_std_mode(g_hop, &std_cfg);
    if (hata != ESP_OK) {
        ESP_LOGE(ETIKET, "hoparlor std kurulumu: %s", esp_err_to_name(hata));
        return hata;
    }

    hata = i2s_channel_enable(g_hop);
    if (hata != ESP_OK) {
        ESP_LOGE(ETIKET, "hoparlor etkinlestirilemedi: %s", esp_err_to_name(hata));
        return hata;
    }

    ESP_LOGI(ETIKET, "hoparlor hazir: %d Hz, 16 bit, BCLK=%d LRC=%d DIN=%d",
             PATI_HOP_HZ, PATI_AMFI_BCLK, PATI_AMFI_LRC, PATI_AMFI_DIN);
    return ESP_OK;
}

esp_err_t hoparlor_hiz_ayarla(float carpan)
{
    if (g_hop == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    // Sinirlar panelinkiyle ayni olsun diye burada da kirpiliyor: panel
    // bozulsa bile robot anlasilmaz bir hizda konusmasin.
    const float c = std::clamp(carpan, 0.80f, 1.60f);
    const std::uint32_t hz =
        static_cast<std::uint32_t>(PATI_HOP_HZ * c + 0.5f);
    if (hz == g_cikis_hz) {
        return ESP_OK;
    }

    // Saat yeniden kurulurken kanal KAPALI olmak zorunda (IDF sarti).
    // Bu, o an calan sesi kesiyor — acilista sorun degil, panelden
    // ayar degistiginde de bir kerelik.
    esp_err_t hata = i2s_channel_disable(g_hop);
    if (hata != ESP_OK) {
        ESP_LOGW(ETIKET, "hiz icin kanal kapatilamadi: %s",
                 esp_err_to_name(hata));
        return hata;
    }

    i2s_std_clk_config_t saat = I2S_STD_CLK_DEFAULT_CONFIG(hz);
    hata = i2s_channel_reconfig_std_clock(g_hop, &saat);
    if (hata == ESP_OK) {
        g_cikis_hz = hz;
    } else {
        ESP_LOGW(ETIKET, "calma hizi kurulamadi: %s", esp_err_to_name(hata));
    }

    // Basarisiz olsa bile kanali GERI ACIYORUZ: eski hizla calmak,
    // hic calmamaktan iyi.
    const esp_err_t acma = i2s_channel_enable(g_hop);
    if (acma != ESP_OK) {
        ESP_LOGE(ETIKET, "kanal geri acilamadi: %s", esp_err_to_name(acma));
        return acma;
    }

    ESP_LOGI(ETIKET, "calma hizi: %.2fx -> %lu Hz (kaynak %d Hz)",
             static_cast<double>(c), static_cast<unsigned long>(g_cikis_hz),
             PATI_HOP_HZ);
    return hata;
}

size_t hoparlor_yaz(std::span<const std::int16_t> kaynak, uint32_t timeout_ms)
{
    if (g_hop == nullptr || kaynak.empty()) {
        return 0;
    }

    // Ses seviyesi 1.0'da HIC IS YAPMIYORUZ — kaynagi oldugu gibi
    // geciriyoruz. Olcum kosulari tam seviyede yapiliyor ve o kosularda
    // buraya tek bir kopyalama bile eklenmesin (prototype/ses.py'de ayni
    // gerekce yazili).
    size_t yazilan_bayt = 0;
    esp_err_t hata;

    // 🔴 KOSUL "== 1.0", ">= 1.0" DEGIL. Tavan 1.0 iken ikisi ayni
    // seydi; tavan 2.50 olunca ">=" seviyeyi TAMAMEN yok sayardi —
    // panelden sesi acmak hicbir sey yapmazdi.
    if (g_seviye > 0.999f && g_seviye < 1.001f) {
        hata = i2s_channel_write(g_hop, kaynak.data(),
                                 kaynak.size() * sizeof(std::int16_t),
                                 &yazilan_bayt, timeout_ms);
        if (hata != ESP_OK && hata != ESP_ERR_TIMEOUT) {
            ESP_LOGW(ETIKET, "hoparlor yazma: %s", esp_err_to_name(hata));
            return 0;
        }
        return yazilan_bayt / sizeof(std::int16_t);
    }

    // 🔴 PARCANIN TAMAMI YAZILIYOR — DONGU SART.
    //
    // 23.08.2026'da bulundu: onceki surum tek bir 1024 ornekli tampon
    // dolduruyor, `std::min` ile kirpiyor ve GERISINI SESSIZCE ATIYORDU.
    // Cagiran (pati_sohbet.cpp) donus degerine bakmadigi icin kayip
    // hicbir yere yazilmiyordu da.
    //
    // 1024 ornek = 24 kHz'de 42 ms. Yorum "Gemini ~20-40 ms gonderiyor"
    // diyordu ve tam sinirdaydi — bir tik uzun gelen her parcanin kuyrugu
    // kayboluyordu. Varsayilan ses seviyesi 0.85, yani bu yol HER ZAMAN
    // calisiyor; sorun 1.0'da gorunmuyor ve o yuzden olcumlerde de
    // gorunmemisti.
    //
    // Belirti: Pati konusurken cumlenin icinde kucuk bosluklar.
    std::array<std::int16_t, 1024> olcekli{};
    size_t toplam_bayt = 0;

    for (size_t bas = 0; bas < kaynak.size(); bas += olcekli.size()) {
        const size_t n = std::min(olcekli.size(), kaynak.size() - bas);
        for (size_t i = 0; i < n; ++i) {
            olcekli[i] = yumusak_sinirla(
                static_cast<float>(kaynak[bas + i]) * g_seviye);
        }

        size_t bayt = 0;
        hata = i2s_channel_write(g_hop, olcekli.data(), n * sizeof(std::int16_t),
                                 &bayt, timeout_ms);
        toplam_bayt += bayt;

        if (hata != ESP_OK && hata != ESP_ERR_TIMEOUT) {
            ESP_LOGW(ETIKET, "hoparlor yazma: %s", esp_err_to_name(hata));
            break;
        }
        // Zaman asimi: kuyruk dolu ve bekleme suresi doldu. Kalan
        // parcayi zorlamak gecikmeyi buyutur, birakmak daha dogru —
        // ama artik SESSIZCE degil, donus degerinde gorunerek.
        if (bayt < n * sizeof(std::int16_t)) {
            break;
        }
    }

    return toplam_bayt / sizeof(std::int16_t);
}

esp_err_t hoparlor_temizle()
{
    if (g_hop == nullptr) {
        return ESP_OK;
    }
    // Kanali kapatip acmak DMA kuyrugunu bosaltiyor. Barge-in'de
    // beklenen davranis: robot ANINDA susuyor.
    esp_err_t hata = i2s_channel_disable(g_hop);
    if (hata == ESP_OK) {
        hata = i2s_channel_enable(g_hop);
    }
    return hata;
}

// ---------------------------------------------------------------------------
// Ses seviyesi
// ---------------------------------------------------------------------------

float ses_seviyesi_ayarla(float yeni)
{
    g_seviye = std::clamp(yeni, SES_SEVIYESI_EN_AZ, SES_SEVIYESI_EN_FAZLA);
    return g_seviye;
}

float ses_seviyesi_degistir(float adim)
{
    return ses_seviyesi_ayarla(g_seviye + adim);
}

float ses_seviyesi()
{
    return g_seviye;
}

}  // namespace pati
