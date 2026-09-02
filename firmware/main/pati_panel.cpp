#include "pati_panel.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>

#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

#include "pati_ag.hpp"
#include "pati_anahtar.hpp"
#include "pati_ayar.hpp"
#include "pati_gozler.hpp"
#include "pati_guc.hpp"
#include "pati_guncelleme.hpp"
#include "pati_hafiza.hpp"
#include "pati_kullanim.hpp"
#include "pati_ses.hpp"
#include "pati_sohbet.hpp"

namespace pati {
namespace {

constexpr const char* ETIKET = "panel";

httpd_handle_t g_sunucu = nullptr;
std::atomic<bool> g_dns_calisiyor{false};

// ---------------------------------------------------------------------------
// Gomulu sayfa dosyalari
// ---------------------------------------------------------------------------
//
// CMake EMBED_FILES ile firmware'e giriyorlar. Sembol adlari dosya
// adindan uretiliyor.

#define GOMULU(ad)                                        \
    extern const std::uint8_t ad##_bas[] asm("_binary_" #ad "_start"); \
    extern const std::uint8_t ad##_son[] asm("_binary_" #ad "_end");

GOMULU(index_html)
GOMULU(stil_css)
GOMULU(pati_js)
GOMULU(gozler240_js)
GOMULU(mikrofon_js)
GOMULU(ornek_js)

#undef GOMULU

struct Dosya {
    const char* yol;
    const char* tur;
    const std::uint8_t* bas;
    const std::uint8_t* son;
};

const Dosya DOSYALAR[] = {
    {"/", "text/html; charset=utf-8", index_html_bas, index_html_son},
    {"/index.html", "text/html; charset=utf-8", index_html_bas, index_html_son},
    {"/stil.css", "text/css; charset=utf-8", stil_css_bas, stil_css_son},
    {"/pati.js", "text/javascript; charset=utf-8", pati_js_bas, pati_js_son},
    {"/gozler240.js", "text/javascript; charset=utf-8",
     gozler240_js_bas, gozler240_js_son},
    {"/mikrofon.js", "text/javascript; charset=utf-8",
     mikrofon_js_bas, mikrofon_js_son},
    {"/ornek.js", "text/javascript; charset=utf-8", ornek_js_bas, ornek_js_son},
};

// ---------------------------------------------------------------------------
// Yardimcilar
// ---------------------------------------------------------------------------

esp_err_t json_yolla(httpd_req_t* r, const std::string& govde)
{
    httpd_resp_set_type(r, "application/json; charset=utf-8");
    // Panel her acilista taze veri gormeli.
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_send(r, govde.c_str(), govde.size());
}

esp_err_t hata_yolla(httpd_req_t* r, const char* kod, const char* mesaj)
{
    httpd_resp_set_status(r, kod);
    return json_yolla(r, std::string("{\"hata\":\"") + mesaj + "\"}");
}

// Istek govdesini okur. Sinir: 2 KB — ebeveyn notu 600 karakter, wifi
// sifresi 63; 2 KB fazlasiyla yeterli ve sinirsiz okumak yigini
// tuketmeye acik kapi olurdu.
bool govde_oku(httpd_req_t* r, std::string& cikti)
{
    constexpr int SINIR = 2048;
    if (r->content_len <= 0 || r->content_len > SINIR) return false;
    cikti.resize(r->content_len);
    int alinan = 0;
    while (alinan < r->content_len) {
        const int n = httpd_req_recv(r, cikti.data() + alinan,
                                     r->content_len - alinan);
        if (n <= 0) return false;
        alinan += n;
    }
    return true;
}

std::string json_dize(const cJSON* k, const char* alan, const char* varsayilan = "")
{
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(k, alan);
    if (cJSON_IsString(v) && v->valuestring != nullptr) return v->valuestring;
    return varsayilan;
}

double json_sayi(const cJSON* k, const char* alan, double varsayilan)
{
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(k, alan);
    if (cJSON_IsNumber(v)) return v->valuedouble;
    return varsayilan;
}

// ---------------------------------------------------------------------------
// Sayfa dosyalari
// ---------------------------------------------------------------------------

esp_err_t dosya_isle(httpd_req_t* r)
{
    for (const auto& d : DOSYALAR) {
        if (std::strcmp(r->uri, d.yol) == 0) {
            httpd_resp_set_type(r, d.tur);
            return httpd_resp_send(
                r, reinterpret_cast<const char*>(d.bas),
                static_cast<size_t>(d.son - d.bas));
        }
    }

    // ---- CAPTIVE PORTAL ----
    //
    // Bilinmeyen adres. Kurulum modundaysak yonlendiriyoruz: telefonun
    // isletim sistemi bunu gorunce "oturum acmaniz gerekiyor" sayfasini
    // kendiliginden aciyor.
    //
    // 302 kullaniliyor, 200 + HTML DEGIL: Android'in yoklamasi govdeye
    // bakmiyor, DURUM KODUNA bakiyor. 200 donsek "internet var" sanip
    // sayfayi hic acmaz.
    if (ag_durumu() == AgDurumu::Kurulum) {
        httpd_resp_set_status(r, "302 Found");
        const std::string hedef = std::string("http://") + ag_ip() + "/";
        httpd_resp_set_hdr(r, "Location", hedef.c_str());
        httpd_resp_send(r, nullptr, 0);
        return ESP_OK;
    }

    httpd_resp_send_404(r);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// GET /api/durum
// ---------------------------------------------------------------------------

esp_err_t durum_isle(httpd_req_t* r)
{
    cJSON* k = cJSON_CreateObject();

    // Sayfa bu bayraga bakip robot moduna geciyor: guc dugmesini
    // gizliyor (robot fis takiliyken zaten acik) ve wifi karti gercek
    // oluyor.
    cJSON_AddBoolToObject(k, "robot", true);

    const auto d = ag_durumu();
    cJSON* a = cJSON_CreateObject();
    cJSON_AddStringToObject(a, "ad", ag_adi());
    cJSON_AddNumberToObject(a, "guc", ag_gucu());
    cJSON_AddBoolToObject(a, "bagli", d == AgDurumu::Bagli);
    cJSON_AddBoolToObject(a, "kurulum", d == AgDurumu::Kurulum);
    cJSON_AddStringToObject(a, "ip", ag_ip());
    cJSON_AddItemToObject(k, "ag", a);

    const auto o = kullanim_ozet();
    cJSON* u = cJSON_CreateObject();
    cJSON_AddNumberToObject(u, "bugun_dk", o.bugun_dk);
    cJSON_AddNumberToObject(u, "ay_dk", o.ay_dk);
    cJSON_AddNumberToObject(u, "tahmin_usd", o.tahmin_usd);
    cJSON_AddBoolToObject(u, "saat_hazir", kullanim_saat_hazir());
    cJSON_AddItemToObject(k, "kullanim", u);

    cJSON_AddStringToObject(k, "ifade", gozler_su_anki());

    // 🔴 UYKU DURUMU AYRI BIR ALAN. Panel bunu gozlerin ifadesinden
    // cikarmiyor ve cikarmamali: "uykulu" ifadesi model tarafindan da
    // secilebiliyor (yuz araci), yani uyanikken de gorulebilir.
    // Bayrak gonderilmeyince robot uyurken panel "dinliyor" yaziyordu.
    cJSON_AddBoolToObject(k, "uyuyor", sohbet_uyuyor());

    // ---- GUC ------------------------------------------------------------
    //
    // 🔴 BU ALAN OLMADAN PILDEKI PATI HIC GOZLENEMIYOR.
    //
    // Seri konsol USB uzerinden geliyor; USB'yi cikarinca kablo da
    // gidiyor. Yani "pilde ne oluyor" sorusunun seri porttan CEVABI YOK
    // — 01.09.2026'da tam bu duvara carpildi: pilde brownout yasandi,
    // ses tavani devreye girdi mi girmedi mi olculemedi.
    //
    // Ag ise pilde de ayakta. Panel bu yuzden tek gozlem penceresi.
    //
    // Ebeveyn icin de gerekli zaten: elde tasinan bir robotun sarji
    // gorunmeli.
    cJSON* g = cJSON_CreateObject();
    const auto kaynak = guc_kaynak();
    cJSON_AddStringToObject(g, "kaynak",
                            kaynak == GucKaynagi::Usb   ? "usb"
                            : kaynak == GucKaynagi::Pil ? "pil"
                                                        : "bilinmiyor");
    cJSON_AddNumberToObject(g, "pil_mv", pil_mv());
    // Yuzde ham gerilimden DEGIL, 30 saniyelik pencerenin tepesinden
    // hesaplaniyor — gerekce pati_guc.hpp'de (yuk altinda 120 mV
    // sarkiyor). Henuz ornek birikmediyse -1.
    cJSON_AddNumberToObject(g, "pil_yuzde", pil_yuzde());
    cJSON_AddNumberToObject(g, "pil_dusuk", pil_dusuk() ? 1 : 0);
    cJSON_AddNumberToObject(g, "vin_mv", vin_mv());
    cJSON_AddNumberToObject(g, "sicaklik_c", yonga_sicakligi());
    // Arizali acilis sayisi — pilde tek olcum araci. Gerekcesi
    // pati_guc.hpp'de: pilde USB yok, seri port yok.
    cJSON_AddNumberToObject(g, "cokme", cokme_sayisi());
    // Son cokme hangi gerilimde oldu — "pil azalinca daha sik mi
    // cokuyor" sorusunu sinayan tek veri.
    cJSON_AddNumberToObject(g, "cokme_mv", son_cokme_mv());
    cJSON_AddNumberToObject(g, "goz_fps", gozler_hedef_fps());
    // Fiilen uygulanan ses tavani — kullanicinin sectigi degil.
    cJSON_AddNumberToObject(g, "ses_tavani",
                            kaynak == GucKaynagi::Usb
                                ? ses_seviyesi()
                                : std::min(ses_seviyesi(), SES_PIL_TAVANI));
    // Acilis sebebi. "brownout" gorunuyorsa ses o donanim icin yuksek.
    const esp_reset_reason_t sebep = esp_reset_reason();
    cJSON_AddStringToObject(g, "acilis",
                            sebep == ESP_RST_BROWNOUT   ? "brownout"
                            : sebep == ESP_RST_PANIC    ? "cokme"
                            : sebep == ESP_RST_TASK_WDT ? "gorev_bekcisi"
                            : sebep == ESP_RST_INT_WDT  ? "kesme_bekcisi"
                            : sebep == ESP_RST_POWERON  ? "guc"
                            : sebep == ESP_RST_SW       ? "yazilim"
                            : sebep == ESP_RST_EXT      ? "dis"
                                                        : "diger");
    cJSON_AddItemToObject(k, "guc", g);

    char* ham = cJSON_PrintUnformatted(k);
    std::string govde = (ham != nullptr) ? ham : "{}";
    if (ham != nullptr) cJSON_free(ham);
    cJSON_Delete(k);

    // Hafiza, ayarlar, anahtar ve guncelleme kendi JSON'larini uretiyor
    // (her modul kendi alanlarini biliyor); parcalari burada
    // birlestiriyoruz.
    if (govde.size() > 2) {
        govde.pop_back();                       // kapanis }
        govde += "," + ayar_json();
        govde += ",\"hafiza\":" + hafiza_ozet_json();
        govde += "," + anahtar_json();
        govde += "," + guncelleme_json();
        govde += "}";
    }
    return json_yolla(r, govde);
}

// ---------------------------------------------------------------------------
// GET /api/aglar
// ---------------------------------------------------------------------------

esp_err_t aglar_isle(httpd_req_t* r)
{
    const auto aglar = ag_tara();
    cJSON* d = cJSON_CreateArray();
    for (const auto& a : aglar) {
        cJSON* e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "ad", a.ad.c_str());
        cJSON_AddNumberToObject(e, "guc", a.guc);
        cJSON_AddBoolToObject(e, "kilit", a.kilit);
        cJSON_AddItemToArray(d, e);
    }
    char* ham = cJSON_PrintUnformatted(d);
    std::string govde = (ham != nullptr) ? ham : "[]";
    if (ham != nullptr) cJSON_free(ham);
    cJSON_Delete(d);
    return json_yolla(r, govde);
}

// ---------------------------------------------------------------------------
// POST /api/wifi
// ---------------------------------------------------------------------------

esp_err_t wifi_isle(httpd_req_t* r)
{
    std::string govde;
    if (!govde_oku(r, govde)) return hata_yolla(r, "400", "govde okunamadi");

    cJSON* k = cJSON_Parse(govde.c_str());
    if (k == nullptr) return hata_yolla(r, "400", "bozuk JSON");
    const std::string ad = json_dize(k, "ad");
    const std::string sifre = json_dize(k, "sifre");
    cJSON_Delete(k);

    if (ad.empty()) return hata_yolla(r, "400", "ag adi bos");

    const esp_err_t s = ag_kaydet_ve_bagla(ad, sifre);
    if (s != ESP_OK) {
        // Sebebi ebeveyne SOYLE. "Baglanilamadi" tek basina ise
        // yaramiyor; sifre mi yanlis, ag mi menzil disi?
        return hata_yolla(r, "502",
                          "baglanilamadi — sifre yanlis olabilir ya da ag "
                          "menzil disinda");
    }
    return json_yolla(r, std::string("{\"tamam\":true,\"ip\":\"") + ag_ip()
                             + "\"}");
}

// ---------------------------------------------------------------------------
// POST /api/ayar
// ---------------------------------------------------------------------------

esp_err_t ayar_isle(httpd_req_t* r)
{
    std::string govde;
    if (!govde_oku(r, govde)) return hata_yolla(r, "400", "govde okunamadi");

    cJSON* k = cJSON_Parse(govde.c_str());
    if (k == nullptr) return hata_yolla(r, "400", "bozuk JSON");

    const std::string alan = json_dize(k, "alan");
    bool tamam = true;

    if (alan == "seviye") {
        // Sinirlar ses katmaninda; panel gonderse bile disina cikmiyor.
        ses_seviyesi_ayarla(static_cast<float>(json_sayi(k, "deger", 0.85)));
    } else if (alan == "hiz") {
        ayar_hiz_yaz(static_cast<float>(json_sayi(k, "deger", 1.30)));
        // HEMEN uygula. Ayari yalnizca kaydetmek, ebeveynin kaydirdigi
        // cubugun hicbir sey yapmamasi demekti — 23.08.2026'ya kadar
        // tam olarak boyleydi.
        hoparlor_hiz_ayarla(ayar_hiz());
    } else if (alan == "ses") {
        ayar_ses_adi_yaz(json_dize(k, "deger"));
    } else if (alan == "uyku") {
        ayar_uyku_yaz(static_cast<int>(json_sayi(k, "deger", 4)));
    } else if (alan == "soz_kesme") {
        ayar_soz_kesme_yaz(json_sayi(k, "deger", 0) != 0);
    } else if (alan == "vad") {
        ayar_vad_yaz(static_cast<int>(json_sayi(k, "deger", 0)));
    } else if (alan == "yuz") {
        ayar_yuz_yaz(json_sayi(k, "deger", 1) != 0);
    } else if (alan == "ad" || alan == "yas") {
        hafiza_cocugu_tanimla(json_dize(k, "ad"),
                              static_cast<int>(json_sayi(k, "yas", 0)));
    } else if (alan == "robot_adi") {
        // Robotun adini IKI TARAF DA yazabiliyor: cocuk sohbette
        // ("bundan sonra senin adin Omer"), ebeveyn de buradan.
        // Son yazan kazaniyor — ayri bir oncelik kurali YOK, cunku
        // ikisi de mesru: cocuk oyunun icinde, ebeveyn dusunerek.
        //
        // Ikisi de AYNI fonksiyona gidiyor (hafiza_robot_adini_degistir):
        // dogrulama, kirpma ve 40 karakter siniri tek yerde kalsin.
        // Iki ayri yol olsaydi biri Turkce harfi elerken oteki elemezdi.
        const std::string yeni = json_dize(k, "deger");
        if (hafiza_kirp(yeni).empty()) {
            // BOSALTMAK HATA DEGIL, "varsayilana don" demek. Ebeveyn
            // alani silip cikinca robot yine "Pati" oluyor; ad hicbir
            // zaman bos kalmiyor (hafiza_robot_adi() bos ise varsayilani
            // donduruyor).
            hafiza_robot_adini_sifirla();
        } else if (!hafiza_robot_adini_degistir(yeni)) {
            // Rakam iceriyor, tek harf, ya da 40 karakterden uzun.
            // Sessizce yutmuyoruz: panel kullaniciya soyleyecek.
            tamam = false;
        }
    } else if (alan == "ebeveyn_notu") {
        hafiza_ebeveyn_notu_kaydet(json_dize(k, "yazi"));
    } else {
        // BILINMEYEN ALANI SESSIZCE YUTMUYORUZ. Panel yeni bir ayar
        // gonderiyor ve firmware onu tanimiyorsa, kullanici kaydiriciyi
        // oynatip hicbir sey olmadigini gorur ve sebebini bulamaz.
        tamam = false;
        ESP_LOGW(ETIKET, "bilinmeyen ayar alani: %s", alan.c_str());
    }
    cJSON_Delete(k);

    if (!tamam) return hata_yolla(r, "400", "bilinmeyen ayar");
    return json_yolla(r, "{\"tamam\":true}");
}

// ---------------------------------------------------------------------------
// POST /api/hafiza
// ---------------------------------------------------------------------------

esp_err_t hafiza_isle(httpd_req_t* r)
{
    std::string govde;
    if (!govde_oku(r, govde)) return hata_yolla(r, "400", "govde okunamadi");

    cJSON* k = cJSON_Parse(govde.c_str());
    if (k == nullptr) return hata_yolla(r, "400", "bozuk JSON");

    const cJSON* hepsi = cJSON_GetObjectItemCaseSensitive(k, "hepsi");
    if (cJSON_IsTrue(hepsi)) {
        hafiza_her_seyi_unut();
    } else {
        hafiza_bilgi_sil(static_cast<int>(json_sayi(k, "id", -1)));
    }
    cJSON_Delete(k);
    return json_yolla(r, "{\"tamam\":true}");
}

// ---------------------------------------------------------------------------
// POST /api/anahtar — Gemini anahtarini kaydet
// ---------------------------------------------------------------------------

// Kaydedilen anahtari Google'a sorar ve gerekiyorsa cihazi yeniden
// baslatir. AYRI GOREVDE calisiyor.
//
// 🔴 HTTP ISLEYICISINDE YAPILAMAZ. esp_http_server butun istekleri TEK
// gorevde, sirayla isliyor. Dogrulama istegi en kotu halde 15 saniye
// (zaman asimi) suruyor ve o sure boyunca panel butunuyle donardi:
// yoklamalar cevapsiz kalir, panel "Pati'ye ulasilamiyor" seridini
// acar ve anne anahtari yazar yazmaz robotu bozdugunu sanirdi.
//
// Bunun yerine hemen "kaydedildi" diyoruz; sonucu panel iki saniyelik
// yoklamayla zaten goruyor (durum: bilinmiyor -> gecerli/gecersiz/kota).
void anahtar_gorevi(void*)
{
    // Sohbet ZATEN calisiyor muydu? Cevap dogrulamadan ONCE alinmali:
    // dogrulama saniyeler suruyor ve o arada app_main sohbeti baslatmis
    // olabilir. O durumda yeniden baslatmaya gerek yok — sohbet zaten
    // YENI anahtarla acilmis olur.
    const bool sohbet_vardi = sohbet_calisiyor();

    const auto d = anahtar_dogrula();

    // ---- NEDEN YENIDEN BASLATIYORUZ -------------------------------------
    //
    // GeminiLiveClient anahtari KURULURKEN aliyor ve icinde tutuyor.
    // Anne kotasi dolmus bir anahtari yenisiyle degistirdiginde, ayakta
    // duran istemci hala ESKISINI kullaniyor: panel "anahtar gecerli"
    // der, robot susmaya devam eder. Panelin yalan soyledigi bu durum
    // bu projede uc kez cikti (PLAN.md).
    //
    // Istemciyi yerinde degistirmek de mumkundu ama ses gorevi,
    // mikrofon gorevi ve olay kuyrugu ona bagli; hepsini akis
    // ortasinda soktan cikarip takmak, kazanci kadar risk. Yeniden
    // baslamak 3-4 saniye suruyor ve hafiza NVS'te duruyor.
    //
    // IKINCI HAL: sohbet hic baslamamis ve HALA baslamamis.
    //
    // app_main anahtari bekleyen bir donguda ve normalde yeni anahtari
    // birkac saniyede goruyor. Ama anahtar ONCEDEN gecersizse o dongu
    // uzun beklemeye geciyor (60 sn) — Google'in "hayir" cevabini
    // saniyede bir tekrar sormanin anlami yok, ustelik kotasi dolmus bir
    // anahtari daha da doldurur.
    //
    // O beklemenin ortasinda dogru anahtar yazilirsa anne bir dakikaya
    // kadar hicbir sey olmadigini gorurdu. Yeniden baslamak 3-4 saniye.
    //
    // `sohbet_calisiyor()` DOGRULAMADAN SONRA okunuyor: dogrulama
    // saniyeler suruyor ve bu arada app_main sohbeti zaten baslatmis
    // olabilir. Baslattiysa yeniden baslatmaya gerek yok.
    if (d == AnahtarDurumu::Gecerli && (sohbet_vardi || !sohbet_calisiyor())) {
        ESP_LOGW(ETIKET, "anahtar gecerli — yeni anahtarla baslamak icin "
                         "yeniden baslaniyor");
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_restart();
    }
    vTaskDelete(nullptr);
}

esp_err_t anahtar_isle(httpd_req_t* r)
{
    std::string govde;
    if (!govde_oku(r, govde)) return hata_yolla(r, "400", "govde okunamadi");

    cJSON* k = cJSON_Parse(govde.c_str());
    if (k == nullptr) return hata_yolla(r, "400", "bozuk JSON");
    const std::string yeni = json_dize(k, "deger");
    cJSON_Delete(k);

    if (!anahtar_yaz(yeni)) {
        // Bicim denetimi. Sebebi SOYLUYORUZ: "kabul edilmedi" tek basina
        // anneyi ayni yanlisi tekrarlamaya birakir. En sik hata yarim
        // kopyalamak.
        return hata_yolla(r, "400",
                          "anahtar kabul edilmedi — eksik kopyalanmis "
                          "olabilir, tamamini yapistirin");
    }

    // Cevap ONCE gidiyor, sinama sonra: bkz. anahtar_gorevi.
    json_yolla(r, "{\"tamam\":true}");

    if (xTaskCreate(anahtar_gorevi, "pati_anh", 6144, nullptr, 3, nullptr)
        != pdPASS) {
        // Gorev acilamadi. Anahtar YAZILDI, sadece hemen sinanamiyor;
        // bir sonraki sohbet denemesi zaten sinayacak.
        ESP_LOGW(ETIKET, "anahtar sinama gorevi acilamadi");
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// POST /api/guncelleme — {"is":"bak"} ya da {"is":"kur"}
// ---------------------------------------------------------------------------
//
// Tek uc, iki is. /api/ayar'daki "alan" deseninin aynisi.
//
// Ikisi de HEMEN DONUYOR ve isi arka planda yapiyor; sonucu panel
// /api/durum yoklamasindan aliyor (guncelleme.durum / .yuzde). Ayri bir
// ilerleme baglantisi acilmiyor cunku panel zaten iki saniyede bir
// soruyor ve ESP32'de her acik soket sayili (LWIP_MAX_SOCKETS).
esp_err_t guncelleme_isle(httpd_req_t* r)
{
    std::string govde;
    if (!govde_oku(r, govde)) return hata_yolla(r, "400", "govde okunamadi");

    cJSON* k = cJSON_Parse(govde.c_str());
    if (k == nullptr) return hata_yolla(r, "400", "bozuk JSON");
    const std::string is = json_dize(k, "is");
    cJSON_Delete(k);

    if (is == "bak") {
        guncelleme_kontrol_et();
    } else if (is == "kur") {
        guncelleme_indir();
    } else {
        // Bilinmeyen isi SESSIZCE YUTMUYORUZ — /api/ayar'daki gerekcenin
        // aynisi: panel yeni bir sey gonderiyor ve firmware onu
        // tanimiyorsa, kullanici dugmeye basip hicbir sey olmadigini
        // gorur ve sebebini bulamaz.
        ESP_LOGW(ETIKET, "bilinmeyen guncelleme isi: %s", is.c_str());
        return hata_yolla(r, "400", "bilinmeyen istek");
    }
    return json_yolla(r, "{\"tamam\":true}");
}

// ---------------------------------------------------------------------------
// POST /api/fabrika
// ---------------------------------------------------------------------------

esp_err_t fabrika_isle(httpd_req_t* r)
{
    ESP_LOGW(ETIKET, "FABRIKA AYARLARI — hafiza, ag ve kullanim siliniyor");
    hafiza_her_seyi_unut();
    kullanim_sifirla();
    ayar_sifirla();
    ag_unut();

    // 🔴 GEMINI ANAHTARI BILEREK SILINMIYOR. Buraya `anahtar_sil()`
    // EKLEMEYIN.
    //
    // Anahtar cocuga ait bir veri degil, kurulum bilgisi: anne Google
    // hesabina girip almis ve panele elle yazmis. Fabrika ayarlari
    // "robotu kutudan yeni cikmis haline getir" demek, "annenin
    // Google'a geri gitmesini gerektir" demek degil.
    //
    // Anahtar zaten AYRI bir NVS bolumunde duruyor (partitions.csv), o
    // yuzden buradaki silmelerin hicbiri ona dokunmuyor. Anahtari
    // gercekten degistirmek isteyen panelden yenisini yaziyor.
    // Cevabi yollayip SONRA yeniden baslatiyoruz; yoksa tarayici
    // "baglanti kesildi" gosteriyor ve ebeveyn islemin basarisiz
    // oldugunu saniyor.
    json_yolla(r, "{\"tamam\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// DNS ele gecirme — captive portal
// ---------------------------------------------------------------------------
//
// Her A sorgusuna kendi IP'mizi donduruyoruz. Tam bir DNS sunucusu
// degil: sadece soruyu aynen geri yazip cevap bolumu ekliyor. Bu is
// icin yeterli ve ~60 satir.

void dns_gorevi(void*)
{
    const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        ESP_LOGE(ETIKET, "DNS soketi acilamadi");
        vTaskDelete(nullptr);
        return;
    }

    sockaddr_in adres{};
    adres.sin_family = AF_INET;
    adres.sin_addr.s_addr = htonl(INADDR_ANY);
    adres.sin_port = htons(53);
    if (bind(s, reinterpret_cast<sockaddr*>(&adres), sizeof(adres)) < 0) {
        ESP_LOGE(ETIKET, "DNS 53 portu baglanamadi");
        close(s);
        vTaskDelete(nullptr);
        return;
    }

    // Kendi IP'miz — cevapta bu donuyor.
    std::uint32_t bizim_ip = inet_addr(ag_ip());

    std::uint8_t tampon[512];
    while (g_dns_calisiyor.load()) {
        sockaddr_in kimden{};
        socklen_t n = sizeof(kimden);
        const int boy = recvfrom(s, tampon, sizeof(tampon), 0,
                                 reinterpret_cast<sockaddr*>(&kimden), &n);
        // 12 bayt baslik + en az bir soru
        if (boy < 13) continue;

        // Sadece standart sorgu (QR=0, OPCODE=0) ve tek soru.
        if ((tampon[2] & 0xF8) != 0x00) continue;
        if (tampon[4] != 0 || tampon[5] != 1) continue;

        // Soru bolumunun sonunu bul: uzunluk-onekli etiketler, 0 ile
        // biter, ardindan 4 bayt (tur + sinif).
        int i = 12;
        while (i < boy && tampon[i] != 0) {
            i += tampon[i] + 1;
        }
        i += 5;                       // sonlandirici 0 + QTYPE(2) + QCLASS(2)
        if (i > boy) continue;

        // Cevap: bayraklari "yanit + yetkili" yap
        tampon[2] = 0x84;
        tampon[3] = 0x00;
        tampon[6] = 0x00; tampon[7] = 0x01;   // ANCOUNT = 1
        tampon[8] = 0x00; tampon[9] = 0x00;   // NSCOUNT
        tampon[10] = 0x00; tampon[11] = 0x00; // ARCOUNT

        if (i + 16 > static_cast<int>(sizeof(tampon))) continue;
        std::uint8_t* c = tampon + i;
        *c++ = 0xC0; *c++ = 0x0C;             // isaretci: soru adina
        *c++ = 0x00; *c++ = 0x01;             // TYPE = A
        *c++ = 0x00; *c++ = 0x01;             // CLASS = IN
        // TTL 0: telefon adresi ONBELLEGE ALMASIN. Almazsa, ag
        // baglandiktan sonra ayni adi sorunca gercek IP'yi aliyor.
        *c++ = 0x00; *c++ = 0x00; *c++ = 0x00; *c++ = 0x00;
        *c++ = 0x00; *c++ = 0x04;             // RDLENGTH = 4
        std::memcpy(c, &bizim_ip, 4);
        c += 4;

        sendto(s, tampon, static_cast<size_t>(c - tampon), 0,
               reinterpret_cast<sockaddr*>(&kimden), n);
    }

    close(s);
    ESP_LOGI(ETIKET, "DNS ele gecirme kapandi");
    vTaskDelete(nullptr);
}

}  // namespace

// ---------------------------------------------------------------------------

esp_err_t panel_baslat()
{
    if (g_sunucu != nullptr) return ESP_OK;

    httpd_config_t k = HTTPD_DEFAULT_CONFIG();
    k.uri_match_fn = httpd_uri_match_wildcard;
    // Joker isleyici bilinmeyen adresleri de yakaliyor (captive portal),
    // o yuzden az sayida isleyici yetiyor.
    //
    // ⚠️ Su an 9 yol kayitli. Sinir asilirsa httpd son yollari SESSIZCE
    // kaydetmiyor — hata donuyor ama biz donus degerine bakmiyoruz ve
    // eksik olan yol 404 veriyor. Panelin bir dugmesi calismaz ve sebebi
    // hicbir yerde gorunmez. Pay birakiliyor.
    k.max_uri_handlers = 12;
    k.lru_purge_enable = true;
    k.stack_size = 6144;

    // Varsayilan 7 idi ve yetmiyordu: tarayici sayfayi acarken alti
    // dosyayi paralel cekiyor, ustune telefonun isletim sistemi captive
    // portal yoklamasi atiyor. 31.07.2026'da cihazda accept() ENFILE
    // ile dondu ve panel iki saniyede bir "baglanti koptu" gosterdi.
    //
    // 10 secildi cunku ust sinir LWIP_MAX_SOCKETS - 3 = 13 ve geri
    // kalani DNS sunucusuna, Gemini WebSocket'ine ve hafiza cikarimini
    // yapan HTTP istemcisine lazim.
    k.max_open_sockets = 10;
    // ⚠️ ONCELIK SES GOREVININ ALTINDA. Panel gecikme yolunda yer
    // almamali (PLAN.md). Varsayilan 5; 4 veriyoruz.
    k.task_priority = 4;

    const esp_err_t s = httpd_start(&g_sunucu, &k);
    if (s != ESP_OK) {
        ESP_LOGE(ETIKET, "HTTP sunucusu baslatilamadi: %s",
                 esp_err_to_name(s));
        return s;
    }

    const httpd_uri_t yollar[] = {
        {"/api/durum", HTTP_GET, durum_isle, nullptr},
        {"/api/aglar", HTTP_GET, aglar_isle, nullptr},
        {"/api/wifi", HTTP_POST, wifi_isle, nullptr},
        {"/api/ayar", HTTP_POST, ayar_isle, nullptr},
        {"/api/hafiza", HTTP_POST, hafiza_isle, nullptr},
        {"/api/anahtar", HTTP_POST, anahtar_isle, nullptr},
        {"/api/guncelleme", HTTP_POST, guncelleme_isle, nullptr},
        {"/api/fabrika", HTTP_POST, fabrika_isle, nullptr},
        // Joker EN SONDA: yukaridakiler once denenmeli.
        {"/*", HTTP_GET, dosya_isle, nullptr},
    };
    for (const auto& y : yollar) {
        httpd_register_uri_handler(g_sunucu, &y);
    }

    ESP_LOGI(ETIKET, "panel hazir: http://%s/", ag_ip());

    if (ag_durumu() == AgDurumu::Kurulum && !g_dns_calisiyor.exchange(true)) {
        if (xTaskCreate(dns_gorevi, "dns", 3072, nullptr, 4, nullptr)
            != pdPASS) {
            g_dns_calisiyor.store(false);
            ESP_LOGW(ETIKET, "DNS gorevi acilamadi — sayfa kendiliginden "
                             "acilmayacak, adres elle yazilmali");
        }
    }
    return ESP_OK;
}

void panel_kurulum_bitti()
{
    // DNS ele gecirmeyi KAPAT. Acik kalirsa ev agindaki butun DNS
    // sorgularina cevap vermeye devam eder ve baska cihazlarin
    // internetini bozar.
    g_dns_calisiyor.store(false);
}

}  // namespace pati
