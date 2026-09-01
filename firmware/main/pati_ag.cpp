#include "pati_ag.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <esp_event.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <mdns.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <nvs.h>

namespace pati {
namespace {

constexpr const char* ETIKET = "ag";
constexpr const char* NVS_ALAN = "pati";
constexpr const char* NVS_AD = "wifi_ad";
constexpr const char* NVS_SIFRE = "wifi_sifre";

// Kac denemeden sonra kurulum moduna gecilsin.
//
// 5: router acilirken ESP32'den once ayakta olmayabiliyor ve ilk iki
// deneme normalde de basarisiz oluyor. Cok az denersek robot her
// elektrik kesintisinden sonra kurulum moduna dusup ebeveyni tekrar
// ugrastirir; cok fazla denersek cocuk bekler.
constexpr int EN_FAZLA_DENEME = 5;

// Baglanti koptuktan sonra kac ust uste basarisizlikta kurulum moduna
// donulsun. Kopma normal (router yeniden basliyor); surekli kopma ise
// sifre degismis demek olabilir.
constexpr int KOPMA_SINIRI = 10;

// ---------------------------------------------------------------------------
// Kurulum modunda kayitli agi kac saniyede bir yeniden yoklayalim
// ---------------------------------------------------------------------------
//
// 🔴 KURULUM MODU TEK YONLU BIR KAPIYDI. Girildikten sonra kayitli ag
// BIR DAHA HIC denenmiyordu: gorev `portMAX_DELAY` ile bekliyor ve o
// bitleri ancak ebeveyn telefonla gelip panele sifre girerse birisi
// kaldiriyordu.
//
// 01.09.2026'da gercek kartta yasandi: Pati pille calisirken brownout'tan
// yeniden basladi, acilista agi yakalayamadi ve kurulum moduna dustu.
// Ag oradaydi, sifre dogruydu, kayit NVS'te duruyordu — Pati yine de
// kendi kendine donemedi. Panel cevap vermiyordu, gozler "uykulu"da
// kalmisti ve disaridan gorunusu "Pati bozuldu"ydu.
//
// Cocugun odasindaki bir robot icin bu kabul edilemez: router yeniden
// baslamasi, elektrik kesintisi, bir anlik sinyal kaybi — hepsi normal
// ve hicbiri insan mudahalesi gerektirmemeli.
//
// 3 dakika secildi. Kisa olsaydi kurulum sayfasindaki ebeveynin isini
// bolerdi (her deneme AP'yi kisa sure aksatiyor); uzun olsaydi router
// geri geldikten sonra cocuk bosuna beklerdi.
constexpr int KURULUM_YOKLAMA_MS = 3 * 60 * 1000;

constexpr int BAGLI_BIT = BIT0;
constexpr int BASARISIZ_BIT = BIT1;

EventGroupHandle_t g_olaylar = nullptr;
esp_netif_t* g_sta = nullptr;
esp_netif_t* g_ap = nullptr;

AgDurumu g_durum = AgDurumu::Kapali;
char g_ip[16] = "0.0.0.0";

// ---------------------------------------------------------------------------
// mDNS — panelin SABIT bir adresi olsun: http://pati.local
// ---------------------------------------------------------------------------
//
// 🔴 31.07.2026'da eksikligi tezgahta ortaya cikti. Ebeveyn kurulum
// modunda ev wifi'sini girdi, robot kendi agini kapatip eve gecti ve
// panel ULASILAMAZ oldu: yeni adres 192.168.x.x gibi bir sey ve onu
// kimse bilmiyor. Router her yeniden baslatmada baska bir numara da
// verebilir, yani "bir kere yaz bir kenara" da cozum degil.
//
// mDNS ile isim sabit. iPhone ve Mac'te Bonjour zaten var; Windows 10+
// ve Android 12+ de destekliyor.
//
// BAGLANTI KURULDUKTAN SONRA cagriliyor: mDNS bir IP'yi duyuruyor,
// IP yokken baslatmanin anlami yok. Tekrar cagrilmasi zararsiz —
// yeniden baglanmalarda (router yeniden basladi) ad geri geliyor.
void mdns_kur()
{
    static bool kuruldu = false;
    if (!kuruldu) {
        const esp_err_t s = mdns_init();
        if (s != ESP_OK) {
            // Panel IP ile yine calisiyor; bu olumcul degil.
            ESP_LOGW(ETIKET, "mDNS baslatilamadi: %s — panel yalnizca "
                             "IP ile acilir", esp_err_to_name(s));
            return;
        }
        kuruldu = true;
    }
    mdns_hostname_set("pati");
    mdns_instance_name_set("Pati ebeveyn paneli");
    mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    ESP_LOGI(ETIKET, "panel adresi: http://pati.local  (ya da http://%s)",
             g_ip);
}
std::string g_ad;
int g_deneme = 0;
// Kurulum AP'sine bagli telefon sayisi. Kayitli agi yoklarken bakiliyor:
// ebeveyn tam o an sifre giriyorsa denemeyi ERTELIYORUZ, cunku STA
// baglanmaya calisirken kanal degisiyor ve kurulum sayfasi kopuyor.
int g_telefon = 0;
int g_kopma = 0;
bool g_kurulum_modu = false;

std::string ap_adi()
{
    // MAC'in son iki baytindan: "Pati-A3F2".
    //
    // Neden MAC: ayni evde iki cihaz bulunursa (ikinci bir robot
    // yaptiysak) adlar cakismasin. Sabit "Pati" olsa telefon hangisine
    // baglandigini bilmezdi.
    std::uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char b[16];
    std::snprintf(b, sizeof(b), "Pati-%02X%02X", mac[4], mac[5]);
    return b;
}

// dBm -> 1..4 cubuk. Panelin gosterdigi sey bu.
int cubuk(int rssi)
{
    if (rssi >= -55) return 4;
    if (rssi >= -66) return 3;
    if (rssi >= -77) return 2;
    return 1;
}

void olay_geldi(void*, esp_event_base_t taban, std::int32_t no, void* veri)
{
    if (taban == WIFI_EVENT && no == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (taban == WIFI_EVENT && no == WIFI_EVENT_STA_DISCONNECTED) {
        const auto* d = static_cast<wifi_event_sta_disconnected_t*>(veri);

        if (g_durum == AgDurumu::Bagli) {
            // Calisan baglanti koptu.
            ++g_kopma;
            std::snprintf(g_ip, sizeof(g_ip), "0.0.0.0");
            g_durum = AgDurumu::Ariyor;
            ESP_LOGW(ETIKET, "baglanti koptu (sebep %d), yeniden deniyor "
                             "[%d/%d]",
                     d->reason, g_kopma, KOPMA_SINIRI);
            if (g_kopma >= KOPMA_SINIRI) {
                ESP_LOGE(ETIKET, "%d kez ust uste koptu — sifre degismis "
                                 "olabilir, kurulum moduna geciliyor",
                         g_kopma);
                xEventGroupSetBits(g_olaylar, BASARISIZ_BIT);
                return;
            }
            esp_wifi_connect();
            return;
        }

        // Henuz hic baglanamadik.
        ++g_deneme;
        if (g_deneme < EN_FAZLA_DENEME) {
            ESP_LOGW(ETIKET, "baglanamadi (sebep %d), deneme %d/%d",
                     d->reason, g_deneme, EN_FAZLA_DENEME);
            // Kisa bekleme: router acilirken hemen tekrar denemek bosa
            // gidiyor.
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_wifi_connect();
        } else {
            // SEBEBI YAZ. "baglanamadi" tek basina ise yaramiyor;
            // sifre yanlisligi ile ag bulunamamasi bambaska iki sorun.
            const char* aciklama =
                (d->reason == WIFI_REASON_NO_AP_FOUND)
                    ? "ag bulunamadi (ad yanlis ya da menzil disi)"
                    : (d->reason == WIFI_REASON_AUTH_FAIL ||
                       d->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                       d->reason == WIFI_REASON_HANDSHAKE_TIMEOUT)
                          ? "SIFRE YANLIS"
                          : "bilinmeyen sebep";
            ESP_LOGE(ETIKET, "baglanti kurulamadi: %s (kod %d)",
                     aciklama, d->reason);
            xEventGroupSetBits(g_olaylar, BASARISIZ_BIT);
        }
        return;
    }

    if (taban == IP_EVENT && no == IP_EVENT_STA_GOT_IP) {
        const auto* olay = static_cast<ip_event_got_ip_t*>(veri);
        std::snprintf(g_ip, sizeof(g_ip), IPSTR, IP2STR(&olay->ip_info.ip));
        g_deneme = 0;
        g_kopma = 0;
        g_durum = AgDurumu::Bagli;
        ESP_LOGI(ETIKET, "bagli: %s · IP %s", g_ad.c_str(), g_ip);
        mdns_kur();
        xEventGroupSetBits(g_olaylar, BAGLI_BIT);
        return;
    }

    if (taban == WIFI_EVENT && no == WIFI_EVENT_AP_STACONNECTED) {
        ++g_telefon;
        ESP_LOGI(ETIKET, "kurulum: bir telefon baglandi (%d)", g_telefon);
        return;
    }

    if (taban == WIFI_EVENT && no == WIFI_EVENT_AP_STADISCONNECTED) {
        if (g_telefon > 0) --g_telefon;
        return;
    }
}

bool nvs_oku(std::string& ad, std::string& sifre)
{
    nvs_handle_t h;
    if (nvs_open(NVS_ALAN, NVS_READONLY, &h) != ESP_OK) return false;

    char t[65];
    size_t n = sizeof(t);
    bool tamam = (nvs_get_str(h, NVS_AD, t, &n) == ESP_OK) && t[0] != '\0';
    if (tamam) ad = t;

    n = sizeof(t);
    if (nvs_get_str(h, NVS_SIFRE, t, &n) == ESP_OK) {
        sifre = t;
    } else {
        sifre.clear();   // sifresiz ag olabilir
    }
    nvs_close(h);
    return tamam;
}

esp_err_t nvs_yaz(const std::string& ad, const std::string& sifre)
{
    nvs_handle_t h;
    esp_err_t s = nvs_open(NVS_ALAN, NVS_READWRITE, &h);
    if (s != ESP_OK) return s;
    s = nvs_set_str(h, NVS_AD, ad.c_str());
    if (s == ESP_OK) s = nvs_set_str(h, NVS_SIFRE, sifre.c_str());
    if (s == ESP_OK) s = nvs_commit(h);
    nvs_close(h);
    return s;
}

void sta_ayarla(const std::string& ad, const std::string& sifre)
{
    wifi_config_t k{};
    std::strncpy(reinterpret_cast<char*>(k.sta.ssid), ad.c_str(),
                 sizeof(k.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(k.sta.password), sifre.c_str(),
                 sizeof(k.sta.password) - 1);
    // Sifresiz agi da kabul et: bazi evlerde misafir agi acik.
    k.sta.threshold.authmode =
        sifre.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &k));
    g_ad = ad;
}

void ap_ac()
{
    // Zaten kurulum modundaysak yalnizca durumu geri aliyoruz.
    //
    // Kayitli agi periyodik yoklama basarisiz olunca buraya geri
    // donuluyor; AP hic kapanmadigi icin telsizi yeniden yapilandirmak
    // gereksiz. Ozellikle BANNER tekrar basilmamali: uc dakikada bir
    // "KURULUM MODU" yazmak gunlugu doldurur ve gercekten yeni bir olay
    // olmus gibi gorunur.
    const bool ilk_giris = !g_kurulum_modu;

    g_kurulum_modu = true;
    g_durum = AgDurumu::Kurulum;
    g_ad = ap_adi();
    std::snprintf(g_ip, sizeof(g_ip), "192.168.4.1");

    if (!ilk_giris) return;

    wifi_config_t k{};
    std::strncpy(reinterpret_cast<char*>(k.ap.ssid), g_ad.c_str(),
                 sizeof(k.ap.ssid) - 1);
    k.ap.ssid_len = static_cast<std::uint8_t>(g_ad.size());
    k.ap.channel = 1;
    k.ap.max_connection = 3;
    // SIFRESIZ — gerekcesi baslik dosyasinda.
    k.ap.authmode = WIFI_AUTH_OPEN;

    // APSTA: AP telefon icin, STA TARAMA icin. Sadece AP olsak
    // esp_wifi_scan_start() calismaz ve ebeveyne ag listesi
    // gosteremezdik.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &k));

    std::snprintf(g_ip, sizeof(g_ip), "192.168.4.1");

    ESP_LOGW(ETIKET, "");
    ESP_LOGW(ETIKET, "===== KURULUM MODU =====");
    ESP_LOGW(ETIKET, " Telefonla su aga baglan: %s", g_ad.c_str());
    ESP_LOGW(ETIKET, " Sayfa kendiliginden acilacak.");
    ESP_LOGW(ETIKET, " Acilmazsa tarayiciya yaz: http://192.168.4.1");
    ESP_LOGW(ETIKET, "========================");
    ESP_LOGW(ETIKET, "");
}

void ag_gorevi(void*)
{
    std::string ad, sifre;
    if (!nvs_oku(ad, sifre)) {
        ESP_LOGW(ETIKET, "kayitli ag yok — kurulum moduna geciliyor");
        ap_ac();
        vTaskDelete(nullptr);
        return;
    }

    g_durum = AgDurumu::Ariyor;
    ESP_LOGI(ETIKET, "'%s' agina baglaniliyor...", ad.c_str());
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    sta_ayarla(ad, sifre);
    ESP_ERROR_CHECK(esp_wifi_start());

    // 🔴 GUC TASARRUFU KAPALI — sesin kesilmemesi icin.
    //
    // ESP-IDF varsayilani WIFI_PS_MIN_MODEM: radyo, yonlendiricinin DTIM
    // beacon'lari arasinda UYUYOR. Yonlendiriciye gore bu 100-300 ms
    // demek ve paketler o araliklarla toplu geliyor.
    //
    // Gemini'nin sesi gercek zamanli akiyor ve hoparlorun DMA tamponu
    // 60 ms tasiyor (6 x 240 kare @ 24 kHz). Yani radyo bir DTIM
    // araligi uyudugunda tampon KURUYOR ve konusmanin ortasinda bosluk
    // duyuluyor.
    //
    // Bedeli: ortalama ~30 mA fazla akim. Priz beslemeli bir masa robotu
    // icin bedava; pil olsaydi burasi tartisilirdi.
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_ps(WIFI_PS_NONE));

    while (true) {
        // 🔴 KURULUM MODUNDA SONSUZA KADAR BEKLENMIYOR.
        //
        // Eskiden burada kosulsuz `portMAX_DELAY` vardi ve kurulum modu
        // tek yonlu bir kapiya donusuyordu: bitleri ancak ebeveyn
        // telefonla gelip sifre girerse birisi kaldirabiliyordu.
        // Gerekcesi ve olculen olay KURULUM_YOKLAMA_MS'in yaninda.
        const TickType_t bekleme = g_kurulum_modu
                                       ? pdMS_TO_TICKS(KURULUM_YOKLAMA_MS)
                                       : portMAX_DELAY;

        const EventBits_t b = xEventGroupWaitBits(
            g_olaylar, BAGLI_BIT | BASARISIZ_BIT, pdTRUE, pdFALSE, bekleme);

        if (b & BASARISIZ_BIT) {
            // Baglanamadi ya da surekli kopuyor: ebeveyn mudahale
            // edebilsin diye kurulum modu.
            ap_ac();
            g_deneme = 0;
            g_kopma = 0;
            continue;
        }

        if (b & BAGLI_BIT) {
            // Yoklama tuttu: ag geri geldi. AP artik gereksiz.
            // (Panelden girilen sifrede ayni isi ag_baglan yapiyor.)
            if (g_kurulum_modu) {
                ESP_LOGI(ETIKET, "kayitli ag geri geldi — kurulum agi "
                                 "kapatiliyor");
                esp_wifi_set_mode(WIFI_MODE_STA);
                g_kurulum_modu = false;
            }
            continue;
        }

        // ---- Zaman asimi: kurulum modundayiz, kayitli agi yokla ----
        std::string yad, ysifre;
        if (!nvs_oku(yad, ysifre)) {
            // Kayit yok — kutudan yeni cikmis robot. Yoklanacak bir sey
            // de yok, ebeveyn bekleniyor.
            continue;
        }

        if (g_telefon > 0) {
            // Ebeveyn TAM SU AN kurulum sayfasinda. Denemek AP'yi
            // aksatir ve onun sayfasi kopar — uc dakika sonra bakariz.
            ESP_LOGI(ETIKET, "kurulum sayfasi acik, yoklama ertelendi");
            continue;
        }

        ESP_LOGI(ETIKET, "kurulum modu: '%s' yeniden yoklaniyor",
                 yad.c_str());
        // Sayac sifirlanmali: yukarida 5'e dayanmis halde duruyor ve
        // sifirlanmazsa ilk kopusta hemen pes ederdik.
        g_deneme = 0;
        g_durum = AgDurumu::Ariyor;
        sta_ayarla(yad, ysifre);
        esp_wifi_connect();
    }
}

}  // namespace

// ---------------------------------------------------------------------------

esp_err_t ag_baslat()
{
    if (g_olaylar != nullptr) return ESP_OK;

    g_olaylar = xEventGroupCreate();
    if (g_olaylar == nullptr) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    g_sta = esp_netif_create_default_wifi_sta();
    g_ap = esp_netif_create_default_wifi_ap();

    wifi_init_config_t k = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&k));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &olay_geldi, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &olay_geldi, nullptr, nullptr));

    // Kurulum modunda esp_wifi_start() ap_ac() icinde CAGRILMIYOR;
    // burada bir kez basliyor ve mod degisiyor. Iki kez start etmek
    // ESP_ERR_WIFI_NOT_STOPPED veriyor.
    std::string ad, sifre;
    if (!nvs_oku(ad, sifre)) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    // Ayri gorev: baglanma denemeleri saniyeler suruyor ve bu sure
    // boyunca ekran/gozler olu kalmamali.
    if (xTaskCreate(ag_gorevi, "ag", 4096, nullptr, 5, nullptr) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

AgDurumu ag_durumu() { return g_durum; }
const char* ag_ip() { return g_ip; }
const char* ag_adi() { return g_ad.c_str(); }

int ag_gucu()
{
    if (g_durum != AgDurumu::Bagli) return 0;
    wifi_ap_record_t k{};
    if (esp_wifi_sta_get_ap_info(&k) != ESP_OK) return 0;
    return cubuk(k.rssi);
}

bool ag_kayitli_var()
{
    std::string a, s;
    return nvs_oku(a, s);
}

std::vector<BulunanAg> ag_tara()
{
    std::vector<BulunanAg> sonuc;

    wifi_scan_config_t k{};
    k.show_hidden = false;
    if (esp_wifi_scan_start(&k, true) != ESP_OK) {
        ESP_LOGW(ETIKET, "tarama baslatilamadi");
        return sonuc;
    }

    std::uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n == 0) return sonuc;

    // Sinir: 30 ag fazlasiyla yeterli ve yiginda 30 * ~80 bayt.
    n = std::min<std::uint16_t>(n, 30);
    std::vector<wifi_ap_record_t> kayit(n);
    if (esp_wifi_scan_get_ap_records(&n, kayit.data()) != ESP_OK) {
        return sonuc;
    }

    for (std::uint16_t i = 0; i < n; ++i) {
        const char* ad = reinterpret_cast<const char*>(kayit[i].ssid);
        if (ad[0] == '\0') continue;
        // Kendi AP'mizi listelemeyelim; ebeveyn ona baglanmayi denerse
        // kurulum sayfasi kopar.
        if (g_kurulum_modu && g_ad == ad) continue;

        // Ayni ag birden fazla kanalda gorunuyor (mesh/repeater):
        // en gucluyu tut.
        const auto var = std::find_if(sonuc.begin(), sonuc.end(),
                                      [&](const BulunanAg& b) {
                                          return b.ad == ad;
                                      });
        const int g = cubuk(kayit[i].rssi);
        if (var != sonuc.end()) {
            var->guc = std::max(var->guc, g);
            continue;
        }
        BulunanAg b;
        b.ad = ad;
        b.guc = g;
        b.kilit = (kayit[i].authmode != WIFI_AUTH_OPEN);
        sonuc.push_back(std::move(b));
    }

    std::sort(sonuc.begin(), sonuc.end(),
              [](const BulunanAg& a, const BulunanAg& b) {
                  return a.guc > b.guc;
              });
    return sonuc;
}

esp_err_t ag_kaydet_ve_bagla(const std::string& ad, const std::string& sifre)
{
    if (ad.empty()) return ESP_ERR_INVALID_ARG;

    // ESKI KAYIT SAKLANIYOR. Yeni bilgi tutmazsa geri yukleniyor —
    // yoksa ebeveyn sifreyi yanlis yazdiginda robot calisan agini da
    // kaybediyor ve elde hicbir sey kalmiyor.
    std::string eski_ad, eski_sifre;
    const bool eski_var = nvs_oku(eski_ad, eski_sifre);

    esp_err_t s = nvs_yaz(ad, sifre);
    if (s != ESP_OK) return s;

    ESP_LOGI(ETIKET, "'%s' deneniyor...", ad.c_str());
    g_deneme = 0;
    g_kopma = 0;
    g_durum = AgDurumu::Ariyor;

    xEventGroupClearBits(g_olaylar, BAGLI_BIT | BASARISIZ_BIT);
    esp_wifi_disconnect();
    sta_ayarla(ad, sifre);
    esp_wifi_connect();

    // Sonucu bekle. Ust sinir EN_FAZLA_DENEME denemesini kapsayacak
    // kadar; tarayici bu sure boyunca bekliyor.
    const EventBits_t b = xEventGroupWaitBits(
        g_olaylar, BAGLI_BIT | BASARISIZ_BIT, pdTRUE, pdFALSE,
        pdMS_TO_TICKS(20000));

    if (b & BAGLI_BIT) {
        // Basarili: kurulum AP'sini kapat, pencere kapansin.
        if (g_kurulum_modu) {
            ESP_LOGI(ETIKET, "baglandi — kurulum agi kapatiliyor");
            esp_wifi_set_mode(WIFI_MODE_STA);
            g_kurulum_modu = false;
        }
        return ESP_OK;
    }

    ESP_LOGW(ETIKET, "'%s' ile baglanilamadi", ad.c_str());
    if (eski_var) {
        ESP_LOGW(ETIKET, "eski ag kaydi geri yukleniyor: '%s'",
                 eski_ad.c_str());
        nvs_yaz(eski_ad, eski_sifre);
    }
    return ESP_ERR_TIMEOUT;
}

void ag_unut()
{
    nvs_handle_t h;
    if (nvs_open(NVS_ALAN, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_AD);
        nvs_erase_key(h, NVS_SIFRE);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGW(ETIKET, "ag kaydi silindi — sonraki acilista kurulum modu");
}

}  // namespace pati
