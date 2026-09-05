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
using stackchan::conversation::ConversationError;
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

// ---------------------------------------------------------------------------
// KOPMA — baglanti oluyordu ve kimse toparlamiyordu
// ---------------------------------------------------------------------------
//
// 🔴 01.09.2026, GERCEK KARTTA YASANDI. Pati ile konusulurken ses kesildi
// ve BIR DAHA GELMEDI. Disaridan bakinca her sey saglamdi: wifi bagli,
// anahtar gecerli (kod 200), uyumuyor, gozler "dinliyor", mikrofon tepe
// genligi 23796/32767 — yani duyuyordu. Ama 235 saniye boyunca seri
// gunlukte TEK BIR sohbet satiri yok: ne dokum, ne cevap, ne tur.
//
// Sebep: gemini_live_client `disable_auto_reconnect = true` diyor. Yani
// tasima katmani BILEREK toparlanmiyor, isi uygulamaya birakiyor —
// istemcinin kendi yorumu "the conv-task recovery path takes over from
// here". O yol stackchan'da vardi, Pati'de YOKTU. `case Error:` sadece
// ESP_LOGE yaziyordu. Ping/pong kopmayi 25 saniyede goruyor, olay
// geliyor, log'a dusuyor ve orada oluyor.
//
// Sonuc zombi robot: mikrofonu okuyor, sesi olu sokete yaziyor, KULLANIM
// DAKIKASI ISLEMEYE DEVAM EDIYOR (14,0 -> 17,6 dk olculdu) ve yalnizca
// fisi cekmek duzeltiyor.
//
// BELIRTISI: gozler "dinliyor"da kalir, panel her seyi saglam gosterir,
// mikrofon tepesi normaldir ama sohbet satirlari akmaz. Boyle
// gorunuyorsa bakilacak yer burasi.
volatile bool g_koptu = false;
std::uint32_t g_kopma_sayisi = 0;   // toplam kopma
std::uint32_t g_deneme = 0;         // ust uste basarisiz toparlama
std::int64_t g_son_deneme_us = 0;

// Geri cekilme. Anahtar iptal edilmisse ya da kota bitmisse kopma
// KALICI; bosta dongusu 100 ms'de bir dondugu icin geri cekilme olmadan
// Gemini'ye saniyede onlarca el sikismasi atardik. Ilk deneme hemen
// (cocuk bekliyor), sonrakiler 2, 4, 8... en fazla 30 saniyede bir.
constexpr int GERI_CEKILME_TABAN_MS = 2000;
constexpr int GERI_CEKILME_EN_COK_MS = 30000;

// ---------------------------------------------------------------------------
// SESSIZ SUNUCU BEKCISI — hata gelmeyen kopma
// ---------------------------------------------------------------------------
//
// Yukaridaki toparlanma "hata olayi geldi" varsayimina dayaniyor.
// Ping/pong (15 sn arayla, 25 sn zaman asimi) kopmayi normalde
// yakaliyor — ama HER olu oturum soketi kapatmiyor. Soket ayakta
// kalabilir, ping'lere cevap gelebilir ve sunucu yine de tek kelime
// etmez (oturum siniri dolmus, kota bitmis, arka uc takilmis olabilir).
// O halde HICBIR hata olayi uretilmez ve toparlanma tetiklenmez.
//
// Disaridan gorunusu yine ayni: gozler "dinliyor", mikrofon duyuyor,
// Pati susuyor. Sikayet de ayni geliyor: "beni duyuyor mu bilmiyorum".
//
// Bu yuzden ikinci ve BAGIMSIZ bir olcut: cocuk konustu ve sunucu
// ondan beri HICBIR SEY soylemedi mi?
//
// Iki damganin karsilastirilmasi sart, tek basina sure yetmez: sessiz
// odada sunucunun susmasi NORMAL. Ancak konusma girdikten sonraki
// sessizlik anormal.
//
// Esik 25 saniye. Olculen gercek gecikme 10-25 ms (pati_olcum) ve
// sunucu VAD'i 500 ms sessizlik bekliyor, yani saglikli bir turda
// cevap bir saniyenin altinda basliyor. 25 sn bunun yirmi katindan
// fazla — yanlis pozitif icin cok yer birakiyor.
//
// Yanlis pozitifin bedeli ~600 ms'lik bir yeniden baglanma (uyanma
// olcumu: 617 ms) ve oturum anahtari korundugu icin robot hicbir seyi
// unutmuyor. Yanlis negatifin bedeli COCUGU DUYMAYAN ROBOT. Esik
// bilerek comert.
constexpr std::int64_t SESSIZ_SUNUCU_US = 25 * 1000000LL;

// 🔴 YANKI KUYRUGU — bekcinin en kolay yanlis anlamasi.
//
// Hoparlor tamponu 341 ms (pati_ses.cpp) ve mikrofon hoparlorun yani
// basinda. Robot sustuktan SONRA da tamponda ses var, mikrofon onu
// duyuyor ve yerel VAD "konusma" diyor.
//
// O yankiyi "cocuk konustu" saysaydik sunu yapardik: cocuk soruyor,
// Pati cevapliyor, cocuk susuyor — ve cevabin bitisinden 25 saniye
// sonra bekci "sunucu susuyor" deyip DURUP DURURKEN yeniden
// baglanirdi. Her cevaptan sonra bir kez, her gun onlarca kez.
//
// 1 saniye, 341 ms'lik tamponun uc kati: odadaki yankilanma payi da
// iceride.
constexpr std::int64_t YANKI_KUYRUGU_US = 1000000LL;

std::int64_t g_son_sunucu_us = 0;  // sunucudan en son ne zaman olay geldi
std::int64_t g_son_ses_us = 0;     // yerel VAD en son ne zaman konusma gordu
std::int64_t g_konusma_us = 0;     // robot en son ne zaman konusuyordu
std::uint32_t g_bekci_sayisi = 0;  // bekcinin kac kez kopma ilan ettigi

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

// Mikrofondan gelen en yuksek genlik — "Pati beni duyuyor mu" sorusunun
// tek olculebilir cevabi.
//
// 01.09.2026'da gercek kartta gerekti: her sey ayaktaydi (anahtar
// gecerli, oturum aciliyor, sure sayiliyor) ama Pati cevap vermiyordu.
// Sebep sunlardan biri olabilirdi: mikrofon susuyor, ses gonderilmiyor,
// Gemini duymuyor. Uc ayri yere bakmak gerekiyordu ve hicbiri
// gorunmuyordu. Bu sayi ilkini tek basina eliyor.
std::uint32_t g_mik_tepe = 0;
std::uint32_t g_mik_okuma = 0;  // kac kez okundu
std::uint32_t g_mik_bos = 0;    // kaci HIC veri dondurmedi

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
    // Bağlantı olayı ses kuyruğunda beklerken çocuk boş ekrana bakmasın.
    if (olay.type == ConversationEventType::StateChanged && !g_uykuda) {
        if (olay.state == ConversationState::Connecting ||
            olay.state == ConversationState::Error)
            gozler_baglanti_bildir(BaglantiUyarisi::Baglaniyor);
        else if (olay.state == ConversationState::Listening)
            gozler_baglanti_bildir(BaglantiUyarisi::Yok);
    }

    // KOPMA TESPITI TAM BURADA — kuyruga atmadan once.
    //
    // 🔴 NEDEN KUYRUKTAN SONRA DEGIL: `stop()` cagirdigimizda da ayni
    // olay geliyor ve o kopma DEGIL, bizim kararimiz. Uc yerde bilerek
    // kapatiyoruz (uyu, yenileme, durdurma) ve UCU DE bayragini ONCE
    // kaldirip sonra stop() cagiriyor — sira her uc yerde de bilincli,
    // gerekcesi kendi yorumlarinda yazili. Yani bayrak stop() boyunca
    // BASILI duruyor ve burada dogru cevabi veriyor.
    //
    // Olay kuyruktan cikarken ise bayrak coktan inmis olabilir: yenileme
    // stop()+start() yapip bayragi indiriyor, olay ise arkadan geliyor.
    // Karar orada verilseydi her yenilemeyi kopma sanardik.
    if (olay.type == ConversationEventType::Error && olay.error.has_value()
        && (*olay.error == ConversationError::NotConnected
            || *olay.error == ConversationError::TransportInit)
        && g_calisiyor && !g_uykuda && !g_yenileniyor && !g_koptu) {
        g_koptu = true;
        gozler_baglanti_bildir(BaglantiUyarisi::Baglaniyor);
        ++g_kopma_sayisi;
    }

    // SUNUCU HAYAT BELIRTISI. Hata DISINDAKI her olay "karsi taraf hala
    // orada" demek: dokum, cevap, ses parcasi, konusma basladi/bitti,
    // arac cagrisi, goAway — hepsi sunucudan geliyor. Bekci bu damgayi
    // cocugun konusma damgasiyla karsilastiriyor.
    if (olay.type != ConversationEventType::Error) {
        g_son_sunucu_us = esp_timer_get_time();
    }

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

    // Sunucuya da soyleniyor: panelde soz kesme kapaliysa Gemini de
    // kendi cevabini kesmesin. Eskiden bu ayar YALNIZCA yerel mikrofon
    // susturmasini etkiliyordu ve sunucu tarafi varsayilanda kaliyordu —
    // yani Pati kendi yankisiyla susuyordu (gemini_live_client.cpp).
    g_ayar.allow_interruption = ayar_soz_kesme();

    // Ses ve VAD de setup mesajinda gidiyor: yenilemede onlar da
    // guncellenmeli, yoksa panelden ses degistirmek hicbir sey
    // yapmiyor gibi gorunur.
    g_ayar.voice = ayar_ses_adi();
    if (ayar_vad_ms() > 0) {
        g_ayar.vad_silence_ms = ayar_vad_ms();
    }
}

// Oturum yeni acildi — bekcinin iki damgasi da tazelensin.
//
// Sifirlanmasaydi yeni oturum eski sessizligin faturasini oderdi:
// acilistan hemen once konusulmus olmasi bekciyi 25 saniye sonra
// bosuna tetiklerdi. Oturumun acilisi "sunucu konustu" sayiliyor,
// cunku setup cevabi zaten sunucudan geliyor.
void bekci_sifirla()
{
    g_son_sunucu_us = esp_timer_get_time();
    g_son_ses_us = 0;
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
    gozler_baglanti_bildir(BaglantiUyarisi::Yok);
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
    bekci_sifirla();
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
    bekci_sifirla();
    ESP_LOGI(ETIKET, "yenilendi (%lld ms boslukla, oturum anahtari korundu)",
             (esp_timer_get_time() - t0) / 1000);
}

// Sunucu, konusuldugu halde susuyor mu? Gerekcesi SESSIZ_SUNUCU_US'un
// yaninda yazili.
//
// Kendisi baglantiyi kapatmiyor, sadece "koptu" diyor; toparlamayi
// asagidaki tek yol yapiyor. Boylece geri cekilme, tur temizligi ve
// kullanim sayaci iki ayri yerde tekrarlanmiyor.
void sessiz_sunucu_bekcisi()
{
    if (!g_calisiyor || g_uykuda || g_yenileniyor || g_koptu) return;

    // Sunucu, cocugun son konusmasindan SONRA bir sey soylediyse saglam.
    // Karsilastirma sart — tek basina "sunucu N saniyedir susuyor" olcutu
    // sessiz odada surekli yanlis alarm verirdi.
    if (g_son_ses_us <= g_son_sunucu_us) return;

    const std::int64_t sessiz_us = esp_timer_get_time() - g_son_ses_us;
    if (sessiz_us < SESSIZ_SUNUCU_US) return;

    ++g_bekci_sayisi;
    ++g_kopma_sayisi;
    g_koptu = true;
    gozler_baglanti_bildir(BaglantiUyarisi::Baglaniyor);
    ESP_LOGW(ETIKET, "cocuk %lld sn once konustu, sunucu o andan beri "
                     "SUSUYOR — oturum olu sayiliyor (hata olayi hic "
                     "gelmedi, bekci %u. kez)",
             sessiz_us / 1000000, static_cast<unsigned>(g_bekci_sayisi));
}

// ---------------------------------------------------------------------------
// Kopan oturumu geri getir
// ---------------------------------------------------------------------------
//
// Yenilemeyle ayni islem (stop -> prompt -> start) ama iki yerde ayriliyor:
//
// 1. YENILEME KONUSMA BOSLUGUNU BEKLER, BU BEKLEYEMEZ. Yenilemenin
//    beklemesinin sebebi turun ortasinda cevabin bolunmesi. Kopmada
//    boyle bir risk yok — bolunecek bir cevap zaten gelmiyor. Beklemek
//    burada "sonsuza kadar bekle" demek olurdu, cunku bekledigi turu
//    baslatacak olan baglanti kopmus durumda.
//
// 2. BASARISIZLIKTA GERI CEKILIYOR. Yenileme "bir dahaki bosta tekrar
//    dener" diyebiliyor cunku tetikleyicisi nadir. Kopma kalici da
//    olabilir (anahtar iptal, kota bitti) ve bosta dongusu saniyede 10
//    kez donuyor.
void kopmayi_toparla()
{
    if (!g_koptu || !g_calisiyor || g_yenileniyor) return;

    // Uyku tam bu sirada basladiysa toparlanacak bir sey yok: oturum
    // zaten bilerek kapali, cocuk konusunca uyandir() aciyor. Olay
    // uretilirken uyku bayragi henuz kalkmamis olabilir, o yuzden
    // ikinci kez burada eliyoruz.
    if (g_uykuda) {
        g_koptu = false;
        return;
    }

    const std::int64_t simdi = esp_timer_get_time();
    if (g_deneme > 0) {
        int bekle = GERI_CEKILME_TABAN_MS << (g_deneme - 1);
        if (bekle > GERI_CEKILME_EN_COK_MS || bekle <= 0) {
            bekle = GERI_CEKILME_EN_COK_MS;
        }
        if ((simdi - g_son_deneme_us) / 1000 < bekle) return;
    }
    g_son_deneme_us = simdi;

    ESP_LOGW(ETIKET, "baglanti koptu (%u. kez) — yeniden baglaniliyor, "
                     "deneme %u",
             static_cast<unsigned>(g_kopma_sayisi),
             static_cast<unsigned>(g_deneme + 1));

    // Bayrak ONCE: mikrofon gorevi bu pencerede push_audio() cagirmasin.
    // Yenilemedekiyle ayni gerekce — kapali sokete yazmak Python'da
    // programi cokertmisti.
    g_yenileniyor = true;

    // 🔴 YARIM KALAN TURU KAPAT. Kopma turun ORTASINDA olduysa
    // `acik_tur()` sonsuza kadar acik kalir; yenileme yolu ona bakip
    // "tur suruyor, sonra bakarim" diyor ve BIR DAHA CALISMIYOR.
    // Ayni sekilde `g_konusuyor` konusma ortasinda kopunca basili
    // kaliyor ve yanki korumasi mikrofonu susturuyor: robot hem
    // konusmuyor hem duymuyor olurdu.
    tur_kapat();
    g_konusuyor = false;

    // OLU OTURUM DAKIKA YAKMASIN. Olculdu: kopmadan sonra sayac
    // isliyordu (14,0 -> 17,6 dk). Basarili olursa asagida devam ediyor.
    kullanim_duraklat();

    g_istemci->stop();
    prompt_kur();
    const auto sonuc = g_istemci->start(g_ayar);

    g_yenileniyor = false;

    if (!sonuc.has_value()) {
        ++g_deneme;
        ESP_LOGE(ETIKET, "yeniden baglanilamadi — tekrar denenecek");
        // SEBEBINI SOR. WebSocket el sikismasi basarisiz oldugunda HTTP
        // kodu gorunmuyor: "anahtar iptal edilmis" ile "wifi koptu" ayni
        // hataya benziyor. Anahtar katmani kucuk bir REST istegiyle
        // ayirt ediyor ve panel dogru olani yaziyor. Kendini dakikada
        // bire sinirliyor. (uyandir() ile ayni gerekce.)
        anahtar_baglanti_hatasi();
        // Gozler YALAN SOYLEMESIN. "dinliyor"da birakmak cocuga Pati'nin
        // bekledigini anlatir; oysa duymuyor. Uykulu bakis dogruyu
        // soyluyor ve baglanti gelince gozler_dinliyor()'a donuyor.
        gozler_durum("uykulu");
        return;
    }

    g_koptu = false;
    g_deneme = 0;
    // Setup yeniden gitti: bekleyen goAway ve ayar degisikligi de bu
    // mesajla kapandi, ikisini de tekrar tetiklemeye gerek yok.
    g_goaway = false;
    ayar_yenileme_temizle();
    anahtar_kod_bildir(200);
    kullanim_devam();
    bekci_sifirla();
    gozler_dinliyor();
    ESP_LOGI(ETIKET, "yeniden baglandi (%lld ms)",
             (esp_timer_get_time() - simdi) / 1000);
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
        // Gemini Speaking olayını ilk ses paketinden ÖNCE gönderir.
        // Bayrağı önce yükseltmek, paket yolundaki göz geçişini atlatıyordu.
        if (olay.state == ConversationState::Speaking && !g_konusuyor) {
            gozler_konusuyor();
        }
        g_konusuyor = (olay.state == ConversationState::Speaking);
        ESP_LOGD(ETIKET, "durum: %d", static_cast<int>(olay.state));
        break;

    case ConversationEventType::SpeechStarted:
        gozler_baglanti_bildir(BaglantiUyarisi::Yok);
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
        gozler_baglanti_bildir(BaglantiUyarisi::CevapBekliyor);
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
        gozler_baglanti_bildir(BaglantiUyarisi::Yok);
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
        gozler_baglanti_bildir(BaglantiUyarisi::Yok);
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
        // Toparlanma bayragi burada DEGIL, olay_geldi()'de kalkiyor;
        // gerekcesi orada yazili. Burasi sadece gunluge yaziyor.
        ESP_LOGE(ETIKET, "istemci hatasi: %s%s", olay.text.c_str(),
                 g_koptu ? "  -> yeniden baglanilacak" : "");
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
            // Kuyruk bos kaldiginda bakiliyor ve dogru yer burasi:
            // olaylar akiyorsa sunucu zaten konusuyor demektir.
            sessiz_sunucu_bekcisi();

            // KOPMA ONCE. Yenileme olu bir soket uzerinde anlamsiz, ve
            // cocuk cevap bekliyor — geciken her saniye "Pati bozuldu"
            // demek. Kendi geri cekilmesi var, bosta dongusunu yormuyor.
            kopmayi_toparla();

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

// Konusma bittikten sonra kac us daha GERCEK ses gonderilsin.
//
// 400 ms. Gerekce mik_gorevi'ndeki sessizlik bastirma blogunda: cumle
// sonu RMS esigin altina duserken kelimenin kuyrugu kirpilmasin.
// Sunucunun tur kapatma esigi 500 ms sessizlik, yani hangover ondan
// KISA olmali — yoksa sessizlik hic olusmaz ve bastirma isini yapmaz.
constexpr std::int64_t SESSIZLIK_HANGOVER_US = 400000;

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
    // Konusmanin en son ne zaman duyuldugu — sessizlik bastirma
    // penceresi bunu kullaniyor (bkz. SESSIZLIK_HANGOVER_US).
    std::int64_t son_sesli_us = 0;
    // Cocuk konustu ve henuz sira sunucuya verilmedi mi.
    bool tur_kapatilacak = false;
    // Kesintisiz ses ne kadar surdu (bkz. UYANMA_SESLI_US).
    std::int64_t sesli_us = 0;
    // Ayni olcu, ama UYANIKKEN — sessiz sunucu bekcisi icin. Ayri sayac
    // tutuluyor cunku uykudakinin sifirlanma anlari farkli (uyandirinca
    // sifirlaniyor); tek sayaci paylassalardi biri digerinin altini
    // oyardi.
    std::int64_t bekci_sesli_us = 0;

    while (g_calisiyor) {
        const std::size_t n = mikrofon_oku(parca, 100);

        // 🔴 IKI AYRI ARIZA, TEK BELIRTI.
        //
        // "Mikrofon sessiz" iki bambaska seyden gelebilir:
        //   n == 0        -> okuma HIC veri dondurmedi (zaman asimi)
        //   n > 0, hep 0  -> veri geldi ama icinde ses yok
        //
        // Ilki I2S/DMA sorunu, ikincisi kodek/ADC sorunu. Ayirmadan
        // bakmak yanlis yerde saatler harcamak demek — 01.09.2026'da
        // tam bu oldu.
        ++g_mik_okuma;
        if (n == 0) {
            ++g_mik_bos;
            continue;
        }
        // Tepe genlik — gonderim kosullarindan ONCE, cunku soru
        // "mikrofon duyuyor mu", "gonderiyor muyuz" degil.
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint32_t g = static_cast<std::uint32_t>(
                parca[i] < 0 ? -static_cast<std::int32_t>(parca[i]) : parca[i]);
            if (g > g_mik_tepe) g_mik_tepe = g;
        }

        // YANKI PENCERESI. Robot konustugu her an damgalaniyor; asagida
        // "en son ne zaman konusuyordu" diye sorulacak. Burada, her
        // continue'dan ONCE duruyor ki pencere hep guncel olsun.
        const std::int64_t simdi = esp_timer_get_time();
        if (g_konusuyor) g_konusma_us = simdi;

        // ---- SESSIZLIK BASTIRMA -----------------------------------
        //
        // 🔴 05.09.2026'da bulundu: Pati Gemini'ye ARALIKSIZ ses
        // gonderiyordu — sessizken bile, ortam gurultusu dahil.
        //
        // Sunucunun turu kapatma kurali "500 ms sessizlik". Gurultu
        // kesintisiz akinca o sessizlik HIC OLUSMUYOR: sunucu cocugun
        // konusmayi surdurdugunu sanip cevabi hic uretmiyor. Disaridan
        // gorunusu tam bir "duyuyor ama cevap vermiyor" — girdi
        // transkripsiyonu VAD'den bagimsiz calistigi icin cocugun
        // soyledigi ekrana/loga dogru dusuyor, sadece cevap gelmiyor.
        //
        // Logda gorulen: mikrofon tepesi 235 ile 4938 arasinda surekli
        // dalgalaniyor, yani hicbir an gercek sessizlik yok.
        //
        // Cozum sesi KESMEK degil SUSTURMAK: parca yine gonderiliyor
        // (akis kesilmiyor, sunucu "baglanti oldu" sanmiyor) ama icerigi
        // sifirlaniyor. Boylece sunucu gercek sessizlik goruyor.
        //
        // HANGOVER: konusma bittikten hemen sonra kesmiyoruz. Cumlenin
        // son hecesi RMS esigin altina dusebiliyor ve orayi susturmak
        // kelimeyi kirpardi. Konusmadan sonraki 400 ms hala gercek ses.
        {
            const bool sesli = konusma_var_mi(parca, n);
            if (sesli) {
                son_sesli_us = simdi;
                tur_kapatilacak = true;
            } else if (son_sesli_us == 0
                       || (simdi - son_sesli_us) > SESSIZLIK_HANGOVER_US) {
                // Sessizlik: parcayi sifirla ama GONDER.
                parca.fill(0);

                // ---- TURU BIZ KAPATIYORUZ ---------------------------
                //
                // 🔴 SUNUCU VAD'I BU GOVDEDE TETIKLENEMIYOR. Hoparlorle
                // mikrofon 5 cm arayla; ortam sesi hic kesilmiyor ve
                // sunucunun bekledigi 500 ms sessizlik HIC olusmuyor.
                // Sonucu: tur kapanmiyor, cevap hic gelmiyor, ama girdi
                // transkripsiyonu calismaya devam ettigi icin disaridan
                // "duyuyor ama konusmuyor" gibi gorunuyor.
                //
                // Cocuk sustu ve hangover doldu: sirayi sunucuya BIR KEZ
                // veriyoruz. Bayrak, sessizlik boyunca her 20 ms'de bir
                // tekrar gondermeyi engelliyor — tekrari sunucu "bos tur"
                // diye okuyabilir.
                //
                // Pati konusurken cagirmiyoruz: o sirada sira zaten
                // sunucuda ve akisi bolmek cevabi kirpardi.
                if (tur_kapatilacak && !g_konusuyor && g_istemci != nullptr) {
                    tur_kapatilacak = false;
                    (void)g_istemci->commit_audio();
                }
            }
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
                            / PATI_GEMINI_GIRIS_HZ;
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

        // BEKCI ICIN DAMGA: "cocuk konustu".
        //
        // Gerekcesi SESSIZ_SUNUCU_US ve YANKI_KUYRUGU_US'un yaninda.
        // Uc kosul birden araniyor:
        //
        //   1. Robot konusmuyor ve susali yanki kuyrugu gecmis. Yoksa
        //      robotun kendi sesi cocuk sanilir ve bekci her cevaptan
        //      sonra bosuna baglanirdi.
        //
        //   2. 🔴 SES SUREKLI — tek bir 20 ms'lik parca YETMIYOR.
        //      Uyandirmadaki ayni gerekce (bkz. UYANMA_SESLI_US): yerel
        //      esik bilerek comert, cunku yanlis negatifin bedeli cocugu
        //      duymamak. Bunun karsiligi oda gurultusunun, kapi
        //      carpmasinin, oksurugun de zaman zaman "konusma" demesi.
        //      Tek parca yetseydi bekci sessiz odada tetiklenir ve
        //      sohbeti DURUP DURURKEN keserdi. Ayni esigi kullaniyoruz
        //      cunku ayni soruyu soruyor: bu ses gercekten konusma mu?
        //
        // Uykuda BURAYA GELINMIYOR (yukarida continue var) ve dogrusu
        // bu: uykuda oturum zaten bilerek kapali, sunucunun susmasi
        // beklenen sey.
        if (!g_konusuyor && (simdi - g_konusma_us) > YANKI_KUYRUGU_US
            && konusma_var_mi(parca, n)) {
            bekci_sesli_us += static_cast<std::int64_t>(n) * 1000000
                              / PATI_GEMINI_GIRIS_HZ;
            if (bekci_sesli_us >= UYANMA_SESLI_US) {
                g_son_ses_us = simdi;
            }
        } else {
            // Araya sessizlik girdi: zincir kirildi.
            bekci_sesli_us = 0;
        }

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
    g_ayar.input_sample_rate_hz = PATI_GEMINI_GIRIS_HZ;
    g_ayar.output_sample_rate_hz = PATI_GEMINI_CIKIS_HZ;
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
    // 🔴 3072'DEN 6144'E CIKARILDI — GERCEK KARTTA COKTU.
    //
    // 01.09.2026, seri gunlukten:
    //     ***ERROR*** A stack overflow in task pati_mik has been detected.
    //     Backtrace: ... |<-CORRUPTED
    //     Rebooting...
    //
    // Yukaridaki yorum "mikrofon gorevi sadece okuyup gonderiyor" diyordu
    // ve YAZILDIGI ZAMAN dogruydu. Sonra uyandirma bu goreve baglandi:
    // cocuk konusunca `uyandir()` BURADAN cagriliyor ve o da
    // `prompt_kur()` ile 5400 karakterlik sistem promptunu kuruyor,
    // ustune `g_istemci->start()` setup mesajini hazirlayip gonderiyor.
    // Bunlar 3 KB'lik bir yigit icin fazla.
    //
    // BELIRTISI YANILTICI: cokme uyanma anindaydi, yani cocuk konustugu
    // an. Disaridan "Pati'ye seslenince kapaniyor" gibi gorunuyor ve
    // sebep guc dalgalanmasinda ya da mikrofonda aranir. Bir kere de
    // brownout'la ayni oturumda oldu ve ikisi birbirine karisti.
    //
    // NEDEN GORESI BUYUTULDU, IS TASINMADI: uyandirmayi ses gorevine
    // devretmek olcuyu 100 ms'lik kuyruk beklemesi kadar geciktirirdi ve
    // uyanma zaten 617 ms. Cocugun bekledigi yerde gereksiz gecikme.
    xTaskCreate(mik_gorevi, "pati_mik", 6144, nullptr, 4, nullptr);

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

std::uint32_t sohbet_kopma_sayisi()
{
    // SIFIRLANMIYOR: "son aralikta kac kez koptu" degil, "acilistan beri
    // kac kez koptu" sorusunun cevabi. Mikrofon tepesinin tersine burada
    // kumulatif olan dogru — bir kopma olduysa bunun izi kalmali.
    return g_kopma_sayisi;
}

std::uint32_t sohbet_mik_tepe()
{
    // Okuyup SIFIRLIYOR: rapor "son araliktaki en yuksek genlik" demek.
    // Kumulatif olsaydi bir kez bagiran biri sonsuza kadar "mikrofon
    // calisiyor" gosterirdi.
    const std::uint32_t t = g_mik_tepe;
    g_mik_tepe = 0;
    return t;
}

void sohbet_mik_okuma(std::uint32_t& okuma, std::uint32_t& bos)
{
    okuma = g_mik_okuma;
    bos = g_mik_bos;
    g_mik_okuma = 0;
    g_mik_bos = 0;
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
