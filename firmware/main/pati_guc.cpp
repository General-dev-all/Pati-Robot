#include "pati_guc.hpp"

#include <algorithm>
#include <cstdint>

#include <driver/temperature_sensor.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "pati_pinler.h"

namespace pati {
namespace {

constexpr const char* ETIKET = "pati.guc";

// I2C islemleri icin bekleme. Hat kisa (govde ici, birkac santim) ve
// uc aygit da hizli cevap veriyor; 100 ms fazlasiyla bol.
constexpr int BEKLEME_MS = 100;

i2c_master_bus_handle_t g_yol = nullptr;
i2c_master_dev_handle_t g_pm1 = nullptr;
bool g_hazir = false;

// M5PM1 islemleri BIRKAC KEZ DENENIYOR.
//
// Yonganin "I2C bosta uyku" kipi var (M5Stack belgesi, M5PM1 Sleep >
// I2C Idle Sleep): hat bir sure sessiz kalinca uyuyor ve kendisini
// uyandiran ILK islem kayboluyor. Tek denemede bu, rastgele bir NACK
// gibi gorunur — acilista bir kere olur, bir daha olmaz, sebebi
// aranirken bulunmaz.
//
// Uc deneme yeterli: ilki uyandirir, ikincisi is gorur. Aradaki kisa
// bekleme yonganin ayaga kalkmasi icin.
constexpr int DENEME = 3;

esp_err_t pm1_oku(std::uint8_t reg, std::uint8_t& deger)
{
    esp_err_t hata = ESP_FAIL;
    for (int i = 0; i < DENEME; ++i) {
        hata = i2c_master_transmit_receive(g_pm1, &reg, 1, &deger, 1,
                                           BEKLEME_MS);
        if (hata == ESP_OK) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return hata;
}

esp_err_t pm1_yaz(std::uint8_t reg, std::uint8_t deger)
{
    const std::uint8_t paket[2] = {reg, deger};
    esp_err_t hata = ESP_FAIL;
    for (int i = 0; i < DENEME; ++i) {
        hata = i2c_master_transmit(g_pm1, paket, sizeof(paket), BEKLEME_MS);
        if (hata == ESP_OK) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return hata;
}

// Bir register'in tek bir bitini degistirir, kalanina dokunmaz.
//
// OKU-DEGISTIR-YAZ SART: bu register'lar M5PM1'in BES pinini birden
// tutuyor. Dogrudan yazsaydik L3B'yi acarken ayni register'daki diger
// pinleri (pil sarj durumu, IMU kesmesi) sifirlardik.
esp_err_t pm1_bit(std::uint8_t reg, int bit, bool deger)
{
    std::uint8_t v = 0;
    const esp_err_t hata = pm1_oku(reg, v);
    if (hata != ESP_OK) return hata;

    const std::uint8_t maske = static_cast<std::uint8_t>(1u << bit);
    const std::uint8_t yeni =
        deger ? static_cast<std::uint8_t>(v | maske)
              : static_cast<std::uint8_t>(v & ~maske);

    if (yeni == v) return ESP_OK;
    return pm1_yaz(reg, yeni);
}

// FUNC0 register'i pin BASINA IKI BIT tutuyor (pin 0-3, toplam 8 bit).
// 0b00 = duz GPIO. Digerleri kesme, uyandirma ve ozel islevler.
esp_err_t pm1_gpio_islevi(int pin)
{
    std::uint8_t v = 0;
    const esp_err_t hata = pm1_oku(PATI_PM1_GPIO_FUNC0, v);
    if (hata != ESP_OK) return hata;

    const std::uint8_t maske = static_cast<std::uint8_t>(0b11u << (pin * 2));
    const std::uint8_t yeni = static_cast<std::uint8_t>(v & ~maske);

    if (yeni == v) return ESP_OK;
    return pm1_yaz(PATI_PM1_GPIO_FUNC0, yeni);
}

// M5PM1'in bir pinini "push-pull cikis" yapip verilen seviyeye surer.
//
// SIRA ONEMLI: once islev ve surus bicimi, sonra SEVIYE, en son YON.
// Yonu en sona birakinca pin cikis oldugu anda dogru seviyede oluyor.
// Ters sirada once yanlis seviyede bir cikis olusur ve L3B icin bu,
// mikrofonla hoparlore anlik bir guc dalgalanmasi demek.
esp_err_t pm1_cikis(int pin, bool seviye)
{
    esp_err_t hata = pm1_gpio_islevi(pin);
    if (hata != ESP_OK) return hata;

    hata = pm1_bit(PATI_PM1_GPIO_DRV, pin, false);  // push-pull
    if (hata != ESP_OK) return hata;

    hata = pm1_bit(PATI_PM1_GPIO_OUT, pin, seviye);
    if (hata != ESP_OK) return hata;

    hata = pm1_bit(PATI_PM1_GPIO_MODE, pin, true);  // cikis
    if (hata != ESP_OK) return hata;

    // ---- GERI OKUYUP DOGRULA --------------------------------------------
    //
    // 🔴 BURADAKI BIT DUZENI BIR VARSAYIM.
    //
    // M5PM1'in register haritasi surucu kutuphanesinin basligindan
    // alindi (adresler ve enum degerleri kesin) ama FUNC0'in pin basina
    // IKI BIT tuttugu ve pin N'in [2N+1:2N] bitlerinde oldugu
    // CIKARIMDIR — dort pin, sekiz bit, dogal yerlesim. Yonga elimize
    // gelmeden dogrulanamiyor.
    //
    // Yanlissa olacak sey sessizlik: yazma I2C'de basarili doner, bit
    // baska yere gider, L3B ya da amfi acilmaz ve hicbir hata cikmaz.
    // Sonra saatlerce "ses neden yok" diye ses koduna bakilir.
    //
    // Geri okuma bunu goruunur yapiyor: yongaya ne yazdigimizi degil,
    // yonganin ne ANLADIGINI soruyoruz. Bedeli iki I2C okumasi, bir
    // kez, acilista.
    std::uint8_t mod = 0, cikis = 0;
    if (pm1_oku(PATI_PM1_GPIO_MODE, mod) != ESP_OK ||
        pm1_oku(PATI_PM1_GPIO_OUT, cikis) != ESP_OK) {
        ESP_LOGW(ETIKET, "PYG%d geri okunamadi — yazildi ama dogrulanmadi",
                 pin);
        return ESP_OK;  // yazma basariliydi; okuyamamak baska bir sorun
    }

    const std::uint8_t maske = static_cast<std::uint8_t>(1u << pin);
    const bool mod_ok = (mod & maske) != 0;
    const bool seviye_ok = ((cikis & maske) != 0) == seviye;

    if (!mod_ok || !seviye_ok) {
        ESP_LOGE(ETIKET,
                 "PYG%d BEKLENEN GIBI AYARLANMADI — mode=0x%02X out=0x%02X "
                 "(cikis %s, seviye %s)",
                 pin, mod, cikis,
                 mod_ok ? "tamam" : "YANLIS",
                 seviye_ok ? "tamam" : "YANLIS");
        ESP_LOGE(ETIKET, "  M5PM1 register bit duzeni varsayimi yanlis "
                         "olabilir — pati_pinler.h, PATI_PM1_*");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

// Iki ardisik register'i tek bir 16 bitlik mV degeri olarak okur.
//
// Kucuk-sonlu ve IKI BAYT DA TAM: ust bayti dort bitle maskelemek
// (surucu basliginin dedigi gibi) pili 4130 mV yerine 34 mV gosteriyordu.
// Gerekcesi ve olculen degerler pati_pinler.h'de.
//
// Okunamazsa -1 — 0 degil, cunku 0 gecerli bir gerilim (VIN yokken).
int pm1_16bit(std::uint8_t dusuk_reg, std::uint8_t ust_reg)
{
    std::uint8_t dusuk = 0, ust = 0;
    if (g_pm1 == nullptr) return -1;
    if (pm1_oku(dusuk_reg, dusuk) != ESP_OK ||
        pm1_oku(ust_reg, ust) != ESP_OK) {
        return -1;
    }
    return (static_cast<int>(ust) << 8) | dusuk;
}

// Hatta kim var — YALNIZCA bir sey ters gidince basiliyor.
//
// Bunun degeri sudur: "M5PM1 cevap vermiyor" tek basina hicbir sey
// anlatmiyor. Hatta HIC KIMSE yoksa sorun I2C hattinda (pin, cekme
// direnci, guc); baskalari cevap verip yalnizca M5PM1 susuyorsa sorun
// o yongada. Iki durum tamamen farkli yerlere bakmayi gerektiriyor ve
// aradaki farki bu tek satir soyluyor.
void hatti_tara()
{
    char liste[96];
    int n = 0;
    for (std::uint8_t adres = 0x08; adres < 0x78 && n < 80; ++adres) {
        if (i2c_master_probe(g_yol, adres, 50) == ESP_OK) {
            n += snprintf(liste + n, sizeof(liste) - n, " 0x%02X", adres);
        }
    }
    if (n == 0) {
        ESP_LOGE(ETIKET, "I2C hattinda HIC KIMSE yok — hat/guc sorunu");
    } else {
        ESP_LOGE(ETIKET, "I2C hattinda cevap verenler:%s", liste);
    }
}

}  // namespace

i2c_master_bus_handle_t i2c_yolu() { return g_yol; }

bool guc_hazir() { return g_hazir; }

esp_err_t guc_baslat()
{
    if (g_yol != nullptr) return ESP_OK;

    i2c_master_bus_config_t yol = {};
    yol.i2c_port = I2C_NUM_0;
    yol.sda_io_num = PATI_I2C_SDA;
    yol.scl_io_num = PATI_I2C_SCL;
    yol.clk_source = I2C_CLK_SRC_DEFAULT;
    yol.glitch_ignore_cnt = 7;
    // Kartta harici cekme direncleri var; icerideki zayif direncler
    // yalnizca emniyet payi.
    yol.flags.enable_internal_pullup = true;

    esp_err_t hata = i2c_new_master_bus(&yol, &g_yol);
    if (hata != ESP_OK) {
        ESP_LOGE(ETIKET, "I2C hatti acilamadi: %s", esp_err_to_name(hata));
        g_yol = nullptr;
        return hata;
    }

    i2c_device_config_t aygit = {};
    aygit.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    aygit.device_address = PATI_ADR_M5PM1;
    aygit.scl_speed_hz = PATI_I2C_HZ;

    hata = i2c_master_bus_add_device(g_yol, &aygit, &g_pm1);
    if (hata != ESP_OK) {
        ESP_LOGE(ETIKET, "M5PM1 eklenemedi: %s", esp_err_to_name(hata));
        return hata;
    }

    if (i2c_master_probe(g_yol, PATI_ADR_M5PM1, BEKLEME_MS) != ESP_OK) {
        ESP_LOGE(ETIKET, "M5PM1 (0x%02X) cevap vermiyor", PATI_ADR_M5PM1);
        hatti_tara();
        return ESP_ERR_NOT_FOUND;
    }

    // ---- L3B: mikrofon + hoparlor + LCD arka isik gucu -------------------
    //
    // Bunu acmadan asagidaki hicbir sey calismaz ve hicbiri HATA DA
    // VERMEZ. Ayrintili gerekce pati_pinler.h'nin basinda.
    hata = pm1_cikis(PATI_PM1_L3B, true);
    if (hata != ESP_OK) {
        ESP_LOGE(ETIKET, "L3B acilamadi: %s — mikrofon, hoparlor ve ekran "
                         "arka isigi olu kalacak", esp_err_to_name(hata));
        return hata;
    }

    // Guc raylarinin oturmasi icin. Kodege bu sure dolmadan I2C'den
    // yazmak, henuz beslenmemis bir yongaya yazmak olurdu.
    vTaskDelay(pdMS_TO_TICKS(50));

    hata = hoparlor_amfi(true);
    if (hata != ESP_OK) {
        ESP_LOGE(ETIKET, "hoparlor amfisi acilamadi: %s",
                 esp_err_to_name(hata));
        return hata;
    }

    g_hazir = true;

    // Arizali acilisi say. Buraya kadar gelindi, yani I2C ve M5PM1
    // ayakta; pil gerilimi de okunabiliyor.
    cokme_say();

    ESP_LOGI(ETIKET, "guc hazir — I2C %d/%d @%d kHz, L3B acik, amfi acik",
             PATI_I2C_SCL, PATI_I2C_SDA, PATI_I2C_HZ / 1000);
    return ESP_OK;
}

esp_err_t hoparlor_amfi(bool ac)
{
    if (g_pm1 == nullptr) return ESP_ERR_INVALID_STATE;
    return pm1_cikis(PATI_PM1_AMF, ac);
}

bool donanim_dogru()
{
    if (g_yol == nullptr) return false;
    return i2c_master_probe(g_yol, PATI_ADR_ES8311, BEKLEME_MS) == ESP_OK;
}

GucKaynagi guc_kaynak()
{
    const int vin = vin_mv();
    if (vin < 0) return GucKaynagi::Bilinmiyor;

    // ESIK 3000 mV. Olculen iki uc birbirinden cok uzak: USB takiliyken
    // 4884 mV, cikarilinca birkac yuz mV'a dusuyor. Esigin tam ortada
    // olmasi gerekmiyor, ikisinden de UZAK olmasi gerekiyor.
    return (vin >= 3000) ? GucKaynagi::Usb : GucKaynagi::Pil;
}

int pil_mv()
{
    return std::max(0, pm1_16bit(PATI_PM1_VBAT_L, PATI_PM1_VBAT_H));
}

// ---------------------------------------------------------------------------
// Cokme sayaci — NVS'te, acilistan acilisa yasiyor
// ---------------------------------------------------------------------------
//
// 🔴 NEDEN GEREKLI: pilde cokmeler USB'siz oluyor ve seri port yok.
// 02.09.2026 gecesi cokmeler ancak disaridan ag uzerinden 2 saniyede bir
// yoklayarak sayilabildi — yani bilgisayarin acik ve bir betigin
// calisiyor olmasi gerekiyordu. Cocugun evinde boyle bir sey olmayacak.
//
// Ustelik yapilacak isin tamami A/B karsilastirmasi: "goz kare hizini
// dusurunce cokme azaldi mi". Bunu sayabilmek icin sayinin CIHAZDA
// durmasi lazim.
//
// Bir acilista bir kez yaziliyor, yani flash yipranmasi sorun degil.
void cokme_say()
{
    const esp_reset_reason_t sebep = esp_reset_reason();
    // Normal acilislar sayilmiyor: dugmeye basmak ya da kabloyla yukleme
    // bir ariza degil.
    if (sebep != ESP_RST_BROWNOUT && sebep != ESP_RST_PANIC &&
        sebep != ESP_RST_TASK_WDT && sebep != ESP_RST_INT_WDT) {
        return;
    }

    nvs_handle_t h;
    if (nvs_open("pati", NVS_READWRITE, &h) != ESP_OK) return;

    std::uint32_t n = 0;
    nvs_get_u32(h, "cokme", &n);
    nvs_set_u32(h, "cokme", n + 1);
    // Son cokme aninda pil ne kadardi — dusuk pil hipotezinin sinandigi
    // yer burasi.
    nvs_set_u32(h, "cokme_mv", static_cast<std::uint32_t>(pil_mv()));
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGW(ETIKET, "COKME SAYACI: %u (bu acilis: %s, pil %d mV)",
             static_cast<unsigned>(n + 1),
             sebep == ESP_RST_BROWNOUT ? "brownout"
             : sebep == ESP_RST_PANIC  ? "panic"
                                       : "bekci",
             pil_mv());
}

std::uint32_t cokme_sayisi()
{
    nvs_handle_t h;
    if (nvs_open("pati", NVS_READONLY, &h) != ESP_OK) return 0;
    std::uint32_t n = 0;
    nvs_get_u32(h, "cokme", &n);
    nvs_close(h);
    return n;
}

void cokme_sayaci_sifirla()
{
    nvs_handle_t h;
    if (nvs_open("pati", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, "cokme", 0);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGW(ETIKET, "cokme sayaci sifirlandi");
}

float yonga_sicakligi()
{
    // Tek sefer kuruluyor, sonra hep ayni tutamak.
    //
    // 🔴 ARALIK KEYFI SECILEMIYOR. Ilk denemede (-10, 110) yazildi ve
    // kurulum sessizce REDDEDILDI: ESP32-S3 yalnizca belirli olcum
    // aralıklarini destekliyor (-10~80, -30~50, 20~100, 50~125) ve
    // eslesmeyen bir aralik hata donduruyor. Panelde -1000 gorununce
    // anlasildi.
    //
    // 20~100 secildi cunku sorulan soru "isiniyor mu": yonga zaten 40 C
    // civarinda calisiyor, ilgilendigimiz bant onun ustu.
    static temperature_sensor_handle_t tutamak = nullptr;
    static bool denendi = false;
    if (!denendi) {
        denendi = true;
        temperature_sensor_config_t ayar =
            TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
        esp_err_t hata = temperature_sensor_install(&ayar, &tutamak);
        if (hata == ESP_OK) hata = temperature_sensor_enable(tutamak);
        if (hata != ESP_OK) {
            // HATA KODU YAZILIYOR. Ilk surumde yalnizca "kurulamadi"
            // yaziyordu ve sebep gorunmuyordu; ustelik tek sefer
            // basildigi icin sonradan bakildiginda gunlukte hic yoktu.
            ESP_LOGW(ETIKET, "sicaklik sensoru kurulamadi: %s",
                     esp_err_to_name(hata));
            tutamak = nullptr;
        }
    }
    if (tutamak == nullptr) return -1000.0f;

    float c = 0.0f;
    if (temperature_sensor_get_celsius(tutamak, &c) != ESP_OK) return -1000.0f;
    return c;
}

int vin_mv()
{
    return pm1_16bit(PATI_PM1_VIN_L, PATI_PM1_VIN_H);
}

}  // namespace pati
