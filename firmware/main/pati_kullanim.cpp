#include "pati_kullanim.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_netif_sntp.h>
#include <esp_timer.h>
#include <nvs.h>

namespace pati {
namespace {

constexpr const char* ETIKET = "kullanim";
constexpr const char* NVS_ALAN = "pati";
constexpr const char* NVS_ANAHTAR = "kullanim";

// Kac gunluk kayit tutulacak. Panelde "bu ay" var, yani 31 gun yeterdi;
// 60 gun ay basi gecislerinde onceki aya bakabilmek icin.
// PLAN.md: sinirsiz buyuyen hicbir sey birakma.
constexpr int EN_FAZLA_GUN = 60;

// Saat ayarlanmadan once biriken dakikalar buraya. Kaybolmasinlar.
constexpr const char* SAATSIZ = "0000-00-00";

std::map<std::string, double> g_gunler;
std::int64_t g_oturum_basi_us = 0;
bool g_oturum_acik = false;
bool g_saat_hazir = false;

std::string bugun()
{
    if (!g_saat_hazir) return SAATSIZ;
    std::time_t t = std::time(nullptr);
    std::tm bilgi{};
    localtime_r(&t, &bilgi);
    // DEGERLER SINIRLANIYOR, tampon buyutulmuyor.
    //
    // Derleyici hakli (-Werror=format-truncation): tm_year cok buyuk
    // olabilir. Tamponu buyutmek uyariyi susturur ama GERCEK sorunu
    // cozmez — saat bozuk okunursa "2147485547-01-01" gibi bir anahtar
    // NVS'e girer ve orada kalir. Sinirlamak hem uyariyi hem sorunu
    // bitiriyor.
    const int yil = std::clamp(bilgi.tm_year + 1900, 1970, 9999);
    const int ay = std::clamp(bilgi.tm_mon + 1, 1, 12);
    const int gun = std::clamp(bilgi.tm_mday, 1, 31);
    char b[11];
    std::snprintf(b, sizeof(b), "%04d-%02d-%02d", yil, ay, gun);
    return b;
}

void nvs_yaz()
{
    // Sinir: en yeni EN_FAZLA_GUN gun. std::map sirali oldugu icin
    // bastan silmek en eskiyi siliyor.
    while (static_cast<int>(g_gunler.size()) > EN_FAZLA_GUN) {
        g_gunler.erase(g_gunler.begin());
    }

    cJSON* k = cJSON_CreateObject();
    cJSON* g = cJSON_CreateObject();
    for (const auto& [gun, dk] : g_gunler) {
        cJSON_AddNumberToObject(g, gun.c_str(), dk);
    }
    cJSON_AddItemToObject(k, "gunler", g);

    char* ham = cJSON_PrintUnformatted(k);
    if (ham != nullptr) {
        nvs_handle_t h;
        if (nvs_open(NVS_ALAN, NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_blob(h, NVS_ANAHTAR, ham, std::strlen(ham));
            nvs_commit(h);
            nvs_close(h);
        }
        cJSON_free(ham);
    }
    cJSON_Delete(k);
}

}  // namespace

// ---------------------------------------------------------------------------

esp_err_t kullanim_baslat()
{
    nvs_handle_t h;
    if (nvs_open(NVS_ALAN, NVS_READONLY, &h) != ESP_OK) return ESP_OK;

    size_t boy = 0;
    if (nvs_get_blob(h, NVS_ANAHTAR, nullptr, &boy) != ESP_OK || boy == 0) {
        nvs_close(h);
        return ESP_OK;
    }
    std::string ham(boy, '\0');
    const esp_err_t s = nvs_get_blob(h, NVS_ANAHTAR, ham.data(), &boy);
    nvs_close(h);
    if (s != ESP_OK) return s;

    cJSON* k = cJSON_Parse(ham.c_str());
    if (k == nullptr) {
        // Bozuk dosya sadece bir sayac; hafiza gibi degerli degil.
        // Sifirdan basliyoruz.
        ESP_LOGW(ETIKET, "kullanim kaydi bozuk, sifirdan baslaniyor");
        return ESP_OK;
    }
    const cJSON* g = cJSON_GetObjectItemCaseSensitive(k, "gunler");
    if (cJSON_IsObject(g)) {
        const cJSON* e = nullptr;
        cJSON_ArrayForEach(e, g) {
            if (e->string != nullptr && cJSON_IsNumber(e)) {
                g_gunler[e->string] = e->valuedouble;
            }
        }
    }
    cJSON_Delete(k);
    ESP_LOGI(ETIKET, "%u gunluk kayit okundu",
             static_cast<unsigned>(g_gunler.size()));
    return ESP_OK;
}

void kullanim_saat_ayarla()
{
    if (g_saat_hazir) return;

    // TR saati. Panelde "bugun" yaziyor ve cocuk Turkiye'de; UTC ile
    // gosterirsek gece yarisindan sonraki konusmalar bir onceki gune
    // yazilir.
    setenv("TZ", "EET-3", 1);
    tzset();

    esp_sntp_config_t k = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    k.start = true;
    k.sync_cb = [](struct timeval*) {
        g_saat_hazir = true;
        ESP_LOGI(ETIKET, "saat ayarlandi");
    };
    if (esp_netif_sntp_init(&k) != ESP_OK) {
        ESP_LOGW(ETIKET, "saat ayarlanamadi — dakikalar toplam olarak "
                         "sayilacak");
    }
}

bool kullanim_saat_hazir() { return g_saat_hazir; }

void kullanim_oturum_basladi()
{
    if (g_oturum_acik) return;
    g_oturum_basi_us = esp_timer_get_time();
    g_oturum_acik = true;
}

void kullanim_oturum_bitti()
{
    if (!g_oturum_acik) return;
    const double dk =
        static_cast<double>(esp_timer_get_time() - g_oturum_basi_us)
        / 1e6 / 60.0;
    g_oturum_acik = false;
    if (dk <= 0) return;
    g_gunler[bugun()] += dk;
    nvs_yaz();
}

void kullanim_duraklat()
{
    if (!g_oturum_acik) return;
    const double dk =
        static_cast<double>(esp_timer_get_time() - g_oturum_basi_us)
        / 1e6 / 60.0;
    g_oturum_acik = false;
    if (dk <= 0) return;
    g_gunler[bugun()] += dk;
    // NVS'e BURADA yaziliyor. Gerekcesi baslik dosyasinda: robotun
    // normal kapanisi fis cekmek, yani "oturum bitti" ani hic gelmiyor.
    nvs_yaz();
    ESP_LOGI(ETIKET, "uyku: sayac durdu (+%.1f dk yazildi)", dk);
}

void kullanim_devam()
{
    if (g_oturum_acik) return;
    g_oturum_basi_us = esp_timer_get_time();
    g_oturum_acik = true;
    ESP_LOGI(ETIKET, "uyandi: sayac yeniden isliyor");
}

KullanimOzeti kullanim_ozet()
{
    KullanimOzeti o;
    const std::string gun = bugun();
    const std::string ay = gun.substr(0, 7);

    // ACIK OTURUM AYRI EKLENIYOR: NVS'e ancak durunca yaziliyor, yoksa
    // panel konusma boyunca hic degismez ve "sayac bozuk" gorunur.
    double acik_dk = 0;
    if (g_oturum_acik) {
        acik_dk = static_cast<double>(esp_timer_get_time() - g_oturum_basi_us)
                  / 1e6 / 60.0;
    }

    for (const auto& [g, dk] : g_gunler) {
        if (g == gun) {
            o.bugun_dk += dk;
        }
        if (g.rfind(ay, 0) == 0) {
            o.ay_dk += dk;
        } else if (g == SAATSIZ) {
            // Saat ayarlanmadan biriken dakikalar KAYBOLMASIN. "Bugun"e
            // yazmak yanlis olur (hangi gun oldugunu bilmiyoruz), ama
            // aylik toplamda gorunmeleri gerekiyor — o dakikalar
            // gercekten harcandi ve faturaya girdi.
            o.ay_dk += dk;
        }
    }
    o.bugun_dk += acik_dk;
    o.ay_dk += acik_dk;
    o.gun_sayisi = static_cast<int>(g_gunler.size());

    // Tahmini tutar: oturum acik kaldigi sure x giris ucreti
    // ($0.005/dk). Cikis ucreti ($0.018/dk) sadece robot KONUSURKEN
    // isliyor ve o sureyi ayri olcmuyoruz — yani bu sayi ALT SINIR.
    // Panelde "tahmini" diye yaziyor.
    o.tahmin_usd = o.ay_dk * 0.005;
    return o;
}

void kullanim_sifirla()
{
    g_gunler.clear();
    g_oturum_acik = false;
    nvs_yaz();
}

}  // namespace pati
