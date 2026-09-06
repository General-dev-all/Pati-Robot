#include "pati_ayar.hpp"

#include "pati_beden_matematik.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

#include <esp_log.h>
#include <nvs.h>

#include "pati_ses.hpp"

namespace pati {
namespace {

constexpr const char* ETIKET = "ayar";
constexpr const char* NVS_ALAN = "pati";

// Sinirlar — panelin gonderdigine GUVENMIYORUZ. Tarayici tarafinda
// kaydiricinin min/max'i var ama istek elle de atilabilir; sinir
// firmware'de olmali.
constexpr float HIZ_EN_AZ = 0.85f;
constexpr float HIZ_EN_FAZLA = 1.45f;
constexpr int UYKU_EN_AZ = 1;
constexpr int UYKU_EN_FAZLA = 15;
constexpr int VAD_EN_AZ = 200;
constexpr int VAD_EN_FAZLA = 2000;

// Bedenin surus hizi tavani, yuzde. Sinirlar
// pati_beden_matematik.hpp'de — tek kaynak, cunku karistirma da ayni
// araligi kirpiyor.
//
// EN AZ 40: altinda tekerlek donmuyor, yalnizca otuyor (06.09.2026'da
// gercek kartta olculdu). Kaydiriciya o araligi koymak, hicbir sey
// yapmayan bir yer birakmak olurdu.
//
// ⚠️ PANEL BU SAYIYI GOSTERMIYOR. Panel 0-100 gosteriyor ve donusumu
// kendisi yapiyor (pati.js · gosterilenden_gercege): gosterilen 0 ->
// 40, gosterilen 100 -> 100. Yani varsayilan 70, panelde %50 diye
// gorunuyor.
//
// Varsayilan bilincli olarak tam guc DEGIL: masada oynanacak bir robot
// icin tam guc fazla. Ebeveyn isterse yukseltiyor.
constexpr int BEDEN_HIZ_EN_AZ = HIZ_TAVAN_EN_AZ;
constexpr int BEDEN_HIZ_EN_FAZLA = HIZ_TAVAN_EN_COK;

std::string g_ses_adi;
float g_hiz = 1.30f;
int g_uyku_dk = 4;
bool g_soz_kesme = false;
int g_vad_ms = 0;
bool g_yuz = true;
int g_beden_hiz = 70;   // panelde %50

// 🔴 IKISI DE VARSAYILAN ACIK — kullanicinin acik istegi (06.09.2026):
// "default olarak acik gelsin".
//
// Acik olmasi guvenli, cunku ozerk hareket YAPISAL olarak yer
// degistiremiyor (pati_beden_matematik.hpp · jest_donus). Eski
// `sevinc` anahtari varsayilan KAPALIYDI ve o dogruydu: o zaman
// tekerlegin Pati'ye acilmasi denenmemis bir seydi.
int g_tekerlek_kip = KIP_ACIK;
int g_kol_kip = KIP_ACIK;

std::atomic<bool> g_yenileme{false};

nvs_handle_t ac(nvs_open_mode_t mod)
{
    nvs_handle_t h = 0;
    if (nvs_open(NVS_ALAN, mod, &h) != ESP_OK) return 0;
    return h;
}

void i32_yaz(const char* anahtar, std::int32_t v)
{
    const nvs_handle_t h = ac(NVS_READWRITE);
    if (h == 0) return;
    nvs_set_i32(h, anahtar, v);
    nvs_commit(h);
    nvs_close(h);
}

void str_yaz(const char* anahtar, const std::string& v)
{
    const nvs_handle_t h = ac(NVS_READWRITE);
    if (h == 0) return;
    nvs_set_str(h, anahtar, v.c_str());
    nvs_commit(h);
    nvs_close(h);
}

}  // namespace

// ---------------------------------------------------------------------------

esp_err_t ayar_baslat()
{
    // Varsayilanlar Kconfig'den: kayit yoksa Asama 2'deki davranisin
    // aynisi calisiyor, yani eski olcumlerle karsilastirilabilirlik
    // bozulmuyor.
    g_ses_adi = CONFIG_PATI_SES;
    g_vad_ms = CONFIG_PATI_VAD_SESSIZLIK_MS;
#ifdef CONFIG_PATI_YARIM_DUPLEKS
    // Kconfig'deki kol "yarim dupleks ACIK" demek; soz kesme onun TERSI.
    //
    // Eskiden iki dal da `false` veriyordu, yani kol hicbir sey
    // yapmiyordu. Daha kotusu: yanki korumasi ayri bir DERLEME ZAMANI
    // `#if`ine bagliydi ve `ayar_soz_kesme()` hicbir yerden
    // okunmuyordu — panelin "Pati'nin sozu kesilebilsin" dugmesi
    // NVS'e yaziyor ama davranisa dokunmuyordu. Panelin yalan
    // soylemesi bu projede ucuncu kez cikti (PLAN.md).
    g_soz_kesme = false;
#else
    g_soz_kesme = true;
#endif

    const nvs_handle_t h = ac(NVS_READONLY);
    if (h == 0) {
        ESP_LOGI(ETIKET, "kayit yok, varsayilanlar kullaniliyor");
        return ESP_OK;
    }

    char t[33];
    size_t n = sizeof(t);
    if (nvs_get_str(h, "ses_adi", t, &n) == ESP_OK && t[0] != '\0') {
        g_ses_adi = t;
    }
    std::int32_t v = 0;
    if (nvs_get_i32(h, "hiz_yuz", &v) == ESP_OK) {
        // Yuzde olarak saklaniyor: NVS'te float yok ve i32 tasinabilir.
        g_hiz = std::clamp(static_cast<float>(v) / 100.0f,
                           HIZ_EN_AZ, HIZ_EN_FAZLA);
    }
    if (nvs_get_i32(h, "uyku_dk", &v) == ESP_OK) {
        g_uyku_dk = std::clamp(static_cast<int>(v), UYKU_EN_AZ, UYKU_EN_FAZLA);
    }
    if (nvs_get_i32(h, "soz_kesme", &v) == ESP_OK) g_soz_kesme = (v != 0);
    if (nvs_get_i32(h, "vad_ms", &v) == ESP_OK) {
        g_vad_ms = (v == 0) ? 0 : std::clamp(static_cast<int>(v),
                                             VAD_EN_AZ, VAD_EN_FAZLA);
    }
    if (nvs_get_i32(h, "yuz", &v) == ESP_OK) g_yuz = (v != 0);
    if (nvs_get_i32(h, "beden_hiz", &v) == ESP_OK) {
        g_beden_hiz = std::clamp(static_cast<int>(v), BEDEN_HIZ_EN_AZ,
                                 BEDEN_HIZ_EN_FAZLA);
    }
    if (nvs_get_i32(h, "tekerlek", &v) == ESP_OK) {
        g_tekerlek_kip = std::clamp(static_cast<int>(v), KIP_KAPALI, KIP_ACIK);
    } else if (nvs_get_i32(h, "hareket", &v) == ESP_OK) {
        // ESKI ANAHTARDAN GECIS. 3.2.x'te tek bir acma-kapama vardi:
        // "konusurken kipirdasin". Kapali demek "tekerlek yalnizca
        // joystick'ten donsun" demekti — yani tam olarak KIP_KUMANDA.
        //
        // Gecis olmasaydi anahtari kapatmis bir ebeveyn guncellemeden
        // sonra Pati'yi yine kipirdar bulurdu ve ayarinin sessizce
        // kayboldugunu fark etmezdi.
        g_tekerlek_kip = (v != 0) ? KIP_ACIK : KIP_KUMANDA;
        ESP_LOGI(ETIKET, "eski 'hareket' anahtari tasindi: tekerlek=%d",
                 g_tekerlek_kip);
    }
    if (nvs_get_i32(h, "kol", &v) == ESP_OK) {
        g_kol_kip = std::clamp(static_cast<int>(v), KIP_KAPALI, KIP_ACIK);
    }
    nvs_close(h);

    ESP_LOGI(ETIKET, "ses=%s hiz=%.2f uyku=%d dk soz_kesme=%d vad=%d yuz=%d "
                     "beden_hiz=%d tekerlek=%d kol=%d",
             g_ses_adi.c_str(), g_hiz, g_uyku_dk, g_soz_kesme ? 1 : 0,
             g_vad_ms, g_yuz ? 1 : 0, g_beden_hiz, g_tekerlek_kip,
             g_kol_kip);
    return ESP_OK;
}

const std::string& ayar_ses_adi() { return g_ses_adi; }
float ayar_hiz() { return g_hiz; }
int ayar_uyku_dk() { return g_uyku_dk; }
bool ayar_soz_kesme() { return g_soz_kesme; }
int ayar_vad_ms() { return g_vad_ms; }
bool ayar_yuz_araci() { return g_yuz; }

// 🔴 RAM'DEN OKUNUYOR, NVS'TEN DEGIL. beden_surus() bunu her komutta
// (saniyede ~7 kez) cagiriyor; NVS'e gitseydi surus yolunda flash
// erisimi olurdu — CLAUDE.md'deki sicak dongu tuzaginin ta kendisi.
int ayar_beden_hiz() { return g_beden_hiz; }
int ayar_tekerlek_kip() { return g_tekerlek_kip; }
int ayar_kol_kip() { return g_kol_kip; }

void ayar_ses_adi_yaz(const std::string& ad)
{
    // Bos ad gelirse DOKUNMUYORUZ: Gemini bos voiceName ile setup'i
    // reddediyor ve robot hic konusmuyor.
    if (ad.empty() || ad == g_ses_adi) return;
    g_ses_adi = ad;
    str_yaz("ses_adi", ad);
    g_yenileme.store(true);
    ESP_LOGI(ETIKET, "ses: %s (tur sonunda gecerli)", ad.c_str());
}

void ayar_hiz_yaz(float hiz)
{
    const float y = std::clamp(hiz, HIZ_EN_AZ, HIZ_EN_FAZLA);
    if (std::abs(y - g_hiz) < 0.005f) return;
    g_hiz = y;
    i32_yaz("hiz_yuz", static_cast<std::int32_t>(y * 100.0f + 0.5f));
    // I2S saati sabit. Yazılım çarpanı tur sonunda değişir ki
    // aynı cümlenin ortasında Pati'nin tınısı değişmesin.
    g_yenileme.store(true);
    ESP_LOGI(ETIKET, "tizlik: %.2fx (tur sonunda gecerli)", y);
}

void ayar_uyku_yaz(int dakika)
{
    g_uyku_dk = std::clamp(dakika, UYKU_EN_AZ, UYKU_EN_FAZLA);
    i32_yaz("uyku_dk", g_uyku_dk);
    ESP_LOGI(ETIKET, "uyku: %d dk", g_uyku_dk);
}

void ayar_soz_kesme_yaz(bool acik)
{
    g_soz_kesme = acik;
    i32_yaz("soz_kesme", acik ? 1 : 0);
    ESP_LOGI(ETIKET, "soz kesme: %s", acik ? "ACIK (kulaklik gerekiyor)"
                                           : "kapali");
}

void ayar_vad_yaz(int ms)
{
    const int y = (ms == 0) ? 0 : std::clamp(ms, VAD_EN_AZ, VAD_EN_FAZLA);
    if (y == g_vad_ms) return;
    g_vad_ms = y;
    i32_yaz("vad_ms", y);
    g_yenileme.store(true);
    ESP_LOGI(ETIKET, "sustu karari: %d ms (tur sonunda gecerli)", y);
}

void ayar_yuz_yaz(bool acik)
{
    if (acik == g_yuz) return;
    g_yuz = acik;
    i32_yaz("yuz", acik ? 1 : 0);
    g_yenileme.store(true);
    ESP_LOGI(ETIKET, "gozlerle duygu: %s (tur sonunda gecerli)",
             acik ? "acik" : "kapali");
}

void ayar_beden_hiz_yaz(int yuzde)
{
    const int y = std::clamp(yuzde, BEDEN_HIZ_EN_AZ, BEDEN_HIZ_EN_FAZLA);
    if (y == g_beden_hiz) return;
    g_beden_hiz = y;
    i32_yaz("beden_hiz", y);
    // ANINDA gecerli: bir sonraki surus komutu yeni tavani kullaniyor.
    // Oturum yenilemesi gerekmiyor, bu ayar Gemini'ye gitmiyor.
}

namespace {

const char* kip_adi(int k)
{
    return (k == KIP_KAPALI) ? "kapali"
           : (k == KIP_KUMANDA) ? "sadece kumandadan" : "acik";
}

}  // namespace

void ayar_tekerlek_kip_yaz(int kip)
{
    const int k = std::clamp(kip, KIP_KAPALI, KIP_ACIK);
    if (k == g_tekerlek_kip) return;
    g_tekerlek_kip = k;
    i32_yaz("tekerlek", k);
    // Donanim tarafi ANINDA gecerli (beden gorevindeki tek bogaz).
    // Ama MODELE de soylenmesi gerekiyor: kip acik degilse donen
    // hareketleri hic secmemeli, yoksa "dans ediyorum!" der ve hicbir
    // sey donmez. O bilgi setup mesajinda gidiyor, yani tur sonu.
    g_yenileme.store(true);
    ESP_LOGI(ETIKET, "tekerlekler: %s (donanim aninda, model tur sonunda)",
             kip_adi(k));
}

void ayar_kol_kip_yaz(int kip)
{
    const int k = std::clamp(kip, KIP_KAPALI, KIP_ACIK);
    if (k == g_kol_kip) return;
    g_kol_kip = k;
    i32_yaz("kol", k);
    g_yenileme.store(true);
    ESP_LOGI(ETIKET, "kollar: %s (donanim aninda, model tur sonunda)",
             kip_adi(k));
}

void ayar_sifirla()
{
    const nvs_handle_t h = ac(NVS_READWRITE);
    if (h != 0) {
        for (const char* a : {"ses_adi", "hiz_yuz", "uyku_dk", "soz_kesme",
                              "vad_ms", "yuz", "beden_hiz", "tekerlek",
                              "kol", "hareket", "sevinc"}) {
            nvs_erase_key(h, a);
        }
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGW(ETIKET, "ayarlar sifirlandi");
}

bool ayar_yenileme_gerekli() { return g_yenileme.load(); }
void ayar_yenileme_iste()
{
    // Govde TEK BIR ATOMIK STORE. Gerekcesi baslikta: beden algilama
    // sicak dongusunden cagriliyor.
    g_yenileme.store(true);
}

void ayar_yenileme_temizle() { g_yenileme.store(false); }

std::string ayar_json()
{
    char b[420];
    std::snprintf(b, sizeof(b),
                  "\"ses\":{\"seviye\":%.3f,\"en_az\":%.2f,\"en_fazla\":%.2f,"
                  "\"hiz\":%.2f,\"ses_adi\":\"%s\"},"
                  "\"uyku\":%d,"
                  "\"konusma\":{\"soz_kesme\":%s,\"vad\":%d,\"yuz\":%s},"
                  "\"kumanda\":{\"hiz\":%d,\"tekerlek\":%d,\"kol\":%d}",
                  ses_seviyesi(), SES_SEVIYESI_EN_AZ, SES_SEVIYESI_EN_FAZLA,
                  g_hiz, g_ses_adi.c_str(), g_uyku_dk,
                  g_soz_kesme ? "true" : "false", g_vad_ms,
                  g_yuz ? "true" : "false",
                  g_beden_hiz, g_tekerlek_kip, g_kol_kip);
    return b;
}

}  // namespace pati
