#include "pati_guncelleme.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#include "pati_ag.hpp"
#include "pati_gozler.hpp"
#include "pati_sohbet.hpp"

namespace pati {
namespace {

constexpr const char* ETIKET = "guncelleme";

// surum.json 1 KB'i gecmiyor; 4 KB bol pay.
constexpr int MANIFEST_SINIRI = 4096;

// Gorev yigiti. TLS el sikismasi ve JSON cozumu ayni gorevde;
// esp_https_ota'nin ornekleri 8 KB kullaniyor.
constexpr std::uint32_t YIGIT = 8192;

std::mutex g_kilit;
GuncellemeDurumu g_durum = GuncellemeDurumu::Bos;
std::string g_yeni_surum;
std::string g_notlar;
std::string g_bin_adresi;
std::string g_hata;
int g_yuzde = 0;

// Ayni anda iki gorev acilmasin. Anne dugmeye iki kez basabilir; ikinci
// bir OTA gorevi ayni bolume yazmaya kalkarsa imaj bozulur.
std::atomic<bool> g_mesgul{false};

// ---------------------------------------------------------------------------
// Surum karsilastirma
// ---------------------------------------------------------------------------
//
// "2.10.0" > "2.9.0" olmali. Dize karsilastirmasi bunu TERS veriyor
// ("1" < "9") ve panel yeni surumu hic gostermezdi. Sayi sayi
// karsilastiriyoruz.
//
// Bicim disi bir sey gelirse (etiket, tarih, bos) esitlik varsayiliyor —
// yani guncelleme GORUNMUYOR. Ters yonde hata yapmak, tanimadigi bir
// dizeyi "yeni" sanip her aciliste indirmeye kalkmak olurdu.
struct Surum {
    int a = 0, b = 0, c = 0;
    bool gecerli = false;
};

Surum surum_coz(const std::string& s)
{
    Surum v;
    if (s.empty()) return v;
    const int n = std::sscanf(s.c_str(), "%d.%d.%d", &v.a, &v.b, &v.c);
    v.gecerli = (n >= 2);
    return v;
}

// uzak > yerel ise true.
bool daha_yeni(const std::string& uzak, const std::string& yerel)
{
    const Surum u = surum_coz(uzak);
    const Surum y = surum_coz(yerel);
    if (!u.gecerli || !y.gecerli) {
        ESP_LOGW(ETIKET, "surum cozulemedi (uzak='%s' yerel='%s') — "
                         "guncelleme gosterilmiyor",
                 uzak.c_str(), yerel.c_str());
        return false;
    }
    if (u.a != y.a) return u.a > y.a;
    if (u.b != y.b) return u.b > y.b;
    return u.c > y.c;
}

void durum_yaz(GuncellemeDurumu d, const std::string& hata = "")
{
    std::lock_guard<std::mutex> k(g_kilit);
    g_durum = d;
    g_hata = hata;
}

const char* durum_adi(GuncellemeDurumu d)
{
    switch (d) {
        case GuncellemeDurumu::Bos:       return "bos";
        case GuncellemeDurumu::Bakiliyor: return "bakiliyor";
        case GuncellemeDurumu::Guncel:    return "guncel";
        case GuncellemeDurumu::Var:       return "var";
        case GuncellemeDurumu::Iniyor:    return "iniyor";
        case GuncellemeDurumu::Bitti:     return "bitti";
        case GuncellemeDurumu::Hata:      return "hata";
    }
    return "bos";
}

std::string json_kacisla(const std::string& ham)
{
    std::string c;
    c.reserve(ham.size() + 8);
    for (const char h : ham) {
        switch (h) {
            case '"':  c += "\\\""; break;
            case '\\': c += "\\\\"; break;
            case '\n': c += "\\n";  break;
            case '\r': c += "\\r";  break;
            case '\t': c += "\\t";  break;
            default:
                if (static_cast<unsigned char>(h) < 0x20) break;
                c += h;
        }
    }
    return c;
}

// ---------------------------------------------------------------------------
// surum.json
// ---------------------------------------------------------------------------

// Gelen govdeyi biriktirir. `perform()` veriyi parca parca bu olayla
// veriyor; tek seferde okunacak bir tampon YOK.
esp_err_t manifest_olayi(esp_http_client_event_t* olay)
{
    if (olay->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    auto* govde = static_cast<std::string*>(olay->user_data);
    if (govde == nullptr) return ESP_OK;
    // Sinir asilirsa yeni parcalari ATIYORUZ. Bozuk/dev bir cevap
    // yigini tuketmesin; JSON zaten cozulemeyecek ve hata dogru yerden
    // gelecek.
    if (govde->size() + static_cast<size_t>(olay->data_len)
        > static_cast<size_t>(MANIFEST_SINIRI)) {
        return ESP_OK;
    }
    govde->append(static_cast<const char*>(olay->data),
                  static_cast<size_t>(olay->data_len));
    return ESP_OK;
}

bool manifesti_cek(std::string& govde)
{
    esp_http_client_config_t ayar{};
    ayar.url = CONFIG_PATI_GUNCELLEME_ADRESI;
    ayar.method = HTTP_METHOD_GET;
    ayar.crt_bundle_attach = esp_crt_bundle_attach;
    ayar.timeout_ms = 15000;
    // 🔴 VARSAYILAN YETMIYOR — indirmedekiyle AYNI sebep.
    // `releases/latest/download/...` iki kez yonlendiriyor ve ikinci
    // Location basligi imzali bir adres: olculdu, 897 karakter.
    ayar.buffer_size = 4096;
    ayar.buffer_size_tx = 1024;
    ayar.event_handler = manifest_olayi;
    ayar.user_data = &govde;

    esp_http_client_handle_t c = esp_http_client_init(&ayar);
    if (c == nullptr) return false;

    // ---- 🔴 NEDEN perform(), ELLE open/fetch_headers/read DEGIL -------
    //
    // 01.08.2026, gercek kartta: manifest cekimi `surum.json HTTP 302`
    // ile dusuyordu. Sebep, elle akista YONLENDIRMENIN IZLENMEMESI.
    //
    // ESP-IDF belgesi acik: `esp_http_client_perform()` iceride
    // open -> write -> fetch_headers -> read yapiyor VE 30x gorunce
    // `esp_http_client_set_redirection()` cagiriyor. Elle acilan bir
    // istekte o adim YOK — 302 sadece bir durum kodu olarak geliyor ve
    // govde bos kaliyor.
    //
    // Kod once elle akisi kullaniyordu ve CALISIYORDU, cunku manifest
    // `raw.githubusercontent.com`'dan geliyordu ve orasi yonlendirmiyor.
    // Adres Release'e tasininca kirildi. Yani hata yeni yazilan kodda
    // degil, DEGISMEYEN kodun yeni ortamdaydi — en sinsi turu.
    //
    // ⚠️ `anahtar_dogrula()` hala elle akisi kullaniyor ve BILEREK
    // oyle birakildi: Google'in REST ucu yonlendirmiyor ve o yol gercek
    // kartta dogrulandi. Bir gun yonlendirirse belirti burada goruleni
    // olacak — "HTTP 302" ve bos cevap.
    bool tamam = false;
    const esp_err_t s = esp_http_client_perform(c);
    if (s == ESP_OK) {
        const int kod = esp_http_client_get_status_code(c);
        if (kod == 200) {
            tamam = true;
        } else {
            // 404: henuz hic Release yok, ya da adres yanlis. Kodu
            // yaziyoruz; "guncelleme bakilamadi" tek basina hangisi
            // oldugunu soylemiyor.
            ESP_LOGE(ETIKET, "surum.json HTTP %d", kod);
        }
    } else {
        ESP_LOGE(ETIKET, "surum.json cekilemedi: %s", esp_err_to_name(s));
    }
    esp_http_client_cleanup(c);
    return tamam;
}

void kontrol_gorevi(void*)
{
    durum_yaz(GuncellemeDurumu::Bakiliyor);

    std::string govde;
    if (!manifesti_cek(govde)) {
        durum_yaz(GuncellemeDurumu::Hata,
                  "Güncelleme sunucusuna ulaşılamadı");
        g_mesgul.store(false);
        vTaskDelete(nullptr);
        return;
    }

    cJSON* k = cJSON_Parse(govde.c_str());
    if (k == nullptr) {
        durum_yaz(GuncellemeDurumu::Hata, "Güncelleme bilgisi okunamadı");
        g_mesgul.store(false);
        vTaskDelete(nullptr);
        return;
    }

    const auto dize = [&k](const char* alan) -> std::string {
        const cJSON* v = cJSON_GetObjectItemCaseSensitive(k, alan);
        if (cJSON_IsString(v) && v->valuestring != nullptr) return v->valuestring;
        return "";
    };
    const std::string uzak_surum = dize("surum");
    const std::string bin = dize("bin");
    const std::string notlar = dize("notlar");
    cJSON_Delete(k);

    const std::string yerel = guncelleme_surumu();

    if (uzak_surum.empty() || bin.empty()) {
        durum_yaz(GuncellemeDurumu::Hata, "Güncelleme bilgisi eksik");
        g_mesgul.store(false);
        vTaskDelete(nullptr);
        return;
    }

    // ADRES HTTPS OLMAK ZORUNDA. Bu dosya cihazda KOSACAK kod; duz
    // HTTP'den gelirse araya giren biri istedigini yukleyebilir.
    // surum.json depoda duruyor ve depoyu ele geciren biri burayi
    // degistirebilirdi — denetim cihazda.
    if (bin.rfind("https://", 0) != 0) {
        ESP_LOGE(ETIKET, "bin adresi https degil: %s", bin.c_str());
        durum_yaz(GuncellemeDurumu::Hata, "Güncelleme adresi güvenli değil");
        g_mesgul.store(false);
        vTaskDelete(nullptr);
        return;
    }

    const bool yeni = daha_yeni(uzak_surum, yerel);
    {
        std::lock_guard<std::mutex> kl(g_kilit);
        g_yeni_surum = uzak_surum;
        g_notlar = notlar;
        g_bin_adresi = bin;
        g_yuzde = 0;
        g_hata.clear();
        g_durum = yeni ? GuncellemeDurumu::Var : GuncellemeDurumu::Guncel;
    }
    ESP_LOGI(ETIKET, "kosan=%s uzak=%s -> %s", yerel.c_str(),
             uzak_surum.c_str(), yeni ? "YENI VAR" : "guncel");

    g_mesgul.store(false);
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// Indirme
// ---------------------------------------------------------------------------

void indirme_gorevi(void*)
{
    std::string adres;
    std::string beklenen_surum;
    {
        std::lock_guard<std::mutex> k(g_kilit);
        adres = g_bin_adresi;
        beklenen_surum = g_yeni_surum;
        g_durum = GuncellemeDurumu::Iniyor;
        g_yuzde = 0;
        g_hata.clear();
    }

    // SOHBET DURUYOR. Iki sebep:
    //   Bellek — TLS indirmesi ile Gemini'nin TLS'i ayni anda ayakta
    //            olunca PSRAM'de sikisiyoruz.
    //   Nezaket — indirme bitince cihaz yeniden basliyor. Cocugun
    //            cumlesinin ortasinda kesmek yerine once susuyoruz.
    sohbet_durdur();
    // Gozler "uykulu": ekranda yazi yok (tasarim karari), ve guncelleme
    // sirasinda robotun cevap vermemesinin bir karsiligi olmali.
    gozler_durum("uykulu");

    ESP_LOGI(ETIKET, "iniyor: %s", adres.c_str());

    esp_http_client_config_t http{};
    http.url = adres.c_str();
    http.crt_bundle_attach = esp_crt_bundle_attach;
    http.timeout_ms = 30000;
    // 🔴 VARSAYILAN (512) YETMIYOR — OLCULDU.
    //
    // GitHub Release adresi 302 ile imzali bir indirme adresine
    // yonleniyor. 01.08.2026'da o Location basligi olculdu: 897
    // KARAKTER (imza + JWT). Varsayilan tamponla baslik kirpilir,
    // yonlendirme "bozuk adres" diye duser ve disaridan "baglanti
    // kurulamadi" gibi gorunur — yani sebebi tamponda aranmaz.
    //
    // 4096 iki kat pay birakiyor; GitHub imza bicimini buyutebilir.
    http.buffer_size = 4096;
    http.buffer_size_tx = 2048;
    http.keep_alive_enable = true;

    esp_https_ota_config_t ota{};
    ota.http_config = &http;

    esp_https_ota_handle_t tutamac = nullptr;
    esp_err_t s = esp_https_ota_begin(&ota, &tutamac);
    if (s != ESP_OK || tutamac == nullptr) {
        ESP_LOGE(ETIKET, "baslatilamadi: %s", esp_err_to_name(s));
        durum_yaz(GuncellemeDurumu::Hata, "Dosya indirilemedi");
        g_mesgul.store(false);
        vTaskDelete(nullptr);
        return;
    }

    // INEN IMAJIN SURUMU VAAT EDILENLE AYNI MI?
    //
    // Yanlis dosyayi Release'e koymak gercek bir hata: etiketi
    // karistirmak, eski bin'i surukleyip birakmak. Denetlemezsek panel
    // "2.1.0 kuruldu" der, cihazda 2.0.0 koser ve bir dahaki kontrolde
    // yine "guncelleme var" cikar — anne dongude kalir.
    esp_app_desc_t inen{};
    if (esp_https_ota_get_img_desc(tutamac, &inen) == ESP_OK) {
        if (beklenen_surum != inen.version) {
            ESP_LOGE(ETIKET, "surum uyusmuyor: bekleniyordu %s, imajda %s",
                     beklenen_surum.c_str(), inen.version);
            esp_https_ota_abort(tutamac);
            durum_yaz(GuncellemeDurumu::Hata,
                      "İndirilen dosya beklenen sürüm değil");
            g_mesgul.store(false);
            vTaskDelete(nullptr);
            return;
        }
    }

    const int toplam = esp_https_ota_get_image_size(tutamac);

    while (true) {
        s = esp_https_ota_perform(tutamac);
        if (s != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;

        if (toplam > 0) {
            const int inen_bayt = esp_https_ota_get_image_len_read(tutamac);
            const int y = std::clamp(inen_bayt * 100 / toplam, 0, 99);
            std::lock_guard<std::mutex> k(g_kilit);
            g_yuzde = y;
        }
    }

    if (s != ESP_OK) {
        ESP_LOGE(ETIKET, "indirme basarisiz: %s", esp_err_to_name(s));
        esp_https_ota_abort(tutamac);
        durum_yaz(GuncellemeDurumu::Hata, "İndirme yarıda kaldı");
        g_mesgul.store(false);
        vTaskDelete(nullptr);
        return;
    }

    s = esp_https_ota_finish(tutamac);
    if (s != ESP_OK) {
        // ESP_ERR_OTA_VALIDATE_FAILED buraya dusuyor: imajin kendi
        // SHA256'si tutmadi, yani dosya bozuk indi. ESKI YAPI DURUYOR,
        // cihaz hala calisiyor.
        ESP_LOGE(ETIKET, "dogrulanamadi: %s", esp_err_to_name(s));
        durum_yaz(GuncellemeDurumu::Hata, "Dosya bozuk indi — Pati eski "
                                          "sürümde kaldı");
        g_mesgul.store(false);
        vTaskDelete(nullptr);
        return;
    }

    {
        std::lock_guard<std::mutex> k(g_kilit);
        g_durum = GuncellemeDurumu::Bitti;
        g_yuzde = 100;
    }
    ESP_LOGW(ETIKET, "yazildi (%s) — yeniden baslaniyor", beklenen_surum.c_str());

    // Panelin son durumu ("bitti") yoklamasi 2 saniyede bir. Yeniden
    // baslamadan once bir tur gecsin ki anne "yeniden başlıyor" yazisini
    // gorsun; yoksa sayfa dogrudan "ulasilamiyor"a duser ve guncelleme
    // basarisiz olmus gibi gorunur.
    vTaskDelay(pdMS_TO_TICKS(2500));
    esp_restart();
}

}  // namespace

// ---------------------------------------------------------------------------

const char* guncelleme_surumu()
{
    const esp_app_desc_t* d = esp_app_get_description();
    return (d != nullptr) ? d->version : "?";
}

esp_err_t guncelleme_baslat()
{
    ESP_LOGI(ETIKET, "kosan surum: %s", guncelleme_surumu());
    ESP_LOGI(ETIKET, "guncelleme adresi: %s", CONFIG_PATI_GUNCELLEME_ADRESI);

    const esp_partition_t* p = esp_ota_get_running_partition();
    if (p != nullptr) {
        ESP_LOGI(ETIKET, "kosan bolum: %s (0x%08lx, %lu KB)", p->label,
                 static_cast<unsigned long>(p->address),
                 static_cast<unsigned long>(p->size / 1024));
    }
    // Bos bolum yoksa OTA HIC calismaz. Bunu ilk aciliste soyluyoruz,
    // anne dugmeye bastiginda degil: sebep bolum tablosu ve o kabloyla
    // duzeltiliyor.
    if (esp_ota_get_next_update_partition(nullptr) == nullptr) {
        ESP_LOGE(ETIKET, "BOS OTA BOLUMU YOK — panelden guncelleme "
                         "calismayacak. Bolum tablosu OTA'siz olabilir.");
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

void guncelleme_kontrol_et()
{
    if (ag_durumu() != AgDurumu::Bagli) {
        durum_yaz(GuncellemeDurumu::Hata, "Önce ağa bağlanması gerekiyor");
        return;
    }
    if (g_mesgul.exchange(true)) return;   // zaten bir is suruyor

    if (xTaskCreate(kontrol_gorevi, "pati_gun_k", YIGIT, nullptr, 3, nullptr)
        != pdPASS) {
        g_mesgul.store(false);
        durum_yaz(GuncellemeDurumu::Hata, "Şu an bakılamıyor");
    }
}

void guncelleme_indir()
{
    {
        std::lock_guard<std::mutex> k(g_kilit);
        if (g_durum != GuncellemeDurumu::Var || g_bin_adresi.empty()) return;
    }
    if (ag_durumu() != AgDurumu::Bagli) {
        durum_yaz(GuncellemeDurumu::Hata, "Önce ağa bağlanması gerekiyor");
        return;
    }

    // BOS BOLUM YOKSA BURADA SOYLE.
    //
    // Yalnizca ESKI bolum tablosuyla yuklenmis bir kartta olur (OTA'siz
    // tek uygulama). Denetlemezsek esp_https_ota_begin basarisiz olur ve
    // panel "Dosya indirilemedi" yazar — yani AG sorunu gibi gorunur.
    // Oysa sebep flash'in duzeni ve caresi kablo; ag ne kadar
    // beklenirse beklensin duzelmez.
    if (esp_ota_get_next_update_partition(nullptr) == nullptr) {
        durum_yaz(GuncellemeDurumu::Hata,
                  "Bu Pati panelden güncellenemiyor — kabloyla yüklenmesi "
                  "gerekiyor");
        return;
    }

    if (g_mesgul.exchange(true)) return;

    // Oncelik 3: ses gorevlerinin (4) ALTINDA. Indirme uzun surer ve
    // gecikme yolunun onune gecmemeli — sohbet zaten duruyor ama gozler
    // hala ciziliyor.
    if (xTaskCreate(indirme_gorevi, "pati_gun_i", YIGIT, nullptr, 3, nullptr)
        != pdPASS) {
        g_mesgul.store(false);
        durum_yaz(GuncellemeDurumu::Hata, "Şu an indirilemiyor");
    }
}

void guncelleme_onayla()
{
    const esp_partition_t* p = esp_ota_get_running_partition();
    if (p == nullptr) return;

    esp_ota_img_states_t hal;
    if (esp_ota_get_state_partition(p, &hal) != ESP_OK) return;
    if (hal != ESP_OTA_IMG_PENDING_VERIFY) return;

    // Buraya gelindiyse yeni yapi acildi, ag katmani kalkti ve panel
    // hizmet veriyor. Geri almayi iptal ediyoruz.
    const esp_err_t s = esp_ota_mark_app_valid_cancel_rollback();
    if (s == ESP_OK) {
        ESP_LOGW(ETIKET, "yeni yapi SAGLAM isaretlendi (%s)",
                 guncelleme_surumu());
    } else {
        ESP_LOGE(ETIKET, "saglam isaretlenemedi: %s", esp_err_to_name(s));
    }
}

std::string guncelleme_json()
{
    std::lock_guard<std::mutex> k(g_kilit);
    std::string j = "\"guncelleme\":{\"durum\":\"";
    j += durum_adi(g_durum);
    j += "\",\"su_anki\":\"";
    j += json_kacisla(guncelleme_surumu());
    j += "\",\"yeni\":\"";
    j += json_kacisla(g_yeni_surum);
    j += "\",\"notlar\":\"";
    j += json_kacisla(g_notlar);
    j += "\",\"yuzde\":";
    j += std::to_string(g_yuzde);
    if (!g_hata.empty()) {
        j += ",\"hata\":\"";
        j += json_kacisla(g_hata);
        j += "\"";
    }
    j += "}";
    return j;
}

}  // namespace pati
