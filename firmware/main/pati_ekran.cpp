#include "pati_ekran.hpp"

#include <driver/ledc.h>

#include <cstring>
#include <new>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>

#include "pati_pinler.h"

namespace pati {
namespace {

constexpr const char* ETIKET = "ekran";

// ---------------------------------------------------------------------------
// IKI VARSAYIM — ilk acilista test deseniyle cozulecek
// ---------------------------------------------------------------------------
//
// Bu iki ayari BILMIYORUZ. Modulun veri sayfasi elimizde degil ve
// "genelde boyledir" demek tam olarak bu projede iki kez hataya yol
// acti. O yuzden ikisi de tek satirlik anahtar ve `ekran_test_deseni()`
// hangisinin yanlis oldugunu gosteriyor.

// 1. BAYT SIRASI. ST7789 16 bitlik pikseli once yuksek bayt bekliyor
//    (big-endian). ESP32 little-endian, yani `uint16_t` dogrudan
//    gonderilirse baytlar ters gidiyor ve renkler karisiyor —
//    kirmizi ile mavi yer degistirmis gorunur.
//
//    BELIRTI: test deseninde kirmizi cubuk MAVI, mavi cubuk KIRMIZI
//             gorunuyorsa bu deger yanlis.
constexpr bool BAYT_CEVIR = true;

// 2. RENK TERSLIGI. Cok sayida ST7789 IPS paneli "inverted" tipte
//    kullaniyor ve ST7789'un INVON komutu gerekiyor.
//
//    BELIRTI: test deseninde siyah BEYAZ, beyaz SIYAH gorunuyorsa
//             (ya da butun renkler negatif) bu deger yanlis.
constexpr bool RENK_TERS = true;

// 3. YON. Panel fiziksel olarak DIKEY (135 genis, 240 yuksek) ama Pati
//    YATAY kullaniyor: iki goz yan yana 178 piksel yer kapliyor ve
//    135'e sigmiyor. Cevirince 240 genislik geliyor.
//
//    swap_xy satirla sutunu degistiriyor, mirror da hangi kenarin
//    yukarida kaldigini seciyor.
//
//    BELIRTI: goruntu BASASAGI ya da AYNADA gibiyse asagidaki iki
//             degerden biri yanlis. Dortlu kombinasyonun biri dogru;
//             once EKR_AYNA_X'i cevirmeyi deneyin.
constexpr bool EKR_CEVIR  = true;   // swap_xy
constexpr bool EKR_AYNA_X = true;
constexpr bool EKR_AYNA_Y = false;

// 4. PIKSEL KAYMASI. ST7789P3'un cerceve bellegi 240x320 ama panel
//    135x240 — goruntu bellegin ORTASINA dusuyor, yani bir kayma var.
//
//    Dikey kullanimda sutun kaymasi (240-135)/2 = 52, satir kaymasi
//    (320-240)/2 = 40. YATAYA cevirince ikisi YER DEGISTIRIYOR.
//
//    BELIRTI: goruntu birkac piksel kayik duruyor ve bir kenarda ince
//             bir cop/gurultu seridi goruluyorsa bu iki sayi yanlis.
//             Ikisini takas etmek ilk denenecek sey.
constexpr int EKR_KAYMA_X = PATI_EKR_KAYMA_X;  // 40
constexpr int EKR_KAYMA_Y = PATI_EKR_KAYMA_Y;  // 52

// ---------------------------------------------------------------------------

// SPI2. SPI3 de var ama SPI2'nin IOMUX pinleri kullaniliyor (bkz.
// pati_pinler.h), yani sinyal GPIO matrisinden gecmiyor.
constexpr spi_host_device_t SPI_YUVA = SPI2_HOST;

esp_lcd_panel_io_handle_t g_io = nullptr;
esp_lcd_panel_handle_t g_panel = nullptr;

// Iki serit tamponu, sirayla kullaniliyor (bkz. baslik dosyasi).
std::uint16_t* g_serit[2] = {nullptr, nullptr};
int g_sira = 0;

bool g_hazir = false;

constexpr size_t SERIT_ORNEK = PATI_EKR_G * EKRAN_SERIT_YUKSEK;
constexpr size_t SERIT_BAYT = SERIT_ORNEK * sizeof(std::uint16_t);

}  // namespace

// ---------------------------------------------------------------------------

std::uint16_t ekran_renk(std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    const std::uint16_t v = static_cast<std::uint16_t>(
        ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
    if constexpr (BAYT_CEVIR) {
        return static_cast<std::uint16_t>((v >> 8) | (v << 8));
    }
    return v;
}

std::uint16_t* ekran_serit()
{
    return g_serit[g_sira];
}

bool ekran_hazir()
{
    return g_hazir;
}

esp_err_t ekran_serit_bas(int x0, int y0, int gen, int yuk)
{
    if (!g_hazir || gen <= 0 || yuk <= 0) {
        return ESP_ERR_INVALID_STATE;
    }
    // x_end / y_end DISARIDA KALIYOR — ESP-IDF dokumaninda acikca
    // yaziyor: "x_start is included", "x_end is not included". Bir
    // eksik/fazla piksel burada olur ve ekranda tek sutunluk cizgi
    // olarak gorunur.
    const esp_err_t s = esp_lcd_panel_draw_bitmap(
        g_panel, x0, y0, x0 + gen, y0 + yuk, g_serit[g_sira]);

    // Tamponu degistir: gonderilmekte olanin uzerine yazmayalim.
    g_sira ^= 1;
    return s;
}

esp_err_t ekran_doldur(std::uint16_t renk)
{
    if (!g_hazir) {
        return ESP_ERR_INVALID_STATE;
    }
    // 🔴 BIR SATIR TASIRARAK SILIYORUZ (PATI_EKR_Y + 1).
    //
    // Panelin kaymasi TAM SAYI DEGIL: cerceve bellegi 240 genis, panel
    // 135, yani (240-135)/2 = 52,5. Kodda 52 yaziyor ve panelin
    // gercekte 52'den mi 53'ten mi basladigi olculmedi.
    //
    // 53'ten basliyorsa bizim yazdigimiz son satir (bellekte 186)
    // panelin sondan bir onceki satiri oluyor ve 187 HIC YAZILMIYOR —
    // orada acilistan kalan ne varsa ekranda oylece kaliyor.
    //
    // 02.09.2026'da tam bu gorundu: gozlerin kenarinda ince, renkli
    // noktacikli bir cizgi. Renkler eski test deseninin o satirdaki
    // kesitiydi (kirmizi-yesil-mavi-beyaz-siyah-turkuaz cubuklar).
    //
    // Bir satir tasirmak iki olasiligi da kapatiyor: panel 52'den
    // basliyorsa fazladan yazilan satir zaten gorunmuyor, 53'ten
    // basliyorsa acikta kalan satir da siliniyor. Bu fonksiyon acilista
    // bir kez cagriliyor, yani bedeli yok.
    //
    // Kayma bir gun kesin olcusuyle ogrenilirse (pati_pinler.h,
    // PATI_EKR_KAYMA_Y) bu tasma kaldirilabilir.
    constexpr int SILINECEK_Y = PATI_EKR_Y + 1;

    for (int y = 0; y < SILINECEK_Y; y += EKRAN_SERIT_YUKSEK) {
        const int yuk = (y + EKRAN_SERIT_YUKSEK <= SILINECEK_Y)
                            ? EKRAN_SERIT_YUKSEK
                            : (SILINECEK_Y - y);
        std::uint16_t* p = ekran_serit();
        for (size_t i = 0; i < static_cast<size_t>(PATI_EKR_G) * yuk; ++i) {
            p[i] = renk;
        }
        const esp_err_t s = ekran_serit_bas(0, y, PATI_EKR_G, yuk);
        if (s != ESP_OK) {
            return s;
        }
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Arka isik — kisilebilir
// ---------------------------------------------------------------------------
//
// 🔴 EN BUYUK SABIT AKIM MUSTERISI. Brownout'u yapan sey hoparlorun
// ANLIK tepe akimi, ama tepe her zaman TABAN akimin uzerine biniyor:
// taban ne kadar dususe ray o kadar yuksekte durur ve tepe icin o kadar
// pay kalir. Yani arka isigi kismak, sesi kismadan ayni emniyeti
// saglamanin yolu (02.09.2026 tartismasi, TESHIS.md).
//
// Eskiden burasi duz `gpio_set_level` idi: ya tam parlak ya kapali, ara
// kademe yoktu. LEDC ile PWM'e cevrildi.
//
// FREKANS 20 kHz. Iki sebep: kulagin ustunde (arka isik surucusu
// duyulabilir bir vizilti yapmasin — hoparlorun yaninda duran bir
// cihazda bu gercek bir risk) ve gozun cok ustunde (titreme gorunmez).
//
// COZUNURLUK 10 bit: 0-1023 kademe, gozle ayirt edilemeyecek kadar ince
// ve LEDC'nin 20 kHz'de rahat verebildigi bir cozunurluk.
constexpr ledc_timer_t ISIK_ZAMANLAYICI = LEDC_TIMER_1;
constexpr ledc_channel_t ISIK_KANAL = LEDC_CHANNEL_1;
constexpr int ISIK_BIT = 10;
constexpr int ISIK_EN_COK = (1 << ISIK_BIT) - 1;

float g_parlaklik = 1.0f;   // istenen seviye
bool g_isik_acik = false;   // ekran_arka_isik(true/false)
bool g_ledc_hazir = false;

void isik_uygula()
{
    if (!g_ledc_hazir) {
        // LEDC kurulamadiysa duz GPIO'ya duserek YINE DE calis: kisik
        // olmayan bir ekran, hic olmayan ekrandan iyidir.
        gpio_set_level(PATI_EKR_BLK, g_isik_acik ? 1 : 0);
        return;
    }
    const int duty =
        g_isik_acik
            ? static_cast<int>(g_parlaklik * static_cast<float>(ISIK_EN_COK))
            : 0;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, ISIK_KANAL,
                  static_cast<std::uint32_t>(duty));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, ISIK_KANAL);
}

void ekran_arka_isik(bool ac)
{
    g_isik_acik = ac;
    isik_uygula();
}

float ekran_parlaklik_ayarla(float yeni)
{
    // 0.15 taban: daha asagisi kararan degil, KAPANAN bir ekran gibi
    // gorunuyor ve cocuk Pati'yi bozuk sanir.
    g_parlaklik = (yeni < 0.15f) ? 0.15f : (yeni > 1.0f ? 1.0f : yeni);
    isik_uygula();
    return g_parlaklik;
}

float ekran_parlaklik() { return g_parlaklik; }

// ---------------------------------------------------------------------------

void ekran_test_deseni()
{
    if (!g_hazir) {
        return;
    }

    // Alti dikey cubuk. Sirasi kasten boyle: kirmizi ile mavi UCLARDA
    // duruyor ki bayt cevirmesi yanlissa yer degistirdikleri hemen
    // gorulsun.
    struct Cubuk {
        const char* ad;
        std::uint8_t r, g, b;
    };
    constexpr Cubuk CUBUK[6] = {
        {"KIRMIZI", 255, 0, 0},
        {"YESIL", 0, 255, 0},
        {"MAVI", 0, 0, 255},
        {"BEYAZ", 255, 255, 255},
        {"SIYAH", 0, 0, 0},
        {"TURKUAZ", 0x17, 0xc4, 0xc4},
    };
    constexpr int GENISLIK = PATI_EKR_G / 6;

    ESP_LOGW(ETIKET, "");
    ESP_LOGW(ETIKET, "=== EKRAN TEST DESENI — SOLDAN SAGA NE GORMEN GEREKIYOR ===");
    ESP_LOGW(ETIKET, "  1. KIRMIZI   2. YESIL   3. MAVI");
    ESP_LOGW(ETIKET, "  4. BEYAZ     5. SIYAH   6. TURKUAZ (gozlerin rengi)");
    ESP_LOGW(ETIKET, "");
    ESP_LOGW(ETIKET, "  Kirmizi ile mavi YER DEGISTIRMISSE : BAYT_CEVIR yanlis");
    ESP_LOGW(ETIKET, "  Siyah ile beyaz YER DEGISTIRMISSE  : RENK_TERS yanlis");
    ESP_LOGW(ETIKET, "  Cubuklar YATAY duruyorsa           : EKR_CEVIR yanlis");
    ESP_LOGW(ETIKET, "  Sira TERSTEN (turkuaz solda) ise   : EKR_AYNA_X yanlis");
    ESP_LOGW(ETIKET, "  Kenarda ince cop seridi varsa      : EKR_KAYMA_X/Y yanlis");
    ESP_LOGW(ETIKET, "  Hicbir sey gorunmuyorsa            : L3B gucu ya da BLK");
    ESP_LOGW(ETIKET, "  Su an: BAYT_CEVIR=%d  RENK_TERS=%d",
             BAYT_CEVIR ? 1 : 0, RENK_TERS ? 1 : 0);
    ESP_LOGW(ETIKET, "  Hepsi pati_ekran.cpp basinda, tek satir.");
    ESP_LOGW(ETIKET, "");

    for (int y = 0; y < PATI_EKR_Y; y += EKRAN_SERIT_YUKSEK) {
        const int yuk = (y + EKRAN_SERIT_YUKSEK <= PATI_EKR_Y)
                            ? EKRAN_SERIT_YUKSEK
                            : (PATI_EKR_Y - y);
        std::uint16_t* p = ekran_serit();
        for (int sy = 0; sy < yuk; ++sy) {
            for (int x = 0; x < PATI_EKR_G; ++x) {
                const int i = (x / GENISLIK < 6) ? (x / GENISLIK) : 5;
                p[sy * PATI_EKR_G + x] =
                    ekran_renk(CUBUK[i].r, CUBUK[i].g, CUBUK[i].b);
            }
        }
        ekran_serit_bas(0, y, PATI_EKR_G, yuk);
    }
}

// ---------------------------------------------------------------------------

esp_err_t ekran_baslat()
{
    if (g_hazir) {
        return ESP_OK;
    }

    // --- arka isik pini
    //
    // ONCE bunu kuruyoruz ve KAPALI baslatiyoruz: panel kurulurken
    // ekranda rastgele gurultu oluyor ve acik isikla bu "bozuk geldi"
    // gorunumu veriyor.
    gpio_config_t isik{};
    isik.pin_bit_mask = 1ULL << PATI_EKR_BLK;
    isik.mode = GPIO_MODE_OUTPUT;
    if (gpio_config(&isik) != ESP_OK) {
        ESP_LOGE(ETIKET, "arka isik pini kurulamadi (GPIO%d)", PATI_EKR_BLK);
        return ESP_FAIL;
    }

    // PWM kanali. Basarisiz olursa OLUMCUL DEGIL: isik_uygula() duz
    // GPIO'ya duser ve ekran tam parlak calisir. Kisilamamak, ekransiz
    // kalmaktan iyidir.
    ledc_timer_config_t zam{};
    zam.speed_mode = LEDC_LOW_SPEED_MODE;
    zam.timer_num = ISIK_ZAMANLAYICI;
    zam.duty_resolution = static_cast<ledc_timer_bit_t>(ISIK_BIT);
    zam.freq_hz = 20000;
    zam.clk_cfg = LEDC_AUTO_CLK;

    ledc_channel_config_t kan{};
    kan.gpio_num = PATI_EKR_BLK;
    kan.speed_mode = LEDC_LOW_SPEED_MODE;
    kan.channel = ISIK_KANAL;
    kan.timer_sel = ISIK_ZAMANLAYICI;
    kan.duty = 0;
    kan.hpoint = 0;

    if (ledc_timer_config(&zam) == ESP_OK &&
        ledc_channel_config(&kan) == ESP_OK) {
        g_ledc_hazir = true;
    } else {
        ESP_LOGW(ETIKET, "arka isik PWM kurulamadi — kisilamayacak");
    }

    ekran_arka_isik(false);

    // --- DMA-uygun serit tamponlari
    //
    // MALLOC_CAP_DMA: ic RAM ve soz hizali. PSRAM'den DMA yapilmaz
    // (bkz. baslik dosyasi).
    for (int i = 0; i < 2; ++i) {
        g_serit[i] = static_cast<std::uint16_t*>(
            heap_caps_malloc(SERIT_BAYT, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
        if (g_serit[i] == nullptr) {
            ESP_LOGE(ETIKET, "serit tamponu ayrilamadi (%u bayt x2)",
                     static_cast<unsigned>(SERIT_BAYT));
            for (int k = 0; k < 2; ++k) {
                heap_caps_free(g_serit[k]);
                g_serit[k] = nullptr;
            }
            return ESP_ERR_NO_MEM;
        }
    }

    // --- SPI veri yolu
    spi_bus_config_t yol{};
    yol.sclk_io_num = PATI_EKR_SCK;
    yol.mosi_io_num = PATI_EKR_MOSI;
    yol.miso_io_num = -1;      // ekrandan okumuyoruz
    yol.quadwp_io_num = -1;
    yol.quadhd_io_num = -1;
    // Tek islemde en fazla bir serit gidiyor. Bunu kucuk vermek
    // "transfer too large" hatasi uretiyor, buyuk vermek DMA
    // tanimlayicisi bosa ayiriyor.
    yol.max_transfer_sz = static_cast<int>(SERIT_BAYT);

    esp_err_t s = spi_bus_initialize(SPI_YUVA, &yol, SPI_DMA_CH_AUTO);
    if (s != ESP_OK) {
        ESP_LOGE(ETIKET, "SPI veri yolu acilamadi: %s", esp_err_to_name(s));
        return s;
    }

    // --- panel IO
    esp_lcd_panel_io_spi_config_t io{};
    io.cs_gpio_num = PATI_EKR_CS;
    io.dc_gpio_num = PATI_EKR_DC;
    io.spi_mode = 0;
    io.pclk_hz = PATI_EKR_HZ;
    // KUYRUK DERINLIGI 1 — bilincli. Iki tamponla birlikte hem
    // guvenlik hem ust uste binme sagliyor (bkz. baslik dosyasi).
    io.trans_queue_depth = 1;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;

    // esp_lcd_spi_bus_handle_t aslinda `int`; SPI yuvasi numarasi
    // dogrudan veriliyor. reinterpret_cast derlenmiyor (enum -> int),
    // static_cast dogrusu.
    s = esp_lcd_new_panel_io_spi(
        static_cast<esp_lcd_spi_bus_handle_t>(SPI_YUVA), &io, &g_io);
    if (s != ESP_OK) {
        ESP_LOGE(ETIKET, "panel IO kurulamadi: %s", esp_err_to_name(s));
        return s;
    }

    // --- ST7789 paneli
    esp_lcd_panel_dev_config_t pd{};
    pd.reset_gpio_num = PATI_EKR_RST;
    pd.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    pd.bits_per_pixel = 16;

    s = esp_lcd_new_panel_st7789(g_io, &pd, &g_panel);
    if (s != ESP_OK) {
        ESP_LOGE(ETIKET, "ST7789 paneli kurulamadi: %s", esp_err_to_name(s));
        return s;
    }

    // Sira onemli: reset -> init. Dokumanda acikca yaziyor
    // ("make sure the LCD panel has finished the reset stage").
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_panel_reset(g_panel));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_panel_init(g_panel));

    // Yon ve kayma, cizimden ONCE. Sira onemli: kaymayi swap_xy'den
    // once verirsek surucu onu eski eksende saklar ve goruntu kayik
    // cikar.
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_lcd_panel_swap_xy(g_panel, EKR_CEVIR));
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_lcd_panel_mirror(g_panel, EKR_AYNA_X, EKR_AYNA_Y));
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_lcd_panel_set_gap(g_panel, EKR_KAYMA_X, EKR_KAYMA_Y));

    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_lcd_panel_invert_color(g_panel, RENK_TERS));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_panel_disp_on_off(g_panel, true));

    g_hazir = true;

    // Once siyahla, SONRA isigi ac. Tersi yapilirsa acilista bir anlik
    // gurultu goruluyor.
    ekran_doldur(ekran_renk(0, 0, 0));
    ekran_arka_isik(true);

    ESP_LOGI(ETIKET, "ekran hazir: %dx%d, SPI%d @%d MHz, serit %d satir",
             PATI_EKR_G, PATI_EKR_Y, SPI_YUVA + 1,
             PATI_EKR_HZ / 1000000, EKRAN_SERIT_YUKSEK);
    ESP_LOGI(ETIKET, "  SCK=%d MOSI=%d CS=%d DC=%d RST=%d BLK=%d",
             PATI_EKR_SCK, PATI_EKR_MOSI, PATI_EKR_CS,
             PATI_EKR_DC, PATI_EKR_RST, PATI_EKR_BLK);
    ESP_LOGI(ETIKET, "  cevir=%d ayna=%d/%d kayma=%d/%d",
             EKR_CEVIR ? 1 : 0, EKR_AYNA_X ? 1 : 0, EKR_AYNA_Y ? 1 : 0,
             EKR_KAYMA_X, EKR_KAYMA_Y);
    return ESP_OK;
}

}  // namespace pati
