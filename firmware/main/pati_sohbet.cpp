#include "pati_sohbet.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <span>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#include <conversation/gemini_live_client.hpp>

#include "pati_kisilik_uretilmis.h"
#include "pati_anahtar.hpp"
#include "pati_ayar.hpp"
#include "pati_cikarim.hpp"
#include "pati_gozler.hpp"
#include "pati_hafiza.hpp"
#include "pati_kullanim.hpp"
#include "pati_olcum.hpp"
#include "pati_pinler.h"
#include "pati_ses.hpp"

namespace pati {
namespace {

using stackchan::conversation::ConversationConfig;
using stackchan::conversation::ConversationEvent;
using stackchan::conversation::ConversationEventType;
using stackchan::conversation::ConversationState;
using stackchan::conversation::GeminiLiveClient;

constexpr const char* ETIKET = "pati.sohbet";

// Olay kuyrugu derinligi.
//
// Ses parcalari shared_ptr ile tasindigi icin kuyruk sadece isaretci
// tutuyor — 32 derinlik ~128 bayt. Bol tutmanin bedeli yok, dolmasinin
// bedeli var (parca dusuyor).
constexpr std::size_t OLAY_KUYRUK_DERINLIK = 32;

std::unique_ptr<GeminiLiveClient> g_istemci;
ConversationConfig g_ayar{};
QueueHandle_t g_kuyruk = nullptr;

volatile bool g_calisiyor = false;
volatile bool g_konusuyor = false;    // robot su an konusuyor mu
volatile bool g_goaway = false;       // kapanma uyarisi geldi, yenileme bekliyor
volatile bool g_yenileniyor = false;  // yenileme SURUYOR -> ses gonderme

// UYKU — maliyetin en buyuk kalemi (PLAN.md)
//
// Acik oturum, kimse konusmasa bile mikrofonu akitiyor ve ses girisi
// dakika basina ucretleniyor. Masada bekleyen bir robot icin bu,
// sohbetin kendisinden PAHALIYA geliyor: gunde 12 saat bosta =
// 720 dk x $0,005 = $3,60/gun = ~$108/ay.
//
// Cozum: sessizlikte oturumu KAPAT, cocuk konusunca yeniden AC.
// Asama 1'de olculdu: uyandirma 617 ms — cocuk farki hissetmiyor.
volatile bool g_uykuda = false;
std::int64_t g_son_hareket_us = 0;
std::uint32_t g_uyku_sayisi = 0;

std::uint32_t g_tur = 0;
std::uint32_t g_dusen_olay = 0;
std::uint32_t g_gonderilemeyen = 0;  // push_audio basarisiz oldu

// ---------------------------------------------------------------------------
// Olay geri cagirimi — ISTEMCININ KENDI GOREVINDE calisiyor
//
// conversation_service.hpp: "It must be cheap and non-blocking — the
// recommended pattern is to marshal the event onto a FreeRTOS queue."
//
// Burada SADECE kopyalayip kuyruga atiyoruz. Hoparlore yazmak bloklayan
// bir is; burada yapilsa websocket gorevi durur ve ses kekelemeye baslar.
// ---------------------------------------------------------------------------
void olay_geldi(const ConversationEvent& olay)
{
    // Kuyruk POD olmayan tip tasiyamiyor (std::string + shared_ptr var),
    // o yuzden isaretci tasiyoruz. Kopya kurucu shared_ptr sayacini
    // dogru artiriyor.
    auto* p = new (std::nothrow) ConversationEvent(olay);
    if (p == nullptr) {
        ++g_dusen_olay;
        return;
    }
    if (xQueueSend(g_kuyruk, &p, 0) != pdTRUE) {
        // Kuyruk dolu. SIZDIRMIYORUZ — silip sayiyoruz. Sessizce
        // atlamak, olcum eksik cikinca sebebini gizler.
        delete p;
        ++g_dusen_olay;
    }
}

// ---------------------------------------------------------------------------
// GoAway yenilemesi — konusmanin ilk dogal bosluguna kadar bekler
// ---------------------------------------------------------------------------
// Sistem promptunu kurar: KURALLAR + hafiza.
//
// Sira prototype/kisilik.py'nin gerekcesiyle ayni: kurallar ONCE, hafiza
// SONRA. Tersi yapilirsa kurallar hafizanin arkasinda kaliyor ve prompt
// buyudukce seyreliyorlar.
//
// 🔴 URETILEN PROMPT HAFIZA ICERMIYOR. prompt_uret.py bir donem
// `kisilik.SISTEM_PROMPTU` kullaniyordu ve o sabit hafizayi da
// icerdigi icin firmware'e uretim anindaki hafiza blogu GOMULUYORDU.
// Sonuc celiskili talimat olurdu: gomulu metin "bu cocukla henuz
// tanismadin" derken buradaki blok "adi Bulut" diyecekti.
//
// HER OTURUM ACILISINDA yeniden kuruluyor cunku hafiza degisiyor.
// Prompt setup mesajinda gidiyor ve setup yalnizca oturum acilirken
// gonderiliyor — yani yenileme aninda da guncellenmesi SART, yoksa
// robot yeni ogrendigini bir sonraki fis takmaya kadar kullanmiyor.
void prompt_kur()
{
    std::string p = SISTEM_PROMPTU;

    // ROBOTUN ADI. Uretilen metinde {ROBOT_ADI} token'i duruyor; cocuk
    // adi degistirmis olabilir ("bundan sonra senin adin Osman"), o
    // yuzden CALISMA ANINDA yerine koyuluyor. Uretim aninda gomulseydi
    // firmware'deki ad hic degismezdi.
    //
    // prompt_kur() her oturum acilisinda ve her yenilemede cagriliyor,
    // yani cocuk adi degistirdikten sonra ilk yenilemede gecerli oluyor
    // (prototype'de ayni is: hafiza cikarimi robot_adi gorunce
    // baglanti.yenile() diyor).
    const auto yeri = p.find(ROBOT_ADI_TOKEN);
    if (yeri != std::string::npos) {
        p.replace(yeri, std::strlen(ROBOT_ADI_TOKEN), hafiza_robot_adi());
    }

    // YUZ ARACI — panelden aciksa hem prompta hem setup'a giriyor.
    //
    // Ikisi birden gerekli ve sirasi onemli: prompt eki KURALLARIN
    // hemen ardina, hafiza blogundan ONCE geliyor. Hafizanin arkasina
    // koysaydik prompt buyudukce seyrelirdi — kisilik.py'nin
    // "kurallar once, hafiza sonra" gerekcesinin aynisi.
    //
    // PC'de ayni is canli.py §80-94'te yapiliyor. Iki tarafin ayni
    // metni ve ayni semayi kullanmasi sart, yoksa "PC'de gozler
    // degisiyor, robotta degismiyor" diye aranacak bir fark cikar.
    g_ayar.tools.clear();
    if (ayar_yuz_araci()) {
        p += YUZ_PROMPT_EKI;
        g_ayar.tools.push_back(stackchan::conversation::ToolDefinition{
            YUZ_ARAC_ADI, YUZ_ARAC_ACIKLAMA, YUZ_ARAC_SEMA});
    }

    const std::string blok = hafiza_prompt_blogu();
    if (!blok.empty()) {
        p += "\n\n";
        p += blok;
    }
    g_ayar.instructions = std::move(p);

    // Ses ve VAD de setup mesajinda gidiyor: yenilemede onlar da
    // guncellenmeli, yoksa panelden ses degistirmek hicbir sey
    // yapmiyor gibi gorunur.
    g_ayar.voice = ayar_ses_adi();
    if (ayar_vad_ms() > 0) {
        g_ayar.vad_silence_ms = ayar_vad_ms();
    }
}

// Oturumu kapatir ama NESNEYI YASATIR.
//
// Devam anahtari istemcinin icinde duruyor, yani cocuk konusunca
// KALDIGI YERDEN devam ediliyor — robot unutmuyor, sadece susuyor.
void uyu()
{
    if (g_uykuda || !g_calisiyor) return;

    const std::int64_t bosta_sn =
        (esp_timer_get_time() - g_son_hareket_us) / 1000000;
    ESP_LOGW(ETIKET, "%lld sn sessizlik -> uyudu (ucret/kota yakmasin)",
             bosta_sn);

    g_uykuda = true;
    g_istemci->stop();
    ++g_uyku_sayisi;
    gozler_durum("uykulu");

    // Sayaci ONCE durdur: cikarim asagida birkac saniye suruyor ve o
    // saniyeler uykuya ait, faturaya degil. (prototype/pati.py
    // §uyku_durumu ile ayni sira.)
    kullanim_duraklat();

    // HAFIZA CIKARIMI TAM BURADA.
    //
    // PC'de cikarim "durdur" dugmesine basilinca calisiyordu ve gercek
    // kullanimda o an HIC gelmiyor: cocuk konusmayi birakip gidiyor,
    // robotta fis cekiliyor. Uyku, konusmanin bittiginin dogal
    // isareti — ve cocuk zaten beklemiyor.
    //
    // BLOKLUYOR (~2-10 sn) ama sorun degil: oturum kapali, ses yolu
    // bos, cocuk ortada yok.
    if (cikarim_dokum_var()) {
        const int n = cikarim_calistir();
        if (n > 0) {
            ESP_LOGI(ETIKET, "%d yeni bilgi ogrenildi", n);
            // Prompt bir sonraki uyanmada yeniden kuruluyor; yeni
            // ogrenilen sey hemen kullanilabilir olsun.
        } else if (n < 0) {
            ESP_LOGW(ETIKET, "cikarim basarisiz — dokum duruyor, "
                             "sonraki uykuda tekrar denenecek");
        }
    }
}

// Cocuk konustu. Yerel VAD'den cagriliyor.
void uyandir()
{
    if (!g_uykuda || !g_calisiyor) return;

    const std::int64_t t0 = esp_timer_get_time();
    g_uykuda = false;
    g_son_hareket_us = t0;

    // Prompt tazeleniyor: uykuda yeni bilgi ogrenilmis olabilir.
    prompt_kur();
    const auto sonuc = g_istemci->start(g_ayar);
    if (!sonuc.has_value()) {
        ESP_LOGE(ETIKET, "uyanamadi — tekrar denenecek");
        // SEBEBINI SOR. WebSocket el sikismasi basarisiz oldugunda HTTP
        // kodu gorunmuyor: "anahtar iptal edilmis" ile "wifi koptu" ayni
        // hataya benziyor. Anahtar katmani kucuk bir REST istegiyle
        // ayirt ediyor ve panel dogru olani yaziyor. Kendini dakikada
        // bire sinirliyor, yani uyanma dongusunde her denemede istek
        // atilmiyor.
        anahtar_baglanti_hatasi();
        g_uykuda = true;
        return;
    }
    anahtar_kod_bildir(200);
    kullanim_devam();
    gozler_dinliyor();
    ESP_LOGI(ETIKET, "uyandi (%lld ms)", (esp_timer_get_time() - t0) / 1000);
}

void yenileme_gerekirse()
{
    // Iki sebep: sunucu kapanma uyarisi verdi (GoAway) ya da panelden
    // bir ayar degisti (ses, tizlik, "sustu" karari, yuz araci). Ikisi
    // de setup mesajini yeniden gondermeyi gerektiriyor ve ikisi de
    // ayni yolu kullaniyor — olculen bosluk 568 ms, hafiza korunuyor.
    const bool ayar_degisti = ayar_yenileme_gerekli();
    if ((!g_goaway && !ayar_degisti) || g_yenileniyor || !g_calisiyor) {
        return;
    }
    // Tur ortasinda yenilemek Python'da cevabin kaybolmasina yol acmisti
    // ("...oncedeni!Merkur!..." diye yapisik geliyordu). Bekliyoruz.
    if (acik_tur() != nullptr || g_konusuyor) {
        return;
    }

    ESP_LOGW(ETIKET, "kapanma uyarisi vardi, konusma arasinda yenileniyor");
    const std::int64_t t0 = esp_timer_get_time();

    // Bayrak ONCE kalkiyor: mikrofon gorevi bu pencerede push_audio()
    // cagirmasin. Python'da tam bu eksikti ve kapali baglantiya yazip
    // programi cokertmisti.
    g_yenileniyor = true;

    g_istemci->stop();
    // Hafiza bu tur icinde degismis olabilir (yeni bir sey ogrenildi,
    // ebeveyn not yazdi); prompt tazelensin.
    prompt_kur();
    const auto sonuc = g_istemci->start(g_ayar);

    g_yenileniyor = false;

    if (!sonuc.has_value()) {
        // Bayragi BIRAKMIYORUZ: bir sonraki bosta tekrar denenecek.
        ESP_LOGE(ETIKET, "yenileme basarisiz, tekrar denenecek");
        anahtar_baglanti_hatasi();
        return;
    }

    g_goaway = false;
    ayar_yenileme_temizle();
    ESP_LOGI(ETIKET, "yenilendi (%lld ms boslukla, oturum anahtari korundu)",
             (esp_timer_get_time() - t0) / 1000);
}

// ---------------------------------------------------------------------------
// Olayi isle — ses gorevinde
// ---------------------------------------------------------------------------
// Cocuk "bunu unutma" anlaminda bir sey soyledi mi?
//
// `prototype/metin.py` §unutma_istegi ile ayni liste. Ayni olmasi sart:
// iki tarafta farkli davranirsa "PC'de hatirladi, robotta hatirlamadi"
// diye aranmayacak bir fark cikar.
//
// NEDEN VAR: cikarim promptu normalde "emin degilsen yazma" diyor ve
// model cocugun ozellikle istedigi seyi atliyordu. Bu satirlar dokume
// "(!)" ile giriyor ve prompt onlar icin "MUTLAKA yaz" diyor.
//
// AGIR DEGIL: tur basina bir kez, tek bir kisa cumlede birkac alt dize
// aramasi. Regex ya da sozluk yigini yok (PLAN.md).
bool unutma_istegi_mi(const std::string& metin)
{
    if (metin.empty()) return false;
    static const char* const KALIP[] = {
        "unutma", "unutmayasin", "unutmayasın", "aklinda tut",
        "aklında tut", "aklinda kalsin", "aklında kalsın", "hatirla",
        "hatırla", "hafizana kaydet", "hafızana kaydet", "hafizana yaz",
        "hafızana yaz", "kaydet bunu", "bunu kaydet", "not al",
        "bilmeni istiyorum",
    };
    const std::string k = hafiza_kucult(metin);
    for (const char* p : KALIP) {
        if (k.find(p) != std::string::npos) return true;
    }
    return false;
}

void olayi_isle(const ConversationEvent& olay)
{
    switch (olay.type) {
    case ConversationEventType::StateChanged:
        g_konusuyor = (olay.state == ConversationState::Speaking);
        ESP_LOGD(ETIKET, "durum: %d", static_cast<int>(olay.state));
        break;

    case ConversationEventType::SpeechStarted:
        // Gozler konusma AKISINDAN suruluyor, modelden degil. Model
        // "su an dinliyorum" demeyi beceremez; bunu zaten biliyoruz.
        // (prototype/yuz.py §AKIS_DURUMLARI ile ayni dort ad.)
        gozler_dinliyor();
        g_son_hareket_us = esp_timer_get_time();   // bosta sayacini sifirla
        // Robot konusurken cocuk konusmaya basladi = SOZUNU KESME.
        if (g_konusuyor) {
            damga_kesme_konusma();
            ESP_LOGI(ETIKET, "sozunu kesme: cocuk ustune konustu");
            // Iki is birden: sunucuya "uretmeyi birak" de, ve DMA'da
            // bekleyen sesi at. Ikincisi olmadan robot susmus gorunup
            // birkac saniye sonra eski cumlesine devam ediyor.
            //
            // Donus degeri YOK SAYILMIYOR: basarisiz olursa sozunu kesme
            // hic calismaz ve sebebini bilmeden ararız.
            if (const auto r = g_istemci->cancel_response(); !r.has_value()) {
                ESP_LOGE(ETIKET, "cancel_response BASARISIZ — sozunu kesme "
                                 "sunucuya iletilemedi");
            }
            hoparlor_temizle();
            g_konusuyor = false;
        }
        break;

    case ConversationEventType::SpeechStopped:
        // OLCUMUN SIFIR NOKTASI. emit_us geciyor: kuyrukta bekleme
        // suresi olcume karismasin.
        damga_sustu(olay.emit_us);
        // Cocuk sustu, cevap bekleniyor. PC'de olculen medyan 1325 ms;
        // cocuk o sureyi bos bir yuze bakarak gecirmesin.
        gozler_dusunuyor();
        break;

    case ConversationEventType::AssistantAudioChunk: {
        if (!olay.audio || olay.audio->empty()) {
            break;
        }
        const std::size_t bayt = olay.audio->size() * sizeof(std::int16_t);
        damga_ilk_paket(bayt, olay.emit_us);

        hoparlor_yaz(std::span<const std::int16_t>(olay.audio->data(),
                                                   olay.audio->size()));
        // Yazma DONDUKTEN SONRA damgaliyoruz: i2s_channel_write, veri
        // DMA'ya kopyalanana kadar bekliyor. Yani bu an, sesin gercekten
        // donanima teslim edildigi an.
        //
        // Damga yalnizca ilk cagrida yaziliyor (fonksiyon kendi koruyor).
        damga_ilk_hoparlor();
        if (!g_konusuyor) {
            gozler_konusuyor();
        }
        g_konusuyor = true;
        break;
    }

    case ConversationEventType::ResponseDone:
    case ConversationEventType::AssistantAudioDone:
        // SOZUNU KESME ONAYI.
        //
        // Gemini'nin cevabi kesildiginde sunucu serverContent.interrupted
        // gonderiyor ve istemci onu AssistantAudioDone'a ceviriyor
        // (gemini_live_client.cpp:740). Ayri bir "iptal edildi" olayi YOK,
        // o yuzden onay damgasi burada aliniyor.
        //
        // Damga yalnizca bu turda kesme olduysa yaziliyor; fonksiyon
        // kendini bir kez calisacak sekilde koruyor.
        if (const Tur* t = acik_tur(); t != nullptr && t->kesildi) {
            damga_kesme_onay(olay.emit_us > 0 ? olay.emit_us : 0);
        }
        if (acik_tur() != nullptr) {
            dusen_parca_bildir(g_istemci->tx_evicted_chunks());
            tur_kapat();
            tur_ozeti_yaz();
            ++g_tur;
        }
        g_konusuyor = false;
        gozler_bos();
        // Tur bitti — yenileme icin dogru an burasi.
        yenileme_gerekirse();
        break;

    case ConversationEventType::GoingAway:
        // Kopmayi ENGELLEMIYORUZ, NE ZAMAN olacagini seciyoruz.
        ESP_LOGW(ETIKET, "goAway: kalan sure %s", olay.text.c_str());
        g_goaway = true;
        break;

    case ConversationEventType::UserTranscript:
        ESP_LOGI(ETIKET, "cocuk: %s", olay.text.c_str());
        // Dokum uykuda cikarima gidiyor: robot boyle ogreniyor.
        //
        // "(!)" = cocuk bunu OZELLIKLE hatirlanmasini istedi ("bunu
        // unutma", "aklinda tut"). Cikarim promptu o satirlar icin
        // "emin degilsen yazma" kuralini KALDIRIYOR. Isaret olmadan
        // model temkinli davranip atliyordu.
        // (prototype/pati.py §_hafizayi_guncelle ile ayni.)
        cikarim_dokum_ekle(unutma_istegi_mi(olay.text) ? "Cocuk (!)"
                                                       : "Cocuk",
                           olay.text);
        break;

    case ConversationEventType::AssistantTextDone:
        ESP_LOGI(ETIKET, "Pati: %s", olay.text.c_str());
        cikarim_dokum_ekle("Robot", olay.text);
        break;

    case ConversationEventType::ToolCallRequested:
        // MODEL KENDI IFADESINI SECTI.
        //
        // Tek aracimiz bu. Isin tamami bir atomik isaretci yazmak
        // (gozler_durum), yani BEKLETMIYOR — cizim ayri gorevde.
        //
        // 🔴 CEVAP HEMEN GONDERILIYOR. Cihazda arac SIRALI calisiyor
        // (NON_BLOCKING alani istemcide yok, bkz. uretilmis baslik):
        // cevap gecikirse model susmus halde bekler ve cocuk robotun
        // dondugunu sanar. Basarisiz olursa da SESSIZ kalmiyoruz —
        // model sonsuza kadar bekleyebilir ve sebebi gorunmez olurdu.
        if (olay.tool_call.has_value()) {
            const auto& c = *olay.tool_call;
            std::string ifade;
            if (cJSON* a = cJSON_Parse(c.arguments_json.c_str());
                a != nullptr) {
                const cJSON* i =
                    cJSON_GetObjectItemCaseSensitive(a, "ifade");
                if (cJSON_IsString(i) && i->valuestring != nullptr) {
                    ifade = i->valuestring;
                }
                cJSON_Delete(a);
            }
            if (!ifade.empty()) {
                // Bilinmeyen ad gelirse gozler_durum sayaci artiriyor
                // ve gunluge yaziyor; burada ayrica susturmuyoruz.
                gozler_durum(ifade.c_str());
                ESP_LOGI(ETIKET, "ifade: %s", ifade.c_str());
            } else {
                ESP_LOGW(ETIKET, "yuz araci bos 'ifade' ile cagrildi: %s",
                         c.arguments_json.c_str());
            }
            if (const auto r = g_istemci->submit_tool_result(
                    c.call_id, R"({"tamam":true})");
                !r.has_value()) {
                ESP_LOGE(ETIKET, "arac cevabi gonderilemedi — model "
                                 "bekliyor olabilir");
            }
        }
        break;

    case ConversationEventType::Error:
        ESP_LOGE(ETIKET, "istemci hatasi: %s", olay.text.c_str());
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Ses gorevi — kuyruktan alip isler
// ---------------------------------------------------------------------------
void ses_gorevi(void* /*arg*/)
{
    ConversationEvent* p = nullptr;
    while (g_calisiyor) {
        if (xQueueReceive(g_kuyruk, &p, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (p != nullptr) {
                olayi_isle(*p);
                delete p;
                p = nullptr;
            }
        } else {
            // Kuyruk bos = konusma boslugu. Yenileme icin ideal an.
            yenileme_gerekirse();

            // BOSTA BEKCISI. Tur ortasinda uyumuyoruz: yarim kalan
            // cevap kaybolurdu.
            if (!g_uykuda && acik_tur() == nullptr && !g_konusuyor) {
                const std::int64_t bosta_us =
                    esp_timer_get_time() - g_son_hareket_us;
                if (bosta_us > static_cast<std::int64_t>(ayar_uyku_dk())
                                   * 60 * 1000000) {
                    uyu();
                }
            }
        }
    }
    vTaskDelete(nullptr);
}

// Bu parcada konusma var mi? Uykudan uyandirmak icin.
//
// Esik `prototype/ayarlar.py` §VAD_ESIK_TABAN ile ayni (90 RMS) — iki
// tarafta ayni sayi olmasi onemli, yoksa robot PC'de uyanip cihazda
// uyanmiyor (ya da tersi) ve sebebi aranmaz.
//
// Yanlis pozitifin bedeli "biraz daha uyanik kaldi"; yanlis negatifin
// bedeli COCUGU DUYMAMAK. O yuzden esik cömert.
bool konusma_var_mi(const std::array<std::int16_t, PATI_OKUMA_ORNEK>& p,
                    std::size_t n)
{
    if (n == 0) return false;
    // Kareler toplami 32 bite sigmiyor (32767^2 x 320); 64 bit.
    std::int64_t toplam = 0;
    for (std::size_t i = 0; i < n; ++i) {
        toplam += static_cast<std::int64_t>(p[i]) * p[i];
    }
    const float rms = std::sqrt(static_cast<float>(toplam)
                                / static_cast<float>(n));
    return rms >= 90.0f;
}

// Uyandirmak icin kac ms KESINTISIZ ses gerekiyor.
//
// `prototype/ayarlar.py` §VAD_KONUSMA_MS ile ayni (120 ms) — esik gibi bu
// sayinin da iki tarafta ayni olmasi sart, yoksa robot PC'de uyanip
// cihazda uyanmiyor ve sebebi aranmaz.
//
// NEDEN TEK PARCA YETMIYOR: 20 ms'lik tek bir parca esigi gecince
// uyandiriyorduk. Kapi carpmasi, oyuncak dusmesi, oksuruk — hepsi
// uyandiriyor ve her yanlis uyanma en az bir uyku suresi kadar ucret
// demek (varsayilan 90 sn, $0.005/dk giris). Konusma sureklidir,
// darbe sesleri degil; araya giren sessiz parca sayaci sifirliyor.
//
// Gecikmeye etkisi yok: uyanma zaten 617 ms olculdu (prototype),
// 120 ms onun icinde kayboluyor.
constexpr std::int64_t UYANMA_SESLI_US = 120 * 1000;

// ---------------------------------------------------------------------------
// Mikrofon gorevi
// ---------------------------------------------------------------------------
void mik_gorevi(void* /*arg*/)
{
    std::array<std::int16_t, PATI_OKUMA_ORNEK> parca{};
    // Kesintisiz ses ne kadar surdu (bkz. UYANMA_SESLI_US).
    std::int64_t sesli_us = 0;

    while (g_calisiyor) {
        const std::size_t n = mikrofon_oku(parca, 100);
        if (n == 0) {
            continue;
        }
        // 🔴 YENILEME PENCERESI: ses GONDERILMIYOR, dusuruluyor.
        //
        // Python'da tam burasi eksikti: yenileme sirasinda kapali sokete
        // yazildi ve program coktu. Dusen ses 500 ms'lik bir sessizlik;
        // yenileme zaten konusma boslugunda yapiliyor.
        if (g_yenileniyor) {
            continue;
        }

        // UYKUDA: mikrofonu DINLIYORUZ ama GONDERMIYORUZ.
        //
        // PLAN.md: "Uykudayken mikrofonu dinle ama gonderme." Gonderme
        // olmadigi icin ucret islemiyor; dinleme yerel ve bedava.
        // Cocuk konusunca uyaniyoruz.
        if (g_uykuda) {
            if (konusma_var_mi(parca, n)) {
                sesli_us += static_cast<std::int64_t>(n) * 1000000
                            / PATI_MIK_HZ;
                if (sesli_us >= UYANMA_SESLI_US) {
                    sesli_us = 0;
                    uyandir();
                }
            } else {
                // Araya sessizlik girdi: zincir kirildi. Darbe sesleri
                // (carpma, dusme) boylece birikemiyor.
                sesli_us = 0;
            }
            continue;
        }
        sesli_us = 0;

        // YANKI KORUMASI — artik CALISMA ANINDA, panelden.
        //
        // Hoparlorle mikrofon yan yana duruyor; robot konusurken mikrofon
        // kendi sesini duyuyor ve sunucunun VAD'i bunu "cocuk konusuyor"
        // sanip robotun sozunu kesiyor. Robot kendi sozunu keser.
        // iPhone'da CANLI GORULDU (prototype/NOTLAR.md §3e).
        //
        // Koruma acikken robot konusurken ses gonderilmiyor; bedeli
        // sozunu kesmenin calismamasi. O yuzden karar ebeveynin:
        // kulaklik varsa soz kesme acilabilir.
        //
        // Eskiden burasi `#if CONFIG_PATI_YARIM_DUPLEKS` idi — derleme
        // zamani. Panel dugmesi NVS'e yaziyor ama bu satira ulasmiyordu,
        // yani dugme hicbir sey yapmiyordu. Ayni hata ses/tizlik/VAD'de
        // de yasanmisti (PLAN.md "panel artik sozunu tutuyor").
        //
        // Gercek cozum govde tasariminda (PLAN.md): hoparloru
        // mikrofondan uzaga ve ters yone koymak.
        if (!ayar_soz_kesme() && g_konusuyor) {
            continue;
        }

        // Donus degeri YOK SAYILMIYOR.
        //
        // Sessizce basarisiz olursa ses Gemini'ye HIC gitmez ve robot
        // hicbir sey duymaz. Bu, dis dunyadan "robot cevap vermiyor"
        // diye gorunur ve sebebi mikrofonda ya da agda aranir. Sayaci
        // rapora yaziyoruz.
        if (const auto r = g_istemci->push_audio(
                std::span<const std::int16_t>(parca.data(), n));
            !r.has_value()) {
            ++g_gonderilemeyen;
        }
    }
    vTaskDelete(nullptr);
}

}  // namespace

// ---------------------------------------------------------------------------
// Kurulum
// ---------------------------------------------------------------------------

esp_err_t sohbet_baslat()
{
    // ANAHTAR ARTIK PANELDEN. Eskiden burada Kconfig'e bakiliyor ve bos
    // ise app_main programi durduruyordu — anahtar derleme zamani
    // geldigi icin dogruydu, "bos" demek "yanlis derledin" demekti.
    //
    // Simdi bos olmasi NORMAL bir hal: kutudan yeni cikmis robotta
    // anahtar yok ve anne panele girip yazacak. Durmak yerine geri
    // donuyoruz; app_main bekliyor, panel ne yapilmasi gerektigini
    // yaziyor.
    const std::string anahtar = anahtar_al();
    if (anahtar.empty()) {
        ESP_LOGW(ETIKET, "Gemini anahtari yok — panelden girilmesi bekleniyor");
        return ESP_ERR_INVALID_STATE;
    }

    g_kuyruk = xQueueCreate(OLAY_KUYRUK_DERINLIK, sizeof(ConversationEvent*));
    if (g_kuyruk == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    // ---- ayarlar: Asama 1'de OLCULEN degerler -------------------------
    prompt_kur();
    // Ses ve "sustu" karari artik NVS'ten (panelden degistirilebilir).
    // Kayit yoksa Kconfig varsayilani kullaniliyor, yani Asama 2
    // davranisi bozulmuyor ve eski olcumlerle karsilastirilabilirlik
    // duruyor.
    g_ayar.voice = ayar_ses_adi();
    g_ayar.model = CONFIG_PATI_MODEL;
    // ⚠️ Varsayilan ikisi de 24000. Giris 16 kHz olmali; yanlis olursa
    // Gemini sesi yavas/hizli duyar ve VAD hic tutmaz.
    g_ayar.input_sample_rate_hz = PATI_MIK_HZ;
    g_ayar.output_sample_rate_hz = PATI_HOP_HZ;
    g_ayar.enable_input_transcription = true;  // dokum: uyum sayimi icin
    if (ayar_vad_ms() > 0) {
        g_ayar.vad_silence_ms = ayar_vad_ms();
    }

    ESP_LOGI(ETIKET, "model=%s ses=%s giris=%lu Hz cikis=%lu Hz vad=%lu ms",
             g_ayar.model.c_str(), g_ayar.voice.c_str(),
             static_cast<unsigned long>(g_ayar.input_sample_rate_hz),
             static_cast<unsigned long>(g_ayar.output_sample_rate_hz),
             static_cast<unsigned long>(g_ayar.vad_silence_ms));
    ESP_LOGI(ETIKET, "sistem promptu %d karakter (prototype/kisilik.py'den)",
             PROMPT_KARAKTER);

    g_istemci = std::make_unique<GeminiLiveClient>(anahtar);
    g_istemci->set_event_callback(olay_geldi);

    const auto sonuc = g_istemci->start(g_ayar);
    if (!sonuc.has_value()) {
        ESP_LOGE(ETIKET, "Gemini baglantisi baslatilamadi");
        // Anahtar mi bozuk, ag mi kopuk? Panelin dogru olani yazabilmesi
        // icin soruyoruz. Bu, kutudan cikan robotta anahtar YANLIS
        // yazildiginda ilk anda gorulen tek isaret.
        anahtar_baglanti_hatasi();

        // Temizlik. app_main tekrar deneyecek ve her denemede yeni bir
        // kuyruk ayirmak bellegi sizdirirdi.
        //
        // ⚠️ SIRA ONEMLI: once ISTEMCI, sonra kuyruk. Istemcinin geri
        // cagirimi (olay_geldi) kuyruga yaziyor ve istemci kendi
        // gorevinde calisiyor. Kuyrugu once silseydik, o gorev henuz
        // kapanmamisken silinmis bir kuyruga yazabilirdi.
        g_istemci.reset();
        vQueueDelete(g_kuyruk);
        g_kuyruk = nullptr;
        return ESP_FAIL;
    }

    // ANAHTARIN CALISTIGININ KANITI BU: oturum acildi.
    //
    // Gemini Live anahtari WebSocket el sikismasinda dogruluyor
    // (?key=...). Yani buraya gelmek, anahtarin Google tarafindan kabul
    // edildigi anlamina geliyor — ayrica bir sinama istegi atmaktan
    // daha guvenilir, cunku sinanan sey robotun GERCEKTEN yaptigi is.
    //
    // Bildirmek ayrica eski bir kotu durumu TEMIZLIYOR: anahtar bir ara
    // kotaya takilip sonra acildiysa panel "kota doldu" demeye devam
    // ederdi. Basarili her oturum onu siliyor.
    anahtar_kod_bildir(200);

    g_son_hareket_us = esp_timer_get_time();
    g_calisiyor = true;

    // 🔴 YUZU BURADA DUZELTIYORUZ, YOKSA "UYKULU" KALIYOR.
    //
    // 31.07.2026, gercek kartta goruldu: anahtar girildi, oturum acildi,
    // kullanim sayaci islemeye basladi — ama hem ekrandaki hem paneldeki
    // gozler UYKULU duruyordu. Sebep app_main'in anahtar bekleme
    // dongusu: anahtar yokken "beni ayarla" hali icin `uykulu` yaziyor
    // ve sohbet acilinca onu geri alan kimse yoktu. Ilk gerceklesme
    // ancak cocuk konustugunda oluyordu (`uyandir` -> gozler_dinliyor).
    //
    // Panelin robotun halini yanlis gostermesi bu projede dorduncu kez
    // cikti; hepsinde sebep ayniydi — durumu KURAN yer ile GOSTEREN yer
    // arasinda haber gitmiyor. O yuzden yuz, sohbetin kendi
    // sorumlulugunda: `sohbet_baslat` basarili donuyorsa robot
    // dinliyordur ve yuzu de oyle demelidir.
    gozler_dinliyor();

    // Ses gorevi once acilsin ki ilk olaylar kuyrukta beklemesin.
    //
    // Yigit boyutlari: ses gorevi hoparlor yaziyor ve olay isliyor (4 KB),
    // mikrofon gorevi sadece okuyup gonderiyor (3 KB). Oncelikler
    // websocket gorevinin (5) altinda kalsin ki ag isi aksamasin.
    xTaskCreate(ses_gorevi, "pati_ses", 4096, nullptr, 4, nullptr);
    xTaskCreate(mik_gorevi, "pati_mik", 3072, nullptr, 4, nullptr);

    ESP_LOGI(ETIKET, "sohbet basladi — konusabilirsin");
    return ESP_OK;
}

std::uint32_t sohbet_tur_sayisi()
{
    return g_tur;
}

bool sohbet_calisiyor()
{
    return g_calisiyor;
}

bool sohbet_uyuyor()
{
    return g_uykuda;
}

std::uint32_t sohbet_dusen_olay()
{
    return g_dusen_olay;
}

std::uint32_t sohbet_gonderilemeyen()
{
    return g_gonderilemeyen;
}

void sohbet_durdur()
{
    if (!g_calisiyor) return;

    ESP_LOGW(ETIKET, "sohbet durduruluyor");

    // Bayrak ONCE. Iki gorev de `while (g_calisiyor)` uzerinde donuyor;
    // once oturumu kapatsaydik, mikrofon gorevi kapali baglantiya
    // yazmaya devam ederdi. Ayni sira yenilemede de kullaniliyor ve
    // sebebi orada yazili: Python'da tam bu eksikti ve programi
    // cokertmisti.
    g_calisiyor = false;

    // Gorevlerin donguden cikmasi icin bir tur bekle. Mikrofon gorevi
    // en fazla bir okuma suresi kadar bloklu kaliyor (200 ms).
    vTaskDelay(pdMS_TO_TICKS(400));

    if (g_istemci) g_istemci->stop();
    kullanim_duraklat();
    gozler_bos();
}

}  // namespace pati
