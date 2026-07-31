#include "pati_hafiza.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>

#include <cJSON.h>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

// Robotun dogustan gelen adi (PATI_ROBOT_ADI) buradan geliyor.
// Tek kaynak prototype/kisilik.py; baslik prompt_uret.py ile uretiliyor.
#include "pati_kisilik_uretilmis.h"

namespace pati {
namespace {

constexpr const char* ETIKET = "hafiza";
constexpr const char* NVS_ALAN = "pati";
constexpr const char* NVS_ANAHTAR = "hafiza";
constexpr const char* NVS_BOZUK = "hafiza_bozuk";

Cocuk g_cocuk;

// Cocugun robota taktigi ad ("bundan sonra senin adin Osman").
//
// NEDEN BILGILERDEN AYRI: bu bir "bilgi" degil, robotun KIMLIGI.
// Bilgiler listesi benzerlikle birlesiyor, tekrar sayisina gore
// siralaniyor ve sinira gelince ATILIYOR — robotun adi atilamaz.
// Ayrica sistem promptunun ILK CUMLESINDE geciyor, bilgiler ise sonda.
//
// Bos ise varsayilan (PATI_ROBOT_ADI) kullaniliyor. Tek kisa dize;
// NVS'te en fazla 40 bayt. (prototype/hafiza.py §BOS_HAFIZA robot_adi)
std::string g_robot_adi;
std::vector<Bilgi> g_bilgiler;
std::string g_ebeveyn_notu;
int g_oturum = 0;
bool g_hazir = false;

// ---------------------------------------------------------------------------
// UTF-8 — kod noktasi bazli islemler
// ---------------------------------------------------------------------------
//
// Python metni karakter olarak isliyor; bayt uzerinden calismak Turkce'de
// bozuyor (bkz. baslik dosyasi). Burada her sey kod noktasi.

// Bir kod noktasinin kac bayt oldugunu ilk bayttan soyluyor.
inline int utf8_uzunluk(unsigned char b)
{
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;   // bozuk bayt: bir bayt ilerle, dizi kilitlenmesin
}

// Metni kod noktalarina ayirir (her biri kendi baytlariyla).
std::vector<std::string> utf8_parcala(const std::string& m)
{
    std::vector<std::string> p;
    for (size_t i = 0; i < m.size();) {
        const int n = utf8_uzunluk(static_cast<unsigned char>(m[i]));
        p.push_back(m.substr(i, static_cast<size_t>(n)));
        i += static_cast<size_t>(n);
    }
    return p;
}

// Turkce harfler. Python'un `\w` sinifi bunlari kelime karakteri
// sayiyor; ASCII disindaki her seyi kelime saymak yanlis olurdu
// (Unicode noktalama da >= 0x80: ’ — … “ ”).
const std::set<std::string> TURKCE_HARF = {
    "ç", "Ç", "ğ", "Ğ", "ı", "I", "İ", "i", "ö", "Ö", "ş", "Ş", "ü", "Ü",
    "â", "Â", "î", "Î", "û", "Û",
};

bool kelime_mi(const std::string& kn)
{
    if (kn.size() == 1) {
        const unsigned char c = static_cast<unsigned char>(kn[0]);
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_';
    }
    return TURKCE_HARF.count(kn) > 0;
}

bool bosluk_mu(const std::string& kn)
{
    if (kn.size() != 1) return false;
    const char c = kn[0];
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
           c == '\v';
}

// Kesme isareti: ASCII ' ve tipografik ’
bool kesme_mi(const std::string& kn)
{
    return kn == "'" || kn == "’";
}

}  // namespace

// ---------------------------------------------------------------------------

std::string hafiza_kucult(const std::string& m)
{
    // Python: m.replace("İ","i").replace("I","ı").lower()
    // Sira ONEMLI: once İ, sonra I. Ters yapilirsa "İ" once "ı" olur.
    std::string s;
    s.reserve(m.size());
    for (const auto& kn : utf8_parcala(m)) {
        if (kn == "İ") { s += "i"; continue; }
        if (kn == "I") { s += "ı"; continue; }
        if (kn == "Ç") { s += "ç"; continue; }
        if (kn == "Ğ") { s += "ğ"; continue; }
        if (kn == "Ö") { s += "ö"; continue; }
        if (kn == "Ş") { s += "ş"; continue; }
        if (kn == "Ü") { s += "ü"; continue; }
        if (kn == "Â") { s += "â"; continue; }
        if (kn == "Î") { s += "î"; continue; }
        if (kn == "Û") { s += "û"; continue; }
        if (kn.size() == 1) {
            const char c = kn[0];
            s += (c >= 'A' && c <= 'Z')
                     ? static_cast<char>(c - 'A' + 'a')
                     : c;
            continue;
        }
        s += kn;
    }
    return s;
}

std::string hafiza_kirp(const std::string& m)
{
    // Python `str.strip()` karsiligi. Ad suzgeci ile bilgi ekleme AYNI
    // kirpmayi kullanmali; iki yerde iki farkli kirpma, " Ali " gibi bir
    // adin birinde gecip otekinde elenmesi demek olurdu.
    const auto bas = m.find_first_not_of(" \t\n\r");
    if (bas == std::string::npos) return "";
    const auto son = m.find_last_not_of(" \t\n\r");
    return m.substr(bas, son - bas + 1);
}

std::string hafiza_sadele(const std::string& m)
{
    // Python: metin.sadele() = kucult() + Turkce harfleri ASCII'ye indir.
    //
    // Ad suzgecinde bu SART: yer tutucu listesi ASCII ("bos", "yok").
    // Sadece kucuk harfe cevirseydik model "Boş" dondurdugunde "boş"
    // ile "bos" esitlenmez ve yer tutucu ADI OLARAK kaydedilirdi.
    // Python'da bu satir zaten boyle; iki taraf ayni olmali.
    std::string s;
    s.reserve(m.size());
    for (const auto& kn : utf8_parcala(hafiza_kucult(m))) {
        if (kn == "ç") { s += "c"; continue; }
        if (kn == "ğ") { s += "g"; continue; }
        if (kn == "ı") { s += "i"; continue; }
        if (kn == "ö") { s += "o"; continue; }
        if (kn == "ş") { s += "s"; continue; }
        if (kn == "ü") { s += "u"; continue; }
        if (kn == "â") { s += "a"; continue; }
        if (kn == "î") { s += "i"; continue; }
        if (kn == "û") { s += "u"; continue; }
        s += kn;
    }
    return s;
}

std::vector<std::string> hafiza_kokler(const std::string& m)
{
    // Python sirasi:
    //   1. kucult()
    //   2. _EK  = r"['’]\w*"  -> kesme ve ARDINDAKI kelime karakterlerini SIL
    //   3. _NOKTALAMA = r"[^\w\s]" -> bosluga cevir
    //   4. split(), len > 1 olanlari al, ilk 5 karakteri govde yap
    const auto kn = utf8_parcala(hafiza_kucult(m));

    // 2 + 3: tek gecişte
    std::vector<std::string> temiz;
    for (size_t i = 0; i < kn.size(); ++i) {
        if (kesme_mi(kn[i])) {
            // Kesmeyi ve ardindaki butun kelime karakterlerini AT.
            ++i;
            while (i < kn.size() && kelime_mi(kn[i])) ++i;
            --i;   // dis dongu ++ yapacak
            continue;
        }
        if (kelime_mi(kn[i]) || bosluk_mu(kn[i])) {
            temiz.push_back(kn[i]);
        } else {
            temiz.push_back(" ");   // noktalama -> bosluk
        }
    }

    // 4: kelimelere ayir, govdele
    std::vector<std::string> kokler;
    std::vector<std::string> kelime;
    auto kelimeyi_bitir = [&]() {
        if (kelime.size() > 1) {
            std::string govde;
            const size_t n = std::min(kelime.size(),
                                      static_cast<size_t>(HAFIZA_GOVDE_UZUNLUK));
            for (size_t k = 0; k < n; ++k) govde += kelime[k];
            // Kume: ayni govde iki kez sayilmasin (Python set kullaniyor).
            if (std::find(kokler.begin(), kokler.end(), govde) == kokler.end()) {
                kokler.push_back(govde);
            }
        }
        kelime.clear();
    };
    for (const auto& c : temiz) {
        if (bosluk_mu(c)) {
            kelimeyi_bitir();
        } else {
            kelime.push_back(c);
        }
    }
    kelimeyi_bitir();
    return kokler;
}

namespace {

int kesisim_sayisi(const std::vector<std::string>& a,
                   const std::vector<std::string>& b)
{
    int n = 0;
    for (const auto& x : a) {
        if (std::find(b.begin(), b.end(), x) != b.end()) ++n;
    }
    return n;
}

bool alt_kume_mi(const std::vector<std::string>& kucuk,
                 const std::vector<std::string>& buyuk)
{
    for (const auto& x : kucuk) {
        if (std::find(buyuk.begin(), buyuk.end(), x) == buyuk.end()) {
            return false;
        }
    }
    return true;
}

// Cumlenin anlamini TERSINE ceviren kelimeler. Alt kume kuralini
// uygularken dikkat etmezsek "Annesi ogretmen" ile "Annesi ogretmen
// degil" ayni sayilir.
const std::vector<std::string> OLUMSUZ = {
    "degil", "değil", "yok", "hic", "hiç", "asla", "hicbir", "hiçbir",
};

std::vector<std::string> olumsuz_govdeler()
{
    std::vector<std::string> g;
    for (const auto& k : OLUMSUZ) {
        const auto kn = utf8_parcala(k);
        std::string govde;
        const size_t n = std::min(kn.size(),
                                  static_cast<size_t>(HAFIZA_GOVDE_UZUNLUK));
        for (size_t i = 0; i < n; ++i) govde += kn[i];
        g.push_back(govde);
    }
    return g;
}

}  // namespace

float hafiza_benzerlik(const std::string& a, const std::string& b)
{
    const auto ka = hafiza_kokler(a);
    const auto kb = hafiza_kokler(b);
    if (ka.empty() || kb.empty()) return 0.0f;
    const int kesisim = kesisim_sayisi(ka, kb);
    const int birlesim = static_cast<int>(ka.size() + kb.size()) - kesisim;
    if (birlesim <= 0) return 0.0f;
    return static_cast<float>(kesisim) / static_cast<float>(birlesim);
}

bool hafiza_ayni_bilgi_mi(const std::string& a, const std::string& b)
{
    const auto ka = hafiza_kokler(a);
    const auto kb = hafiza_kokler(b);
    if (ka.empty() || kb.empty()) return false;

    const int kesisim = kesisim_sayisi(ka, kb);
    const int birlesim = static_cast<int>(ka.size() + kb.size()) - kesisim;
    if (birlesim > 0 &&
        static_cast<float>(kesisim) / static_cast<float>(birlesim)
            >= HAFIZA_BENZERLIK_ESIGI) {
        return true;
    }

    // ALT KUME kurali: "Basketbol oynuyor" cumlesi "Okul takiminda
    // basketbol oynuyor" icinde geciyor. Jaccard bunu 0,50 veriyor ve
    // ikisini ayri kaydediyordu.
    //
    // Python `kucuk < buyuk` yaziyor: GERCEK alt kume, yani esit
    // olmamali. Esitse Jaccard 1,0 verirdi ve yukarida yakalanirdi.
    const auto& kucuk = (ka.size() <= kb.size()) ? ka : kb;
    const auto& buyuk = (ka.size() <= kb.size()) ? kb : ka;
    if (kucuk.size() >= 2 && kucuk.size() < buyuk.size() &&
        alt_kume_mi(kucuk, buyuk)) {
        // Fark olumsuzluk kelimesi iceriyorsa AYNI SAYMA.
        const auto olumsuz = olumsuz_govdeler();
        for (const auto& x : buyuk) {
            if (std::find(kucuk.begin(), kucuk.end(), x) != kucuk.end()) {
                continue;
            }
            if (std::find(olumsuz.begin(), olumsuz.end(), x) != olumsuz.end()) {
                return false;
            }
        }
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Kod noktasi sayisi (sinir denetimleri icin)
// ---------------------------------------------------------------------------

namespace {

int kn_sayisi(const std::string& m)
{
    int n = 0;
    for (size_t i = 0; i < m.size();) {
        i += static_cast<size_t>(utf8_uzunluk(static_cast<unsigned char>(m[i])));
        ++n;
    }
    return n;
}

std::string kn_kes(const std::string& m, int en_fazla)
{
    int n = 0;
    size_t i = 0;
    while (i < m.size() && n < en_fazla) {
        i += static_cast<size_t>(utf8_uzunluk(static_cast<unsigned char>(m[i])));
        ++n;
    }
    return m.substr(0, i);
}

// ---------------------------------------------------------------------------
// Serilestirme
// ---------------------------------------------------------------------------

std::string json_uret()
{
    cJSON* k = cJSON_CreateObject();
    cJSON_AddNumberToObject(k, "surum", 1);

    cJSON* c = cJSON_CreateObject();
    if (g_cocuk.ad.empty()) {
        cJSON_AddNullToObject(c, "ad");
    } else {
        cJSON_AddStringToObject(c, "ad", g_cocuk.ad.c_str());
    }
    if (g_cocuk.yas > 0) {
        cJSON_AddNumberToObject(c, "yas", g_cocuk.yas);
    } else {
        cJSON_AddNullToObject(c, "yas");
    }
    cJSON_AddItemToObject(k, "cocuk", c);

    cJSON* d = cJSON_CreateArray();
    for (const auto& b : g_bilgiler) {
        cJSON* e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "id", b.id);
        cJSON_AddStringToObject(e, "metin", b.metin.c_str());
        cJSON_AddStringToObject(e, "kaynak", b.kaynak.c_str());
        cJSON_AddStringToObject(e, "tarih", b.tarih.c_str());
        cJSON_AddStringToObject(e, "son_gorulme", b.son_gorulme.c_str());
        cJSON_AddNumberToObject(e, "kez", b.kez);
        cJSON_AddItemToArray(d, e);
    }
    cJSON_AddItemToObject(k, "bilgiler", d);

    cJSON_AddStringToObject(k, "ebeveyn_notu", g_ebeveyn_notu.c_str());

    // Cocugun robota taktigi ad. Bos ise alani hic yazmiyoruz: eski
    // kayitlarla uyumlu kaliyor ve bos hafiza birkac bayt kuculuyor.
    if (!g_robot_adi.empty()) {
        cJSON_AddStringToObject(k, "robot_adi", g_robot_adi.c_str());
    }

    cJSON* i = cJSON_CreateObject();
    cJSON_AddNumberToObject(i, "oturum", g_oturum);
    cJSON_AddItemToObject(k, "istatistik", i);

    char* ham = cJSON_PrintUnformatted(k);
    std::string s = (ham != nullptr) ? ham : "";
    if (ham != nullptr) cJSON_free(ham);
    cJSON_Delete(k);
    return s;
}

bool json_oku(const std::string& ham)
{
    cJSON* k = cJSON_Parse(ham.c_str());
    if (k == nullptr) {
        return false;
    }

    g_cocuk = Cocuk{};
    g_bilgiler.clear();
    g_ebeveyn_notu.clear();
    g_robot_adi.clear();
    g_oturum = 0;

    const cJSON* c = cJSON_GetObjectItemCaseSensitive(k, "cocuk");
    if (cJSON_IsObject(c)) {
        const cJSON* ad = cJSON_GetObjectItemCaseSensitive(c, "ad");
        if (cJSON_IsString(ad) && ad->valuestring != nullptr) {
            g_cocuk.ad = ad->valuestring;
        }
        const cJSON* yas = cJSON_GetObjectItemCaseSensitive(c, "yas");
        if (cJSON_IsNumber(yas)) g_cocuk.yas = yas->valueint;
    }

    const cJSON* d = cJSON_GetObjectItemCaseSensitive(k, "bilgiler");
    if (cJSON_IsArray(d)) {
        const cJSON* e = nullptr;
        cJSON_ArrayForEach(e, d) {
            Bilgi b;
            const cJSON* v = cJSON_GetObjectItemCaseSensitive(e, "id");
            if (cJSON_IsNumber(v)) b.id = v->valueint;
            v = cJSON_GetObjectItemCaseSensitive(e, "metin");
            if (cJSON_IsString(v) && v->valuestring) b.metin = v->valuestring;
            v = cJSON_GetObjectItemCaseSensitive(e, "kaynak");
            if (cJSON_IsString(v) && v->valuestring) b.kaynak = v->valuestring;
            v = cJSON_GetObjectItemCaseSensitive(e, "tarih");
            if (cJSON_IsString(v) && v->valuestring) b.tarih = v->valuestring;
            v = cJSON_GetObjectItemCaseSensitive(e, "son_gorulme");
            if (cJSON_IsString(v) && v->valuestring) b.son_gorulme = v->valuestring;
            v = cJSON_GetObjectItemCaseSensitive(e, "kez");
            if (cJSON_IsNumber(v)) b.kez = v->valueint;
            if (!b.metin.empty()) g_bilgiler.push_back(std::move(b));
        }
    }

    const cJSON* n = cJSON_GetObjectItemCaseSensitive(k, "ebeveyn_notu");
    if (cJSON_IsString(n) && n->valuestring) g_ebeveyn_notu = n->valuestring;

    const cJSON* r = cJSON_GetObjectItemCaseSensitive(k, "robot_adi");
    if (cJSON_IsString(r) && r->valuestring) g_robot_adi = r->valuestring;

    const cJSON* i = cJSON_GetObjectItemCaseSensitive(k, "istatistik");
    if (cJSON_IsObject(i)) {
        const cJSON* o = cJSON_GetObjectItemCaseSensitive(i, "oturum");
        if (cJSON_IsNumber(o)) g_oturum = o->valueint;
    }

    cJSON_Delete(k);
    return true;
}

// En az duyulmus ve en eski bilgiyi atar. Cok tekrarlanan bilgi
// (kopeginin adi gibi) kaliyor.
void buda()
{
    auto sirala = [] {
        std::stable_sort(g_bilgiler.begin(), g_bilgiler.end(),
                         [](const Bilgi& a, const Bilgi& b) {
                             if (a.kez != b.kez) return a.kez > b.kez;
                             return a.son_gorulme > b.son_gorulme;
                         });
    };

    if (static_cast<int>(g_bilgiler.size()) > HAFIZA_EN_FAZLA_KAYIT) {
        sirala();
        g_bilgiler.resize(HAFIZA_EN_FAZLA_KAYIT);
    }

    // BAYT siniri: kayit sayisi tek basina yetmiyor, tek bir kaydin
    // metni de uzun olabiliyor (PLAN.md).
    while (g_bilgiler.size() > 1 &&
           static_cast<int>(json_uret().size()) > HAFIZA_EN_FAZLA_BAYT) {
        sirala();
        g_bilgiler.pop_back();
    }
}

esp_err_t nvs_yaz()
{
    const std::string ham = json_uret();

    nvs_handle_t h;
    esp_err_t s = nvs_open(NVS_ALAN, NVS_READWRITE, &h);
    if (s != ESP_OK) {
        ESP_LOGE(ETIKET, "NVS acilamadi: %s", esp_err_to_name(s));
        return s;
    }
    s = nvs_set_blob(h, NVS_ANAHTAR, ham.data(), ham.size());
    if (s == ESP_OK) {
        // ATOMIKLIK BURADA. commit'ten once guc kesilirse ESKI deger
        // duruyor; sonra kesilirse YENI. Yarim yazma yok.
        s = nvs_commit(h);
    }
    nvs_close(h);
    if (s != ESP_OK) {
        ESP_LOGE(ETIKET, "hafiza yazilamadi: %s", esp_err_to_name(s));
    }
    return s;
}

std::string simdi_damgasi()
{
    // Cihazda gercek saat YOK (NTP kurulmadi). Sadece siralama icin
    // kullanildigi icin acilistan beri gecen saniye yeterli — ama bunu
    // "tarih" saniip kullanmamak lazim, o yuzden onune "t" koyuyoruz.
    //
    // NEDEN ONEMLI: budama `son_gorulme` alanina gore siraliyor. Bos
    // birakilsa siralama kararsiz olurdu ve hangi bilginin atilacagi
    // rastgele degisirdi.
    char b[24];
    const std::int64_t sn = esp_log_timestamp() / 1000;
    std::snprintf(b, sizeof(b), "t%012lld", static_cast<long long>(sn));
    return b;
}

}  // namespace

// ---------------------------------------------------------------------------

esp_err_t hafiza_baslat()
{
    if (g_hazir) return ESP_OK;

    nvs_handle_t h;
    esp_err_t s = nvs_open(NVS_ALAN, NVS_READWRITE, &h);
    if (s != ESP_OK) {
        ESP_LOGE(ETIKET, "NVS acilamadi: %s", esp_err_to_name(s));
        return s;
    }

    size_t boy = 0;
    s = nvs_get_blob(h, NVS_ANAHTAR, nullptr, &boy);
    if (s == ESP_ERR_NVS_NOT_FOUND || boy == 0) {
        nvs_close(h);
        g_hazir = true;
        ESP_LOGI(ETIKET, "hafiza bos — cocukla henuz tanisilmadi");
        return ESP_OK;
    }
    if (s != ESP_OK) {
        nvs_close(h);
        ESP_LOGE(ETIKET, "hafiza boyu okunamadi: %s", esp_err_to_name(s));
        return s;
    }

    std::string ham(boy, '\0');
    s = nvs_get_blob(h, NVS_ANAHTAR, ham.data(), &boy);
    nvs_close(h);
    if (s != ESP_OK) {
        ESP_LOGE(ETIKET, "hafiza okunamadi: %s", esp_err_to_name(s));
        return s;
    }

    if (!json_oku(ham)) {
        // BOZUK BLOB — silmiyoruz, YEDEKLIYORUZ.
        //
        // Cocugun hafizasi degerli. Bozuk olsa bile elle kurtarilabilir
        // olmali; Python tarafi da bozugu .bozuk-*.json diye sakliyor.
        ESP_LOGE(ETIKET, "hafiza BOZUK (%u bayt) — yedeklenip sifirlaniyor",
                 static_cast<unsigned>(boy));
        nvs_handle_t y;
        if (nvs_open(NVS_ALAN, NVS_READWRITE, &y) == ESP_OK) {
            nvs_set_blob(y, NVS_BOZUK, ham.data(), ham.size());
            nvs_commit(y);
            nvs_close(y);
        }
        g_cocuk = Cocuk{};
        g_bilgiler.clear();
        g_ebeveyn_notu.clear();
    }

    g_hazir = true;
    ESP_LOGI(ETIKET, "hafiza: %s%s · %u bilgi · %u oturum",
             g_cocuk.ad.empty() ? "isim yok" : g_cocuk.ad.c_str(),
             g_cocuk.yas > 0 ? " (yas var)" : "",
             static_cast<unsigned>(g_bilgiler.size()),
             static_cast<unsigned>(g_oturum));
    return ESP_OK;
}

const Cocuk& hafiza_cocuk() { return g_cocuk; }
const std::vector<Bilgi>& hafiza_bilgiler() { return g_bilgiler; }
const std::string& hafiza_ebeveyn_notu() { return g_ebeveyn_notu; }
int hafiza_oturum_sayisi() { return g_oturum; }

bool hafiza_bilgi_ekle(const std::string& yazi_ham, const std::string& kaynak)
{
    // Python: strip, sonra 3 <= len <= 200 KARAKTER
    const std::string yazi = hafiza_kirp(yazi_ham);

    const int uzunluk = kn_sayisi(yazi);
    if (uzunluk < HAFIZA_BILGI_EN_AZ || uzunluk > HAFIZA_BILGI_EN_FAZLA) {
        return false;
    }

    for (auto& b : g_bilgiler) {
        if (hafiza_ayni_bilgi_mi(b.metin, yazi)) {
            ++b.kez;
            b.son_gorulme = simdi_damgasi();
            // Yeni cumle eskisini KAPSIYORSA daha bilgilendirici olani
            // sakla (Python: `if _kokler(b) < _kokler(yazi)`) — gercek
            // alt kume, esitlik degil.
            const auto ke = hafiza_kokler(b.metin);
            const auto ky = hafiza_kokler(yazi);
            if (ke.size() < ky.size() && alt_kume_mi(ke, ky)) {
                b.metin = yazi;
            }
            nvs_yaz();
            return true;
        }
    }

    int en_buyuk_id = 0;
    for (const auto& b : g_bilgiler) en_buyuk_id = std::max(en_buyuk_id, b.id);

    Bilgi yeni;
    yeni.id = en_buyuk_id + 1;
    yeni.metin = yazi;
    yeni.kaynak = kaynak;
    yeni.tarih = simdi_damgasi();
    yeni.son_gorulme = yeni.tarih;
    yeni.kez = 1;
    g_bilgiler.push_back(std::move(yeni));

    buda();
    nvs_yaz();
    return true;
}

bool hafiza_bilgi_sil(int id)
{
    const auto once = g_bilgiler.size();
    g_bilgiler.erase(
        std::remove_if(g_bilgiler.begin(), g_bilgiler.end(),
                       [id](const Bilgi& b) { return b.id == id; }),
        g_bilgiler.end());
    if (g_bilgiler.size() == once) return false;
    nvs_yaz();
    return true;
}

void hafiza_cocugu_tanimla(const std::string& ad, int yas)
{
    if (!ad.empty()) g_cocuk.ad = kn_kes(ad, 40);
    if (yas > 0) g_cocuk.yas = yas;
    nvs_yaz();
}

const std::string& hafiza_robot_adi()
{
    // Cocuk degistirmediyse dogustan gelen ad. `static` sart: gecici
    // bir std::string'e referans dondurmek askida referans olurdu.
    static const std::string varsayilan = PATI_ROBOT_ADI;
    return g_robot_adi.empty() ? varsayilan : g_robot_adi;
}

bool hafiza_ad_bicimi_uygun_mu(const std::string& ad_ham)
{
    // prototype/hafiza.py §_ad_bicimi_uygun_mu ile AYNI kurallar.
    //
    // Reddedilenler: yer tutucular ("Bilinmiyor", "Yok", "?"), tek harf
    // ya da 40 karakterden uzun, rakam iceren ("12 yasindayim" gibi bir
    // cumle parcasi).
    //
    // Sinir KARAKTER (kod noktasi), bayt degil: "Şükrü" 5 karakter ama
    // 8 bayt. Bayt sayilsaydi Turkce adlar haksiz yere elenirdi.
    const std::string ad = hafiza_kirp(ad_ham);
    const int n = kn_sayisi(ad);
    if (n < 2 || n > 40) return false;
    for (const char c : ad) {
        if (c >= '0' && c <= '9') return false;
    }

    static const char* const YER_TUTUCU[] = {
        "bilinmiyor", "bilinmeyen", "bilmiyorum", "belirtilmemis",
        "yok", "bos", "none", "null", "isimsiz", "adsiz", "cocuk",
        "bilinmez", "anonim", "?", "-",
    };
    const std::string s = hafiza_sadele(ad);
    for (const char* y : YER_TUTUCU) {
        if (s == y) return false;
    }
    return true;
}

bool hafiza_cocuk_adi_gecerli_mi(const std::string& ad)
{
    // prototype/hafiza.py §_ad_gecerli_mi.
    //
    // Bicim kurallarina EK olarak: icinde ROBOTUN adi gecen bir sey
    // cocugun adi olamaz. Olculmus hata (30.07.2026): cocuk robota
    // "bundan sonra senin adin Pargali Patipasa" dedi, model bunu
    // COCUGUN adi sanip kaydetti; panelde cocugun adi "Pargali"
    // gorundu, gercek ad Deniz'ti.
    //
    // Karsilastirma HEM varsayilana HEM su anki ada bakiyor, cunku ad
    // degisebiliyor. Cocuk robota "Osman" deyip sonra "benim adim da
    // Osman" derse ikincisi elenir — bu kaybetmeye deger, tersi
    // panelde YANLIS isim demek.
    if (!hafiza_ad_bicimi_uygun_mu(ad)) return false;
    const std::string k = hafiza_kucult(ad);
    const std::string yasak[] = {hafiza_kucult(PATI_ROBOT_ADI),
                                 hafiza_kucult(hafiza_robot_adi())};
    for (const auto& y : yasak) {
        if (!y.empty() && k.find(y) != std::string::npos) return false;
    }
    return true;
}

int hafiza_yas_gecerli_mi(int ham)
{
    // prototype/hafiza.py §_yas_gecerli_mi. Doner: gecerliyse yas, degilse 0.
    //
    // 2-17 disi bir sayi cocugun yasi degildir; model cumleden rastgele
    // bir sayi kapmis demektir ("yedi kardesim var", "2026'da").
    // Panelde yanlis yas gormek, hic yas gormemekten kotu.
    return (ham >= 2 && ham <= 17) ? ham : 0;
}

bool hafiza_robot_adini_degistir(const std::string& ad)
{
    if (!hafiza_ad_bicimi_uygun_mu(ad)) return false;
    const std::string k = kn_kes(hafiza_kirp(ad), 40);
    g_robot_adi = k;
    nvs_yaz();
    ESP_LOGI(ETIKET, "artik adi: %s", g_robot_adi.c_str());
    return true;
}

void hafiza_robot_adini_sifirla()
{
    // g_robot_adi BOS BIRAKILIYOR, "Pati" YAZILMIYOR. Sebep:
    // hafiza_robot_adi() bos gorunce PATI_ROBOT_ADI'ni donduruyor, yani
    // varsayilan TEK YERDE duruyor (uretilen baslikta, kisilik.py'den).
    // Buraya "Pati" yazsaydik varsayilan iki yerde olurdu ve biri
    // degisince oteki eskirdi.
    g_robot_adi.clear();
    nvs_yaz();
    ESP_LOGI(ETIKET, "robot adi varsayilana dondu: %s",
             hafiza_robot_adi().c_str());
}

void hafiza_ebeveyn_notu_kaydet(const std::string& yazi)
{
    // Sinir var cunku prompt buyudukce model kurallara daha az uyuyor.
    g_ebeveyn_notu = kn_kes(yazi, HAFIZA_EBEVEYN_NOTU_EN_FAZLA);
    nvs_yaz();
}

void hafiza_her_seyi_unut()
{
    g_cocuk = Cocuk{};
    g_bilgiler.clear();
    g_ebeveyn_notu.clear();
    g_robot_adi.clear();          // varsayilan ada donuyor
    g_oturum = 0;
    nvs_yaz();
    ESP_LOGW(ETIKET, "hafiza sifirlandi — robot cocugu bastan taniyacak");
}

void hafiza_oturum_sayaci()
{
    ++g_oturum;
    nvs_yaz();
}

// ---------------------------------------------------------------------------
// Sistem promptuna giren blok — prototype/hafiza.py prompt_blogu() ile AYNI
// ---------------------------------------------------------------------------

std::string hafiza_prompt_blogu()
{
    std::vector<std::string> S;

    if (!g_cocuk.ad.empty()) {
        std::string t = "Konustugun cocugun adi " + g_cocuk.ad + ".";
        if (g_cocuk.yas > 0) {
            t += " " + std::to_string(g_cocuk.yas) + " yasinda.";
        }
        t += " Adini ara sira kullan ama her cumlede degil, "
             "bunaltici olur.";
        S.push_back(t);
    } else {
        S.push_back("Bu cocukla HENUZ TANISMADIN. Ilk isin sicak bir sekilde "
                    "kendini tanitip onun adini sormak olsun. Adini soyleyince "
                    "sevindigini belli et.");
    }

    // EBEVEYN NOTU — bilgilerden ONCE. Model uzun promptta bas kisma
    // daha cok uyuyor ve ebeveynin yazdigi sey daha onemli.
    if (!g_ebeveyn_notu.empty()) {
        S.push_back("");
        S.push_back("ANNE-BABASININ SANA BILDIRDIKLERI:");
        S.push_back(g_ebeveyn_notu);
    }

    // AZ SEY BILIYORSAN MERAK ET. (prototype/hafiza.py ile AYNI metin.)
    //
    // Kullanicinin gozlemi: "robot cocugun ismini hic bilmiyorsa
    // sormuyor, yasini da sormuyor, evcil hayvani var mi diye de".
    // Adi zaten yukarida isteniyor ama gerisi hic istenmiyordu — robot
    // bos bir hafizayla da sohbeti gotururdu ve hafiza bos kalirdi.
    //
    // Tek cumle, ve SADECE hafiza inceyken. Dolu hafizada bu satir yok:
    // promptu buyutmenin bedeli olculdu (kural uyumu %86'dan duser) ve
    // zaten tanidigi cocuga soru yagdirmasi da yanlis olur.
    if (g_bilgiler.size() < 3) {
        S.push_back("");
        S.push_back("Onu HENUZ AZ TANIYORSUN. Sohbetin akisinda merak et: "
                    "kac yasinda, evcil hayvani var mi, nelerden "
                    "hoslaniyor. Arada bir tane sor, sorgu gibi degil "
                    "arkadas gibi.");
    }

    if (!g_bilgiler.empty()) {
        std::vector<const Bilgi*> secili;
        secili.reserve(g_bilgiler.size());
        for (const auto& b : g_bilgiler) secili.push_back(&b);
        std::stable_sort(secili.begin(), secili.end(),
                         [](const Bilgi* a, const Bilgi* b) {
                             if (a->kez != b->kez) return a->kez > b->kez;
                             return a->son_gorulme > b->son_gorulme;
                         });

        std::vector<std::string> satirlar;
        int uzunluk = 0;
        const int en_fazla = std::min(static_cast<int>(secili.size()),
                                      HAFIZA_PROMPT_EN_FAZLA_KAYIT);
        for (int i = 0; i < en_fazla; ++i) {
            const std::string satir = "- " + secili[i]->metin;
            // Python `len(satir)` KARAKTER sayiyor.
            const int n = kn_sayisi(satir);
            if (uzunluk + n > HAFIZA_PROMPT_EN_FAZLA_KARAKTER) break;
            satirlar.push_back(satir);
            uzunluk += n;
        }
        if (!satirlar.empty()) {
            S.push_back("");
            S.push_back("ONUN HAKKINDA HATIRLADIKLARIN:");
            for (const auto& s : satirlar) S.push_back(s);
            S.push_back("");
            S.push_back("Bunlari bir arkadas nasil hatirlarsa oyle kullan: "
                        "dogrudan konusmanin icinde. Hatirladigin seyi "
                        "soylerken sadece soyle, nereden bildigini anlatma.");
        }
    }

    std::string cikti;
    for (size_t i = 0; i < S.size(); ++i) {
        if (i > 0) cikti += "\n";
        cikti += S[i];
    }
    return cikti;
}

std::string hafiza_ozet_json()
{
    cJSON* k = cJSON_CreateObject();

    cJSON* c = cJSON_CreateObject();
    cJSON_AddStringToObject(c, "ad", g_cocuk.ad.c_str());
    cJSON_AddNumberToObject(c, "yas", g_cocuk.yas);
    cJSON_AddItemToObject(k, "cocuk", c);

    // ROBOTUN ADI — panel bunu okuyor (panel/pati.js §hafizaAl).
    //
    // Alan gonderilmezse panel varsayilana dusuyor ve cocuk robotun
    // adini degistirmis olsa bile ebeveyn hep "Pati" goruyor. Yani
    // panel sessizce yalan soyluyor — bu projede iki kez yasanan
    // hatanin aynisi (bkz. PLAN.md "panel artik sozunu tutuyor").
    // Python tarafi da ikisini birden veriyor (hafiza.py §ozet).
    cJSON_AddStringToObject(k, "robot_adi", hafiza_robot_adi().c_str());
    cJSON_AddStringToObject(k, "robot_adi_varsayilan", PATI_ROBOT_ADI);

    cJSON_AddStringToObject(k, "ebeveyn_notu", g_ebeveyn_notu.c_str());
    cJSON_AddNumberToObject(k, "bilgi_sayisi",
                            static_cast<double>(g_bilgiler.size()));
    cJSON_AddNumberToObject(k, "oturum", g_oturum);
    cJSON_AddNumberToObject(k, "depolama_siniri", HAFIZA_EN_FAZLA_KAYIT);

    // Panel en yeniyi ustte gostersin (Python: id'ye gore tersten).
    std::vector<const Bilgi*> s;
    for (const auto& b : g_bilgiler) s.push_back(&b);
    std::sort(s.begin(), s.end(),
              [](const Bilgi* a, const Bilgi* b) { return a->id > b->id; });

    cJSON* d = cJSON_CreateArray();
    for (const auto* b : s) {
        cJSON* e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "id", b->id);
        cJSON_AddStringToObject(e, "metin", b->metin.c_str());
        cJSON_AddNumberToObject(e, "kez", b->kez);
        cJSON_AddItemToArray(d, e);
    }
    cJSON_AddItemToObject(k, "bilgiler", d);

    char* ham = cJSON_PrintUnformatted(k);
    std::string cikti = (ham != nullptr) ? ham : "{}";
    if (ham != nullptr) cJSON_free(ham);
    cJSON_Delete(k);
    return cikti;
}

}  // namespace pati
