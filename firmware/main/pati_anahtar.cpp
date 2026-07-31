#include "pati_anahtar.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "pati_ag.hpp"

namespace pati {
namespace {

constexpr const char* ETIKET = "anahtar";

// Bolum tablosundaki ad (partitions.csv) ve icindeki ad alani.
constexpr const char* BOLUM = "anahtar";
constexpr const char* NVS_ALAN = "gizli";
constexpr const char* NVS_ANAHTAR = "gemini";

// Bicim sinirlari. Google AI Studio anahtari bugun 39 karakter ve "AIza"
// ile basliyor ama ikisi de BELGELENMIS bir soz degil — Google yarin
// degistirse burada takilip kalirdik. O yuzden sinirlar genis: amac
// yanlis yapistirmayi yakalamak (bos, yarim, cumle), anahtarin gercekten
// calisip calismadigini soylemek degil. Onu yalnizca Google soyler.
constexpr size_t EN_KISA = 20;
constexpr size_t EN_UZUN = 200;

// Dogrulama istegi: model listesinden TEK satir.
//
// generateContent yerine bu kullaniliyor cunku liste istegi belirtec
// harcamiyor ve ucretlendirilmiyor — bozuk anahtari sinamak, calisan bir
// anahtarin kotasindan yememeli.
constexpr const char* DOGRULAMA_URL =
    "https://generativelanguage.googleapis.com/v1beta/models?pageSize=1&key=";

// Hata govdesinden alinan aciklama sinirlanmasin diye degil, yigini
// tuketmesin diye sinirli.
constexpr int CEVAP_SINIRI = 1024;

// Baglanti hatasi ustune atilan dogrulama istekleri arasindaki en kisa
// sure. Sohbet baglantisi kopunca saniyede bir denenebiliyor; her
// birinde Google'a gitmek, 429 yiyen bir anahtari daha da batirmak olurdu.
constexpr std::int64_t DOGRULAMA_ARASI_US = 60LL * 1000 * 1000;

std::mutex g_kilit;
std::string g_anahtar;
AnahtarDurumu g_durum = AnahtarDurumu::Yok;
std::string g_ayrinti;             // Google'in kendi hata cumlesi (Ingilizce)
int g_son_kod = 0;
std::int64_t g_son_dogrulama_us = 0;
bool g_bolum_hazir = false;

// ---------------------------------------------------------------------------
// Bicim
// ---------------------------------------------------------------------------

// Kopyala-yapistirin getirdiklerini temizler.
//
// Anne anahtari Google'in sayfasindan kopyalayacak. Oradan bosluk, satir
// sonu, bazen tirnak da geliyor. Bunlari REDDETMEK yerine temizliyoruz:
// kullanicinin gozune ayni gorunen iki dizeden birini kabul edip otekini
// reddetmek, sebebi anlasilmayan bir hata olur.
std::string kirp(const std::string& ham)
{
    const auto bosluk = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    size_t b = 0;
    size_t s = ham.size();
    while (b < s && bosluk(static_cast<unsigned char>(ham[b]))) ++b;
    while (s > b && bosluk(static_cast<unsigned char>(ham[s - 1]))) --s;

    // Tirnak icinde yapistirilmis olabilir: "AIza..." ya da 'AIza...'
    if (s - b >= 2 && (ham[b] == '"' || ham[b] == '\'') && ham[s - 1] == ham[b]) {
        ++b;
        --s;
    }
    return ham.substr(b, s - b);
}

bool bicim_gecerli(const std::string& a)
{
    if (a.size() < EN_KISA || a.size() > EN_UZUN) return false;
    // Anahtar bir URL sorgu parametresi olarak gidiyor. Bosluk ya da
    // ASCII disi bir karakter varsa ya yanlis yapistirilmis ya da
    // araya baska bir metin karismis.
    for (const unsigned char c : a) {
        if (c <= 0x20 || c >= 0x7f) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// NVS
// ---------------------------------------------------------------------------

bool nvs_oku(std::string& cikti)
{
    if (!g_bolum_hazir) return false;
    nvs_handle_t h;
    if (nvs_open_from_partition(BOLUM, NVS_ALAN, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    char t[EN_UZUN + 1];
    size_t n = sizeof(t);
    const bool tamam =
        (nvs_get_str(h, NVS_ANAHTAR, t, &n) == ESP_OK) && t[0] != '\0';
    if (tamam) cikti = t;
    nvs_close(h);
    return tamam;
}

bool nvs_yaz(const std::string& a)
{
    if (!g_bolum_hazir) return false;
    nvs_handle_t h;
    if (nvs_open_from_partition(BOLUM, NVS_ALAN, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t s = nvs_set_str(h, NVS_ANAHTAR, a.c_str());
    if (s == ESP_OK) s = nvs_commit(h);
    nvs_close(h);
    return s == ESP_OK;
}

// ---------------------------------------------------------------------------
// HTTP durum kodunu duruma cevir
// ---------------------------------------------------------------------------
//
// Google'in dondurdugu kod, anahtarin gecmisine gore degisiyor:
//
//   400  API_KEY_INVALID — anahtar bicimsel olarak kabul edilmedi
//   401  anahtar eksik/gecersiz
//   403  PERMISSION_DENIED — anahtar var ama bu API'ye yetkisi yok,
//        ya da anahtar "sizdirildi" diye iptal edilmis
//   429  RESOURCE_EXHAUSTED — kota doldu ya da odeme yok
//
// 400/401/403'un UCU DE anne icin ayni is: yeni anahtar gerekiyor. Bu
// yuzden tek durumda toplaniyorlar; ayrintiyi (Google'in kendi cumlesi)
// yine de saklayip gunluge yaziyoruz.
//
// ⚠️ 429 icin "kota" diyoruz ama gecici bir hiz siniri da olabilir.
// Panel metni ikisini de kapsiyor; "hemen para yukle" DEMIYOR.
AnahtarDurumu koddan_durum(int kod)
{
    if (kod == 200) return AnahtarDurumu::Gecerli;
    if (kod == 400 || kod == 401 || kod == 403) return AnahtarDurumu::Gecersiz;
    if (kod == 429) return AnahtarDurumu::Kota;
    // 500, 503, 0... Google'in kendi sorunu ya da ag. Anahtari
    // SUCLAMIYORUZ: gecici bir sunucu hatasinda anneye "anahtarin bozuk"
    // demek, olmayan bir isi yaptirmak olurdu.
    return AnahtarDurumu::Ulasilamadi;
}

// Google'in hata govdesinden aciklamayi ceker: {"error":{"message":...}}
std::string ayrintiyi_al(const std::string& govde)
{
    if (govde.empty()) return "";
    cJSON* k = cJSON_Parse(govde.c_str());
    if (k == nullptr) return "";
    std::string cikti;
    const cJSON* h = cJSON_GetObjectItemCaseSensitive(k, "error");
    if (h != nullptr) {
        const cJSON* m = cJSON_GetObjectItemCaseSensitive(h, "message");
        if (cJSON_IsString(m) && m->valuestring != nullptr) {
            cikti = m->valuestring;
            if (cikti.size() > 160) cikti.resize(160);
        }
    }
    cJSON_Delete(k);
    return cikti;
}

const char* durum_adi(AnahtarDurumu d)
{
    switch (d) {
        case AnahtarDurumu::Yok:         return "yok";
        case AnahtarDurumu::Bilinmiyor:  return "bilinmiyor";
        case AnahtarDurumu::Gecerli:     return "gecerli";
        case AnahtarDurumu::Gecersiz:    return "gecersiz";
        case AnahtarDurumu::Kota:        return "kota";
        case AnahtarDurumu::Ulasilamadi: return "ulasilamadi";
    }
    return "bilinmiyor";
}

// JSON dizesine giren metin kacislanmali: Google'in hata cumlesinde
// tirnak ve ters bolu gecebiliyor ve kacislanmazsa panelin JSON'u
// bozulur — panel o zaman HICBIR seyi gosteremez, sadece anahtar
// kutusunu degil.
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
                if (static_cast<unsigned char>(h) < 0x20) break;  // at
                c += h;
        }
    }
    return c;
}

}  // namespace

// ---------------------------------------------------------------------------

esp_err_t anahtar_baslat()
{
    // Bolum VAR MI? Once buna bakiyoruz cunku iki basarisizligin caresi
    // bambaska: bolum yoksa kablo gerekiyor, varsa icerigi silmek yetiyor.
    // Ayirmasak "anahtar bolumu kurulamadi" diye tek bir satir kalirdi ve
    // hangisi oldugu anlasilmazdi.
    if (esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                 ESP_PARTITION_SUBTYPE_DATA_NVS, BOLUM)
        == nullptr) {
        ESP_LOGE(ETIKET, "'%s' diye bir bolum YOK.", BOLUM);
        ESP_LOGE(ETIKET, "  Kart eski bolum tablosuyla yuklenmis. Panelden");
        ESP_LOGE(ETIKET, "  anahtar girilemez ve guncelleme calismaz.");
        ESP_LOGE(ETIKET, "  Care KABLO: idf.py -p <PORT> flash");
        ESP_LOGE(ETIKET, "  (app-flash YETMEZ — bolum tablosunu yazmiyor)");
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t s = nvs_flash_init_partition(BOLUM);
    if (s != ESP_OK) {
        // HANGI HATA OLURSA OLSUN SILIP YENIDEN KURUYORUZ.
        //
        // Baslangicta yalnizca NO_FREE_PAGES ve NEW_VERSION_FOUND
        // yakalaniyordu — ESP-IDF orneklerindeki kaliP. Burada yetmiyor:
        //
        // 🔴 TEK UYGULAMADAN OTA'LI TABLOYA GECEN HER KART BURAYA DUSUYOR.
        // Eski tabloda uygulama 0x10000'de basliyordu; yeni tabloda ayni
        // yerde 'anahtar' var. `idf.py flash` o araligi YAZMIYOR (yalnizca
        // onyukleyici, tablo, otadata ve uygulama yaziliyor), yani bolum
        // eski uygulamanin kod baytlariyla dolu aciliyor. NVS oradan
        // rastgele bir hata dondurebiliyor ve o hatalarin hepsini tek tek
        // saymak, sayilmayan biri cikinca anahtarin HIC yazilamamasi
        // demekti — panel "kaydedildi" der, hicbir sey kaydedilmezdi.
        //
        // Silmenin bedeli yok: bu bolumde tek bir sey var ve zaten
        // okunamiyor. Anne panelden yeniden yaziyor, durum "yok"
        // goruundugu icin panel ne yapmasi gerektigini soyluyor.
        ESP_LOGW(ETIKET, "anahtar bolumu okunamadi (%s), siliniyor",
                 esp_err_to_name(s));
        nvs_flash_erase_partition(BOLUM);
        s = nvs_flash_init_partition(BOLUM);
        if (s != ESP_OK) {
            ESP_LOGE(ETIKET, "anahtar bolumu kurulamadi: %s",
                     esp_err_to_name(s));
            return s;
        }
        ESP_LOGI(ETIKET, "anahtar bolumu yeniden kuruldu");
    }

    g_bolum_hazir = true;

    std::lock_guard<std::mutex> k(g_kilit);
    if (nvs_oku(g_anahtar)) {
        // Kayitli ama HENUZ DENENMEDI. "Gecerli" demek yalan olurdu:
        // anahtar iptal edilmis ya da kotasi bitmis olabilir ve panel
        // acilir acilmaz her sey yolundaymis gibi gorunurdu.
        g_durum = AnahtarDurumu::Bilinmiyor;
        ESP_LOGI(ETIKET, "anahtar kayitli (%u karakter), henuz denenmedi",
                 static_cast<unsigned>(g_anahtar.size()));
    } else {
        g_durum = AnahtarDurumu::Yok;
        ESP_LOGW(ETIKET, "anahtar YOK — panelden girilmesi gerekiyor");
    }
    return ESP_OK;
}

bool anahtar_var()
{
    std::lock_guard<std::mutex> k(g_kilit);
    return !g_anahtar.empty();
}

std::string anahtar_al()
{
    std::lock_guard<std::mutex> k(g_kilit);
    return g_anahtar;
}

bool anahtar_yaz(const std::string& yeni)
{
    const std::string a = kirp(yeni);
    if (!bicim_gecerli(a)) {
        ESP_LOGW(ETIKET, "anahtar reddedildi: bicim (%u karakter)",
                 static_cast<unsigned>(a.size()));
        return false;
    }
    if (!nvs_yaz(a)) {
        ESP_LOGE(ETIKET, "anahtar NVS'e yazilamadi");
        return false;
    }
    {
        std::lock_guard<std::mutex> k(g_kilit);
        g_anahtar = a;
        g_durum = AnahtarDurumu::Bilinmiyor;
        g_ayrinti.clear();
        g_son_kod = 0;
        // Zamanlayici sifirlaniyor: YENI anahtar hemen denenebilmeli.
        // Eskisinin hatasi yuzunden konmus bekleme suresi yeni anahtari
        // bekletmemeli — anne tam o an panelde duruyor ve sonucu
        // gormeyi bekliyor.
        g_son_dogrulama_us = 0;
    }
    ESP_LOGI(ETIKET, "yeni anahtar kaydedildi (%u karakter)",
             static_cast<unsigned>(a.size()));
    return true;
}

void anahtar_sil()
{
    if (g_bolum_hazir) {
        nvs_handle_t h;
        if (nvs_open_from_partition(BOLUM, NVS_ALAN, NVS_READWRITE, &h)
            == ESP_OK) {
            nvs_erase_key(h, NVS_ANAHTAR);
            nvs_commit(h);
            nvs_close(h);
        }
    }
    std::lock_guard<std::mutex> k(g_kilit);
    g_anahtar.clear();
    g_durum = AnahtarDurumu::Yok;
    g_ayrinti.clear();
    g_son_kod = 0;
    ESP_LOGW(ETIKET, "anahtar silindi");
}

AnahtarDurumu anahtar_durumu()
{
    std::lock_guard<std::mutex> k(g_kilit);
    return g_durum;
}

// ---------------------------------------------------------------------------

AnahtarDurumu anahtar_dogrula()
{
    const std::string a = anahtar_al();
    if (a.empty()) {
        std::lock_guard<std::mutex> k(g_kilit);
        g_durum = AnahtarDurumu::Yok;
        return g_durum;
    }

    // Aga cikamiyorsak istek atmanin anlami yok ve sonucu YANILTICI
    // olurdu: baglanti hatasini anahtarin sucu gibi kaydederdik.
    if (ag_durumu() != AgDurumu::Bagli) {
        std::lock_guard<std::mutex> k(g_kilit);
        if (g_durum != AnahtarDurumu::Gecersiz && g_durum != AnahtarDurumu::Kota) {
            g_durum = AnahtarDurumu::Bilinmiyor;
        }
        return g_durum;
    }

    // Zaman damgasi istekten ONCE konuyor: istek 15 saniye surebiliyor
    // ve o sirada gelen ikinci bir cagri (sohbet yeniden baglanmaya
    // calisiyor) beklemeye takilsin, ustune bir istek daha atmasin.
    //
    // Kilit altinda cunku 64 bitlik yazma ESP32'de tek islemde olmuyor;
    // korumasiz birakilsa yarim okunan bir damga beklemeyi bosa
    // cikarabilirdi.
    {
        std::lock_guard<std::mutex> k(g_kilit);
        g_son_dogrulama_us = esp_timer_get_time();
    }

    const std::string url = std::string(DOGRULAMA_URL) + a;

    esp_http_client_config_t ayar{};
    ayar.url = url.c_str();
    ayar.method = HTTP_METHOD_GET;
    ayar.crt_bundle_attach = esp_crt_bundle_attach;
    ayar.timeout_ms = 15000;
    ayar.buffer_size = 1024;
    ayar.buffer_size_tx = 1024;

    esp_http_client_handle_t c = esp_http_client_init(&ayar);
    if (c == nullptr) {
        std::lock_guard<std::mutex> k(g_kilit);
        g_durum = AnahtarDurumu::Ulasilamadi;
        return g_durum;
    }

    int kod = 0;
    std::string govde;

    const esp_err_t s = esp_http_client_open(c, 0);
    if (s == ESP_OK && esp_http_client_fetch_headers(c) >= 0) {
        kod = esp_http_client_get_status_code(c);
        std::vector<char> tampon(512);
        while (static_cast<int>(govde.size()) < CEVAP_SINIRI) {
            const int n = esp_http_client_read(c, tampon.data(),
                                               static_cast<int>(tampon.size()));
            if (n <= 0) break;
            govde.append(tampon.data(), static_cast<size_t>(n));
        }
    } else {
        ESP_LOGW(ETIKET, "dogrulama istegi gonderilemedi: %s",
                 esp_err_to_name(s));
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);

    const AnahtarDurumu d = koddan_durum(kod);
    const std::string ayrinti = (kod == 200) ? std::string() : ayrintiyi_al(govde);

    {
        std::lock_guard<std::mutex> k(g_kilit);
        g_durum = d;
        g_son_kod = kod;
        g_ayrinti = ayrinti;
    }

    if (d == AnahtarDurumu::Gecerli) {
        ESP_LOGI(ETIKET, "anahtar gecerli");
    } else {
        ESP_LOGW(ETIKET, "anahtar durumu: %s (HTTP %d) %s", durum_adi(d), kod,
                 ayrinti.c_str());
    }
    return d;
}

void anahtar_kod_bildir(int http_kod)
{
    // 0 = istek hic gitmedi. Bunu "ag" sayiyoruz, anahtarin sorunu degil.
    const AnahtarDurumu d = koddan_durum(http_kod);

    std::lock_guard<std::mutex> k(g_kilit);
    if (g_anahtar.empty()) {
        g_durum = AnahtarDurumu::Yok;
        return;
    }
    // AG HATASI, BILINEN BIR SORUNUN UZERINE YAZILMIYOR.
    //
    // Anahtar gecersizken robot Google'a ulasamaz hale gelirse durum
    // "ulasilamadi"ya donerdi ve panel "ag sorunu, bekleyin" derdi. Anne
    // bekler, hicbir sey degismez. Bilinen kotu durum korunuyor; bir
    // sonraki BASARILI istek onu zaten temizliyor.
    if (d == AnahtarDurumu::Ulasilamadi
        && (g_durum == AnahtarDurumu::Gecersiz || g_durum == AnahtarDurumu::Kota)) {
        return;
    }
    g_durum = d;
    g_son_kod = http_kod;
    if (d == AnahtarDurumu::Gecerli) g_ayrinti.clear();
}

namespace {

void sorma_gorevi(void*)
{
    anahtar_dogrula();
    vTaskDelete(nullptr);
}

}  // namespace

void anahtar_baglanti_hatasi()
{
    {
        std::lock_guard<std::mutex> k(g_kilit);
        if (g_anahtar.empty()) {
            g_durum = AnahtarDurumu::Yok;
            return;
        }
        const std::int64_t simdi = esp_timer_get_time();
        if (g_son_dogrulama_us != 0
            && simdi - g_son_dogrulama_us < DOGRULAMA_ARASI_US) {
            return;
        }
    }

    // ⚠️ AYRI GOREVDE, CUNKU CAGIRAN YERLER BEKLEYEMEZ.
    //
    // Burayi cagiranlar: uyandirma (mikrofon gorevi), oturum yenileme ve
    // sohbet kurulumu. Ilki gecikme yolunun tam ustunde — orada 15
    // saniyelik bir TLS istegini beklemek cocugun sesini kesmek olurdu.
    //
    // 🔴 Ustelik bloklamak YANLIS CEVAP da veriyordu: 31.07.2026'da
    // acilista app_main'den bloklu cagriliyordu ve gozler gorevi CPU 0'i
    // doyurdugu icin TLS el sikismasi ac kalip dusuyordu
    // (mbedtls -0x0050). Anahtar saglamken panel "Google'a
    // ulasilamiyor" yaziyordu.
    //
    // Gorev acilamazsa SESSIZ KALIYORUZ: durum "bilinmiyor" olarak
    // kalir, ki bu dogru — sormadik, bilmiyoruz.
    if (xTaskCreate(sorma_gorevi, "pati_anh_s", 6144, nullptr, 3, nullptr)
        != pdPASS) {
        ESP_LOGW(ETIKET, "sorma gorevi acilamadi — durum bilinmiyor kaliyor");
    }
}

std::string anahtar_json()
{
    std::lock_guard<std::mutex> k(g_kilit);
    std::string j = "\"anahtar\":{\"var\":";
    j += g_anahtar.empty() ? "false" : "true";
    j += ",\"durum\":\"";
    j += durum_adi(g_durum);
    j += "\",\"kod\":";
    j += std::to_string(g_son_kod);

    // ANAHTARIN KENDISI GONDERILMIYOR. Panel onu geri gostermeye
    // calismamali: sayfa ev agindaki herkese acik ve anahtar orada
    // durursa aga giren biri onu okur. Anne yalnizca YENI yazabiliyor,
    // yazdigini geri okuyamiyor.
    //
    // Son dort karakter YETERLI ve gerekli: anne iki farkli anahtar
    // arasinda kaldiginda hangisinin yazili oldugunu ancak boyle
    // ayirt edebiliyor.
    j += ",\"kuyruk\":\"";
    j += (g_anahtar.size() >= 4) ? g_anahtar.substr(g_anahtar.size() - 4) : "";
    j += "\"";

    if (!g_ayrinti.empty()) {
        j += ",\"ayrinti\":\"";
        j += json_kacisla(g_ayrinti);
        j += "\"";
    }
    j += "}";
    return j;
}

}  // namespace pati
