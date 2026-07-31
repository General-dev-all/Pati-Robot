#include "pati_cikarim.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>

#include "pati_cikarim_uretilmis.h"
#include "pati_hafiza.hpp"

namespace pati {
namespace {

constexpr const char* ETIKET = "cikarim";

// Cevap tamponu. 800 cikti belirteci istiyoruz; JSON sarmalamasiyla
// birlikte 8 KB fazlasiyla yeterli. Sinirsiz okumak yigini tuketmeye
// acik kapi olurdu.
constexpr int CEVAP_SINIRI = 8192;

std::string g_dokum;

}  // namespace

// ---------------------------------------------------------------------------

void cikarim_dokum_ekle(const char* kim, const std::string& metin)
{
    if (metin.empty()) return;

    // Sinir dolduysa YENI metni atiyoruz, eskisini korumuyoruz degil —
    // KORUYORUZ. Sebep: konusmanin basinda tanisma geciyor ("benim
    // adim ...") ve en degerli bilgi orada.
    const size_t eklenecek = std::strlen(kim) + 2 + metin.size() + 1;
    if (g_dokum.size() + eklenecek >
        static_cast<size_t>(CIKARIM_DOKUM_EN_FAZLA)) {
        return;
    }

    g_dokum += kim;
    g_dokum += ": ";
    g_dokum += metin;
    g_dokum += "\n";
}

bool cikarim_dokum_var() { return !g_dokum.empty(); }
size_t cikarim_dokum_boyu() { return g_dokum.size(); }

// ---------------------------------------------------------------------------

namespace {

// Modelin cevabindan ilk JSON dizisini ceker.
//
// Model bazen ```json ile sariyor — prototype/hafiza.py'de de ayni sorun
// var ve ayni sekilde cozuluyor: ilk '[' ile son ']' arasi.
// Ad ve yas suzgecleri ARTIK BURADA DEGIL: pati_hafiza.cpp'ye tasindi.
//
// Sebep: Python'da da hafiza.py'nin icindeler ve konak testi
// (test/hafiza_karsilastir.cpp) yalnizca hafiza motorunu derliyor —
// burada kaldiklari surece iki taraf KARSILASTIRILAMIYORDU. Nitekim
// sessizce ayrilmislardi: buradaki surum ASCII'ye indirmeden
// karsilastirdigi icin "Boş" gibi bir yer tutucuyu eliyemiyor, ve
// robotun adini yalnizca sabit "pati" ile ariyordu — cocuk robota
// "Osman" dediyse o ad cocugun adi olarak kaydedilebiliyordu.
int yas_gecerli_mi(const cJSON* y)
{
    if (!cJSON_IsNumber(y)) return 0;
    return hafiza_yas_gecerli_mi(y->valueint);
}

std::vector<std::string> diziyi_ayikla(const std::string& ham,
                                       std::string& bulunan_ad,
                                       int& bulunan_yas,
                                       std::string& bulunan_robot_adi)
{
    std::vector<std::string> cikti;
    const auto bas = ham.find('[');
    const auto son = ham.rfind(']');
    if (bas == std::string::npos || son == std::string::npos || son <= bas) {
        return cikti;
    }

    cJSON* d = cJSON_ParseWithLength(ham.data() + bas, son - bas + 1);
    if (d == nullptr || !cJSON_IsArray(d)) {
        if (d != nullptr) cJSON_Delete(d);
        return cikti;
    }

    const cJSON* e = nullptr;
    cJSON_ArrayForEach(e, d) {
        if (cJSON_IsString(e) && e->valuestring != nullptr) {
            cikti.emplace_back(e->valuestring);
            continue;
        }
        // Dizinin ilk elemani {"ad": "...", "yas": 7} olabiliyor
        // (prompt boyle istiyor).
        if (cJSON_IsObject(e)) {
            // Cocuk robota yeni bir ad takti. Bilgilerden AYRI
            // tutuluyor: bu robotun kimligi, atilabilir bir bilgi
            // degil (bkz. pati_hafiza.cpp §g_robot_adi).
            const cJSON* r =
                cJSON_GetObjectItemCaseSensitive(e, "robot_adi");
            if (cJSON_IsString(r) && r->valuestring != nullptr) {
                bulunan_robot_adi = r->valuestring;
            }
            const cJSON* ad = cJSON_GetObjectItemCaseSensitive(e, "ad");
            if (cJSON_IsString(ad) && ad->valuestring != nullptr) {
                if (hafiza_cocuk_adi_gecerli_mi(ad->valuestring)) {
                    bulunan_ad = ad->valuestring;
                } else {
                    // Sessizce yutmuyoruz: bu suzgec olculmus bir hatayi
                    // kapatiyor, yanlis calisirsa gunlukte gorunmeli.
                    ESP_LOGW(ETIKET, "ad reddedildi: %s", ad->valuestring);
                }
            }
            const int yas = yas_gecerli_mi(
                cJSON_GetObjectItemCaseSensitive(e, "yas"));
            if (yas != 0) bulunan_yas = yas;
        }
    }
    cJSON_Delete(d);
    return cikti;
}

std::string promptu_kur()
{
    std::string bilinen;
    const auto& b = hafiza_bilgiler();
    // Son N kayit — prototype ile ayni sinir.
    const int bas = std::max(0, static_cast<int>(b.size())
                                    - CIKARIM_BILINEN_EN_FAZLA);
    for (int i = bas; i < static_cast<int>(b.size()); ++i) {
        bilinen += "- ";
        bilinen += b[i].metin;
        bilinen += "\n";
    }
    if (bilinen.empty()) bilinen = "(henuz hicbir sey)";

    // Bilinen adi prompta KOYUYORUZ. Sebep: cikarim dokumun sadece yeni
    // parcasini goruyor, o parcada ad genelde gecmiyor ve model bosluk
    // gorunce doldurmaya calisiyor ("Bilinmiyor" yazip dogru adin
    // uzerine yaziyordu). prototype/hafiza.py §cikarim_promptu ile ayni.
    const std::string mevcut_ad = hafiza_cocuk().ad;
    std::string ad_durumu;
    if (mevcut_ad.empty()) {
        ad_durumu = "Cocugun adi henuz bilinmiyor.";
    } else {
        ad_durumu = "COCUGUN ADI ZATEN BILINIYOR: " + mevcut_ad
                    + ". Cocuk bu konusmada YENI bir ad soylemedikce "
                      "{\"ad\": ...} yazma.";
    }

    std::string p = CIKARIM_PROMPTU;
    const auto yer_degistir = [&p](const char* isaret, const std::string& yeni) {
        const auto i = p.find(isaret);
        if (i != std::string::npos) p.replace(i, std::strlen(isaret), yeni);
    };
    yer_degistir("{ad_durumu}", ad_durumu);
    yer_degistir("{bilinen}", bilinen);
    yer_degistir("{konusma}", g_dokum);
    return p;
}

}  // namespace

// ---------------------------------------------------------------------------

int cikarim_calistir(const char* api_anahtari)
{
    if (g_dokum.empty()) return 0;
    if (api_anahtari == nullptr || api_anahtari[0] == '\0') {
        ESP_LOGE(ETIKET, "API anahtari yok — cikarim yapilamiyor");
        return -1;
    }

    // --- istek govdesi
    cJSON* k = cJSON_CreateObject();
    cJSON* icerikler = cJSON_CreateArray();
    cJSON* icerik = cJSON_CreateObject();
    cJSON* parcalar = cJSON_CreateArray();
    cJSON* parca = cJSON_CreateObject();
    const std::string p = promptu_kur();
    cJSON_AddStringToObject(parca, "text", p.c_str());
    cJSON_AddItemToArray(parcalar, parca);
    cJSON_AddItemToObject(icerik, "parts", parcalar);
    cJSON_AddItemToArray(icerikler, icerik);
    cJSON_AddItemToObject(k, "contents", icerikler);

    cJSON* uretim = cJSON_CreateObject();
    // Sicaklik 0: cikarim yaratici olmamali, ayni konusmadan ayni
    // bilgiyi cikarmali.
    cJSON_AddNumberToObject(uretim, "temperature", 0.0);
    cJSON_AddNumberToObject(uretim, "maxOutputTokens", 800);
    cJSON_AddItemToObject(k, "generationConfig", uretim);

    char* govde = cJSON_PrintUnformatted(k);
    cJSON_Delete(k);
    if (govde == nullptr) return -1;

    // --- istek
    std::string url = "https://generativelanguage.googleapis.com/v1beta/models/";
    url += CIKARIM_MODELI;
    url += ":generateContent?key=";
    url += api_anahtari;

    esp_http_client_config_t ayar{};
    ayar.url = url.c_str();
    ayar.method = HTTP_METHOD_POST;
    // TLS: ESP-IDF'in kok sertifika demeti. Kendi sertifikamizi gomsek
    // Google onu degistirdigi gun robot sessizce ogrenmeyi birakirdi.
    ayar.crt_bundle_attach = esp_crt_bundle_attach;
    ayar.timeout_ms = 30000;
    ayar.buffer_size = 2048;
    ayar.buffer_size_tx = 2048;

    esp_http_client_handle_t c = esp_http_client_init(&ayar);
    if (c == nullptr) {
        cJSON_free(govde);
        return -1;
    }
    esp_http_client_set_header(c, "Content-Type", "application/json");

    std::string cevap;
    int eklenen = -1;

    esp_err_t s = esp_http_client_open(c, static_cast<int>(std::strlen(govde)));
    if (s != ESP_OK) {
        ESP_LOGE(ETIKET, "baglanti acilamadi: %s", esp_err_to_name(s));
        goto bitir;
    }
    if (esp_http_client_write(c, govde, std::strlen(govde)) < 0) {
        ESP_LOGE(ETIKET, "istek gonderilemedi");
        goto bitir;
    }
    if (esp_http_client_fetch_headers(c) < 0) {
        ESP_LOGE(ETIKET, "cevap basliklari okunamadi");
        goto bitir;
    }
    {
        const int kod = esp_http_client_get_status_code(c);
        std::vector<char> tampon(1024);
        while (static_cast<int>(cevap.size()) < CEVAP_SINIRI) {
            const int n = esp_http_client_read(c, tampon.data(),
                                               static_cast<int>(tampon.size()));
            if (n <= 0) break;
            cevap.append(tampon.data(), static_cast<size_t>(n));
        }
        if (kod != 200) {
            // Kodu VE govdeyi yaz: 400 ile 429 bambaska iki sorun
            // (bozuk istek vs kota doldu) ve sadece kod yaziinca hangisi
            // oldugu anlasilmiyor.
            ESP_LOGE(ETIKET, "HTTP %d: %.200s", kod, cevap.c_str());
            goto bitir;
        }
    }

    {
        // --- cevabi coz
        cJSON* c2 = cJSON_Parse(cevap.c_str());
        if (c2 == nullptr) {
            ESP_LOGE(ETIKET, "cevap cozulemedi: %.200s", cevap.c_str());
            goto bitir;
        }
        const cJSON* aday = cJSON_GetObjectItemCaseSensitive(c2, "candidates");
        const cJSON* ilk = cJSON_IsArray(aday) ? cJSON_GetArrayItem(aday, 0)
                                               : nullptr;
        const cJSON* ic = ilk ? cJSON_GetObjectItemCaseSensitive(ilk, "content")
                              : nullptr;
        const cJSON* prc = ic ? cJSON_GetObjectItemCaseSensitive(ic, "parts")
                              : nullptr;
        const cJSON* p0 = cJSON_IsArray(prc) ? cJSON_GetArrayItem(prc, 0)
                                             : nullptr;
        const cJSON* yazi = p0 ? cJSON_GetObjectItemCaseSensitive(p0, "text")
                               : nullptr;

        if (!cJSON_IsString(yazi) || yazi->valuestring == nullptr) {
            ESP_LOGE(ETIKET, "cevapta metin yok");
            cJSON_Delete(c2);
            goto bitir;
        }

        std::string ad;
        std::string yeni_robot_adi;
        int yas = 0;
        const auto bilgiler =
            diziyi_ayikla(yazi->valuestring, ad, yas, yeni_robot_adi);
        cJSON_Delete(c2);

        eklenen = 0;
        if (!yeni_robot_adi.empty()) {
            // Ad promptun ILK CUMLESINDE geciyor. prompt_kur() her
            // oturum acilisinda calistigi icin uyandiginda gecerli
            // oluyor — cikarim zaten uykuda yapiliyor.
            hafiza_robot_adini_degistir(yeni_robot_adi);
        }
        if (!ad.empty()) {
            hafiza_cocugu_tanimla(ad, 0);
            ESP_LOGI(ETIKET, "adini ogrendi: %s", ad.c_str());
        }
        if (yas != 0) {
            hafiza_cocugu_tanimla("", yas);
            ESP_LOGI(ETIKET, "yasi ogrendi: %d", yas);
        }
        for (const auto& b : bilgiler) {
            if (hafiza_bilgi_ekle(b, "cikarim")) {
                ESP_LOGI(ETIKET, "hatirladi: %s", b.c_str());
                ++eklenen;
            }
        }

        // BASARILI: dokum temizleniyor. Basarisiz olsaydi
        // TEMIZLEMEZDIK — bir sonraki uykuda tekrar denensin.
        g_dokum.clear();
    }

bitir:
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    cJSON_free(govde);
    return eklenen;
}

}  // namespace pati
