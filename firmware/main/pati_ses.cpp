#include "pati_ses.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

#include <driver/i2s_std.h>
#include <esp_log.h>

#include "es8311_codec.h"
#include "esp_codec_dev_defaults.h"
#include "pati_guc.hpp"
#include "pati_ornekleyici.hpp"
#include "pati_pinler.h"

namespace pati {
namespace {

constexpr const char* ETIKET = "pati.ses";

// TEK I2S KANAL CIFTI, TEK SAAT. Onceki kartta iki ayri yonga ve iki
// ayri kanal vardi; burada ikisi de ES8311'in icinde ve ayni hatti
// paylasiyorlar. Gerekce pati_pinler.h'de (PATI_SES_HZ).
i2s_chan_handle_t g_tx = nullptr;
i2s_chan_handle_t g_rx = nullptr;

const audio_codec_if_t* g_kodek = nullptr;
bool g_hazir = false;

float g_seviye = SES_SEVIYESI_BASLANGIC;

// Calma hizi carpani. 1.30 -> Pati'nin afacan sesi.
//
// Onceki kartta bu bir I2S SAAT ayariydi; burada saat sabit (48 kHz,
// mikrofonla paylasildigi icin degistirilemez) ve carpan ORNEK
// uzerinde calisiyor. Ayrintili gerekce hoparlor_hiz_ayarla'da.
float g_hiz = 1.30f;

// Yeniden ornekleyici. Matematigi pati_ornekleyici.hpp'de duruyor
// cunku KONAKTA SINANIYOR — test/ses_karsilastir.cpp ayni basligi ice
// alip gercek kodu olcuyor.
YenidenOrnekleyici g_ornek;

// Mikrofonun 48 kHz ham verisi. 320 ornek (20 ms) uretmek icin 960 ham
// ornek gerekiyor — birebir uc kati.
//
// Neden static: bu fonksiyon saniyede 50 kez cagriliyor, her seferinde
// yigitta 2 KB ayirmak istemiyoruz. Tek gorevden cagrildigi icin
// paylasimi guvenli.
std::array<std::int16_t, PATI_OKUMA_ORNEK * 3> g_ham{};

// ---------------------------------------------------------------------------
// I2S ve kodek kurulumu — tek sefer, ilk cagirana
// ---------------------------------------------------------------------------

// DMA tamponu — kac tanimlayici, her birinde kac cerceve.
//
// 🔴 BU SAYILAR OLCUMLE SECILDI, TAHMINLE DEGIL.
//
// 23.08.2026'da onceki kartta olculdu: Gemini sesi 200-280 ms'lik
// parcalar halinde gonderiyor. O kartta tampon 200 ms tutuyordu, yani
// PAY SIFIRDI ve Pati konusurken kisa bosluklar duyuluyordu. Tampon
// buyutulunce bosluklar bitti; calisan yapilandirma 525 ms'lik CALMA
// suresine denk geliyordu.
//
// 🔴 AMA BURADA 525 ms'YI KARSILAYAMIYORUZ VE BU BILINCLI BIR TAVIZ.
//
// Onceki kartta tampon yalnizca CIKIS icindi: mikrofon ayri bir yongada,
// ayri bir kanaldaydi. Burada tam dupleks tek kanal cifti kullaniliyor
// ve `i2s_new_channel` ikisini TEK cagriyla aciyor — TX ile RX ayni
// yapilandirmayi paylasmak zorunda, ayri boyut verilemiyor. Yani
// istenen her milisaniye IKI KEZ odeniyor.
//
// Ustune DMA tamponu IC RAM'de olmak zorunda; PSRAM'den DMA yapilamiyor.
// Olculdu (idf.py size, 23.08.2026): duragan kullanimdan sonra iç
// RAM'de 184 KB kaliyor ve o 184 KB'yi wifi, TLS, websocket, gorev
// yiginlari ve ekranin serit tamponlari da paylasiyor.
//
//   525 ms istesek : 24 x 1024 x 2 bayt x 2 yon = ~98 KB
//   secilen        : 16 x 1024 x 2 bayt x 2 yon = ~65 KB  (341 ms)
//
// NEDEN KUCUK OLANI: iki basarisizligin bedeli esit degil.
//
//   Tampon buyuk olursa ayirma BASARILI olur ama arkasindan gelen
//   wifi/TLS bellek bulamaz. Pati hic baglanamaz ve sebebi "ses
//   tamponu" oldugu akla gelmez.
//
//   Tampon kucuk olursa en kotu ihtimalle konusma sirasinda kisa bir
//   takilma duyulur — tek sabitle duzeltilir ve nereye bakilacagi
//   bellidir.
//
// 341 ms hala olculen en uzun parcanin (280 ms kaynak = 215 ms calma)
// bir buçuk kati. Ayrica bu kartta rakip is de azaldi: ekran 57.600
// yerine 32.400 piksel ve parlama katmani inceldi, yani ses gorevi
// daha az aç kaliyor.
//
// SES TAKILIRSA ILK YAPILACAK: DMA_TANIM'i 20'ye, sonra 24'e cikar.
// Her adim ~8 KB ic RAM yiyor. Acilista "ic RAM yetmedi" uyarisi
// cikarsa geri dusulmustur; o zaman deger kalici olarak dusurulmeli.
//
// Tanimlayici basina 1024 x 2 = 2048 bayt; IDF'in siniri 4092.
constexpr int DMA_TANIM = 16;
constexpr int DMA_CERCEVE = 1024;

esp_err_t i2s_kur(int tanim, int cerceve)
{
    i2s_chan_config_t kanal =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    kanal.dma_desc_num = tanim;
    kanal.dma_frame_num = cerceve;
    kanal.auto_clear = true;  // bosalinca sessizlik bas, eski veriyi tekrarlama

    // TEK CAGRI, IKI TUTAMAK = TAM DUPLEKS. Ayri cagrilarla acilsalardi
    // ayni portu paylasamazlardi.
    const esp_err_t hata = i2s_new_channel(&kanal, &g_tx, &g_rx);
    if (hata != ESP_OK) {
        g_tx = nullptr;
        g_rx = nullptr;
    }
    return hata;
}

esp_err_t ses_kur()
{
    if (g_hazir) return ESP_OK;

    // ---- I2S kanallari ---------------------------------------------------
    esp_err_t hata = i2s_kur(DMA_TANIM, DMA_CERCEVE);
    int tanim = DMA_TANIM;

    // IC RAM yetmezse kademe kademe kucul. Sessiz kalmaktansa daha kucuk
    // tamponla calismak iyidir: ses takilabilir ama Pati konusur.
    //
    // 🔴 KADEMELER 12'DE DURUYOR VE BU ONEMLI. 12 x 1024 = 256 ms, yani
    // olculen en uzun parcanin calma suresinin (215 ms) hala ustunde.
    // Daha asagisi — mesela yariya inmek, 8 x 1024 = 170 ms — o esigin
    // ALTINA duserdi ve takilmayi azaltmak yerine GARANTI ederdi.
    // "Calisan ama kotu" ile "calismiyor" arasinda secim yapiyorsak
    // ucuncu bir secenek olan "calisiyor gorunup surekli takilan"
    // en kotusu.
    //
    // Buraya dusuluyorsa DMA_TANIM kalici olarak indirilmeli: yer,
    // calisma sirasinda baska bir seyi aclikta birakiyor demektir.
    for (int daha_az : {14, 12}) {
        if (hata != ESP_ERR_NO_MEM) break;
        ESP_LOGW(ETIKET, "DMA icin ic RAM yetmedi (%d x %d) — %d deneniyor",
                 tanim, DMA_CERCEVE, daha_az);
        tanim = daha_az;
        hata = i2s_kur(tanim, DMA_CERCEVE);
    }
    if (hata == ESP_OK && tanim != DMA_TANIM) {
        ESP_LOGW(ETIKET, "ses tamponu %d ms (hedef %d ms) — takilma olabilir",
                 tanim * DMA_CERCEVE * 1000 / PATI_SES_HZ,
                 DMA_TANIM * DMA_CERCEVE * 1000 / PATI_SES_HZ);
    }
    if (hata != ESP_OK) {
        ESP_LOGE(ETIKET, "I2S kanallari acilamadi: %s", esp_err_to_name(hata));
        return hata;
    }

    // MONO. ES8311'in kendisi tek kanalli ve set_fs kanal sayisina hic
    // bakmiyor — yalnizca bit derinligi ve orneklem hizi onemli.
    // Stereo kursaydik ayni sureyi tamponlamak IKI KATI ic RAM ister,
    // ustune her ornekte serpistirme/ayirma isi eklenirdi.
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(PATI_SES_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = PATI_SES_MCLK,
            .bclk = PATI_SES_BCLK,
            .ws   = PATI_SES_LRCK,
            .dout = PATI_SES_DOUT,
            .din  = PATI_SES_DIN,
            .invert_flags = {false, false, false},
        },
    };
    // MCLK'i ES8311'e BIZ veriyoruz (kodek kole). 256 x 48 kHz =
    // 12,288 MHz — standart bir deger.
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    hata = i2s_channel_init_std_mode(g_tx, &std_cfg);
    if (hata != ESP_OK) {
        ESP_LOGE(ETIKET, "I2S cikis kurulamadi: %s", esp_err_to_name(hata));
        return hata;
    }
    // ---- GIRIS YUVASI — 01.09.2026'da olculerek bulunacak ------------
    //
    // IDF'in mono varsayilani SOL yuva (I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG
    // -> slot_mask = I2S_STD_SLOT_LEFT). Gercek kartta o yuvadan TAM SIFIR
    // geliyor: gurultu bile yok, yani hatta hic veri dusmuyor.
    //
    // Elenenler (hepsi bakildi, hicbiri degildi):
    //   - kodek saati: {12288000, 48000} surucunun tablosunda VAR ve
    //     "Unable to configure sample rate" hatasi cikmiyor
    //   - ADC seviyesi: enable(true) -> es8311_start -> ADC_REG17 = 0xBF
    //   - no_dac_ref: false'a alindi (surucunun test edilen yolu), fark yok
    //
    // Geriye ES8311'in ADC verisini HANGI yuvaya koydugu kaliyor.
    i2s_std_config_t giris_cfg = std_cfg;
    giris_cfg.slot_cfg.slot_mask = PATI_SES_GIRIS_YUVA;

    hata = i2s_channel_init_std_mode(g_rx, &giris_cfg);
    if (hata != ESP_OK) {
        ESP_LOGE(ETIKET, "I2S giris kurulamadi: %s", esp_err_to_name(hata));
        return hata;
    }

    // ---- ES8311 ----------------------------------------------------------
    //
    // I2C hatti pati_guc.cpp'de aciliyor ve UC aygit paylasiyor. Kodek
    // kendi hattini acmiyor, hazir hatti aliyor.
    i2c_master_bus_handle_t yol = i2c_yolu();
    if (yol == nullptr) {
        ESP_LOGE(ETIKET, "I2C hatti yok — guc_baslat() cagrilmamis");
        return ESP_ERR_INVALID_STATE;
    }

    audio_codec_i2c_cfg_t i2c = {};
    i2c.port = I2C_NUM_0;
    // 🔴 SEKIZ BITLIK ADRES. Bilesen icerde `addr >> 1` yapiyor
    // (platform/audio_codec_ctrl_i2c.c). Buraya 0x18 yazsaydik kodek
    // 0x0C'de aranir, kimse cevap vermez ve ses SESSIZCE hic gelmezdi.
    i2c.addr = ES8311_CODEC_DEFAULT_ADDR;  // 0x30 = 0x18 << 1
    i2c.bus_handle = yol;
    i2c.clock_speed_hz = PATI_I2C_HZ;

    const audio_codec_ctrl_if_t* denetim = audio_codec_new_i2c_ctrl(&i2c);
    if (denetim == nullptr) {
        ESP_LOGE(ETIKET, "kodek I2C arayuzu kurulamadi");
        return ESP_FAIL;
    }

    es8311_codec_cfg_t kodek = {};
    kodek.ctrl_if = denetim;
    kodek.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;  // mikrofon + hoparlor
    kodek.master_mode = false;  // saati ESP32 veriyor, kodek kole
    kodek.use_mclk = true;      // MCLK ayri telden (G18)
    kodek.mclk_div = PATI_SES_MCLK_KAT;
    // ANALOG MIKROFON — 01.09.2026'da gercek kartta dogrulandi.
    //
    // MEMS mikrofon kodegin analog ADC girisine bagli, PDM degil.
    // M5Stack'in urun sayfasi yalnizca "MEMS microphone, SNR 65 dB"
    // diyor; hangisi oldugu yazmiyor. Teshis sirasinda bir ara
    // `true` denendi (PDM) ve mikrofon yine sessiz kaldi — cunku asil
    // sebep baskaydi, DIN/DOUT pinleri terstiydi (bkz. pati_pinler.h).
    //
    // Pinler duzeltilip burasi `false` kalinca mikrofon calisti:
    // konusurken tepe genlik 11.000-32.000 arasi.
    kodek.digital_mic = false;
    // Amfiyi ESP32'nin bir pini degil M5PM1 aciyor (pati_guc.cpp), yani
    // burada verilecek bir pin yok.
    kodek.pa_pin = -1;
    // 🔴 no_dac_ref = false — VE BU BILINCLI, "varsayilan boyle" DEGIL.
    //
    // Once true yapilmisti: "sag kanala DAC cikisini koymak yanki
    // iptali icin; biz yarim dupleks kullaniyoruz ve mono okuyoruz,
    // gereksiz is." Akil yurutmesi mantikliydi ve YANLISTI.
    //
    // 01.09.2026, gercek kartta: mikrofon TAM SIFIR okuyor. Gurultu
    // bile yok. Anahtar gecerli, oturum aciliyor, sure sayiliyor ama
    // Gemini'ye sessizlik gidiyordu, dolayisiyla Pati hic cevap
    // vermiyordu.
    //
    // Sebep surucude yaziyor (es8311.c, es8311_codec_open):
    //
    //     if (no_dac_ref == false) {
    //         // set internal reference signal (ADCL + DACR)
    //         write_reg(ES8311_GPIO_REG44, 0x58);
    //     } else {
    //         write_reg(ES8311_GPIO_REG44, 0x08);
    //     }
    //
    // Yorumdaki "ADCL" onemli: 0x58 ADC'yi SOL yuvaya yonlendiriyor.
    // Biz mono okuyoruz ve IDF'in mono varsayilani SOL yuva
    // (I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG -> slot_mask =
    // I2S_STD_SLOT_LEFT). 0x08 yazilinca ADC o yuvaya gelmiyor ve
    // okudugumuz her ornek sifir cikiyor.
    //
    // DERS: bu surucunun varsayilani Espressif'in ES8311'li kendi
    // kartlarinda surekli test edilen yol. Test edilmemis bir
    // optimizasyon icin ondan sapmak, kazandirdigindan cok
    // kaybettiriyor.
    kodek.no_dac_ref = false;

    g_kodek = es8311_codec_new(&kodek);
    if (g_kodek == nullptr) {
        ESP_LOGE(ETIKET, "ES8311 kurulamadi — kart StickS3 mi?");
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t bicim = {};
    bicim.sample_rate = PATI_SES_HZ;
    bicim.channel = 1;
    bicim.bits_per_sample = 16;
    if (g_kodek->set_fs(g_kodek, &bicim) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(ETIKET, "kodek %d Hz'e ayarlanamadi", PATI_SES_HZ);
        return ESP_FAIL;
    }

    // Kodegin KENDI analog kazanci. Sayisal seviye (g_seviye) ve
    // sinirlayici bizde kaliyor; burasi yalnizca cikis yolunun temel
    // kazanci.
    //
    // 0 dB secildi: bozulmayi sayisal tarafta yonetiyoruz ve orada
    // sinirlayici var. Kodekten kazanc almak o korumanin ONUNE gecerdi.
    g_kodek->set_vol(g_kodek, 0.0f);

    // Mikrofon kazanci 30 dB. MEMS mikrofonun sinyali zayif; onceki
    // karttaki INMP441'de bu kazanc yonganin icinde sabitti, burada
    // ayarlanabiliyor. Cocuk robotun bir kol boyu uzaginda konusuyor.
    g_kodek->set_mic_gain(g_kodek, 30.0f);

    if (g_kodek->enable(g_kodek, true) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(ETIKET, "kodek etkinlestirilemedi");
        return ESP_FAIL;
    }


    // Kanallari kodekten SONRA aciyoruz: saat, kodek dinlemeye hazir
    // olmadan akmaya baslamasin.
    hata = i2s_channel_enable(g_tx);
    if (hata != ESP_OK) {
        ESP_LOGE(ETIKET, "I2S cikis acilamadi: %s", esp_err_to_name(hata));
        return hata;
    }
    hata = i2s_channel_enable(g_rx);
    if (hata != ESP_OK) {
        ESP_LOGE(ETIKET, "I2S giris acilamadi: %s", esp_err_to_name(hata));
        return hata;
    }

    g_hazir = true;
    ESP_LOGI(ETIKET,
             "ses hazir — ES8311 %d Hz mono, mik %d Hz, hoparlor %d Hz x %.2f",
             PATI_SES_HZ, PATI_GEMINI_GIRIS_HZ, PATI_GEMINI_CIKIS_HZ,
             static_cast<double>(g_hiz));
    // Tampon suresi ILK ACILISTA gorulmeli: ses takilirsa bakilacak ilk
    // sayi bu ve gunlukte yoksa tahmin edilmesi gerekir.
    ESP_LOGI(ETIKET, "  DMA tamponu %d x %d = %d ms, ~%d KB (iki yon)",
             tanim, DMA_CERCEVE,
             tanim * DMA_CERCEVE * 1000 / PATI_SES_HZ,
             tanim * DMA_CERCEVE * 2 * 2 / 1024);
    return ESP_OK;
}

}  // namespace

// ---------------------------------------------------------------------------
// Mikrofon
// ---------------------------------------------------------------------------

bool ses_hazir() { return g_hazir; }

esp_err_t mikrofon_baslat() { return ses_kur(); }

size_t mikrofon_oku(std::span<std::int16_t> hedef, uint32_t timeout_ms)
{
    if (!g_hazir || hedef.empty()) {
        return 0;
    }

    // Kac 16 kHz ornek istiyoruz, kac 48 kHz ornek okumamiz gerekiyor.
    const size_t istenen = std::min(hedef.size(), g_ham.size() / 3);
    size_t okunan_bayt = 0;

    const esp_err_t hata = i2s_channel_read(
        g_rx, g_ham.data(), istenen * 3 * sizeof(std::int16_t),
        &okunan_bayt, timeout_ms);

    if (hata != ESP_OK && hata != ESP_ERR_TIMEOUT) {
        ESP_LOGW(ETIKET, "mikrofon okuma: %s", esp_err_to_name(hata));
        return 0;
    }

    // ---- 48 kHz -> 16 kHz ------------------------------------------------
    //
    // Oran TAM UC. Ucer ornegin ORTALAMASI aliniyor, "her ucuncuyu al"
    // degil.
    //
    // Fark onemli ve duyulur: dogrudan secmek SEYRELTME olur ve 8 kHz
    // ustundeki her sey bandin icine KATLANIR — tiz sesler cizirtiya
    // doner. Ortalama almak, ilk sifiri tam 16 kHz'de olan kucuk bir
    // alcak geciren suzgec demek; katlanacak enerjinin buyuk kismini
    // once bastiriyor, sonra seyreltiyor.
    //
    // Duzgun bir FIR suzgec daha temiz olurdu ama konusma icin bu
    // yeterli ve ornek basina iki toplama bir bolme tutuyor.
    const size_t ham_ornek = okunan_bayt / sizeof(std::int16_t);
    const size_t ornek = ham_ornek / 3;

    for (size_t i = 0; i < ornek; ++i) {
        const std::int32_t toplam = static_cast<std::int32_t>(g_ham[i * 3]) +
                                    g_ham[i * 3 + 1] + g_ham[i * 3 + 2];
        hedef[i] = static_cast<std::int16_t>(toplam / 3);
    }
    return ornek;
}

// ---------------------------------------------------------------------------
// Hoparlor
// ---------------------------------------------------------------------------

esp_err_t hoparlor_baslat() { return ses_kur(); }

esp_err_t hoparlor_hiz_ayarla(float carpan)
{
    // 🔴 ARTIK SAATE DOKUNMUYOR — ve bu bir kazanc.
    //
    // Onceki kartta bu fonksiyon I2S kanalini kapatip saati yeniden
    // kuruyordu, yani panelden hiz degistirmek CALAN SESI KESIYORDU.
    // Burada saat mikrofonla paylasildigi icin zaten degistirilemez:
    // hizi degistirmek 48 kHz'i degistirmek olurdu ve o da mikrofonun
    // orneklem hizini bozardi.
    //
    // Bu yuzden carpan ORNEK uzerine tasindi. Yan etkisi: degisiklik
    // aninda ve sessizce uygulaniyor, hicbir sey kesilmiyor.
    g_hiz = std::clamp(carpan, 0.80f, 1.60f);
    ESP_LOGI(ETIKET, "calma hizi %.2fx (adim %.3f)",
             static_cast<double>(g_hiz),
             static_cast<double>(g_hiz * PATI_GEMINI_CIKIS_HZ / PATI_SES_HZ));
    return ESP_OK;
}

size_t hoparlor_yaz(std::span<const std::int16_t> kaynak, uint32_t timeout_ms)
{
    if (!g_hazir || kaynak.empty()) {
        return 0;
    }

    // ---- ADIM: kaynakta ornek basina ne kadar ilerliyoruz ----------------
    //
    //   adim = 24000 x carpan / 48000 = carpan / 2
    //
    // 1.30'da 0,65. ADIM HER ZAMAN 1'DEN KUCUK (carpan tavani 1.60,
    // yani adim tavani 0,80) ve bu tesaduf degil, secimin sebebi:
    // adim 1'in altindayken ARA DEGER uretiyoruz, yani hicbir bilgi
    // atilmiyor ve katlanma olusmuyor. Adim 1'in ustunde olsaydi
    // seyreltme yapardik ve Pati'nin sesi cizirdardi.
    const float adim = g_hiz * static_cast<float>(PATI_GEMINI_CIKIS_HZ) /
                       static_cast<float>(PATI_SES_HZ);

    // Cikis bloklar halinde yaziliyor, tek seferde degil: bir parca
    // 2,5 kata kadar uzayabiliyor (en yavas hizda) ve 33 KB'lik bir
    // yigit tamponu kabul edilemez.
    std::array<std::int16_t, 512> cikti{};

    return g_ornek.isle(
        kaynak, adim, g_seviye, cikti,
        [&](std::span<const std::int16_t> blok) -> bool {
            size_t bayt = 0;
            const esp_err_t hata = i2s_channel_write(
                g_tx, blok.data(), blok.size() * sizeof(std::int16_t),
                &bayt, timeout_ms);
            if (hata != ESP_OK && hata != ESP_ERR_TIMEOUT) {
                ESP_LOGW(ETIKET, "hoparlor yazma: %s", esp_err_to_name(hata));
                return false;
            }
            // Kismi yazma: kuyruk dolu ve bekleme suresi doldu. Kalani
            // zorlamak gecikmeyi buyutur; birakmak daha dogru — ama
            // artik SESSIZCE degil, donus degerinde gorunerek.
            return bayt == blok.size() * sizeof(std::int16_t);
        });
}

esp_err_t hoparlor_temizle()
{
    if (!g_hazir) {
        return ESP_OK;
    }
    // Kanali kapatip acmak DMA kuyrugunu bosaltiyor. Barge-in'de
    // beklenen davranis: robot ANINDA susuyor.
    esp_err_t hata = i2s_channel_disable(g_tx);
    if (hata == ESP_OK) {
        hata = i2s_channel_enable(g_tx);
    }

    // Yeniden ornekleyici de sifirlanmali. Atilan sesin fazini
    // saklamak, sonraki cumleyi yarim bir ornekten baslatmak olurdu.
    g_ornek.sifirla();
    return hata;
}

// ---------------------------------------------------------------------------
// Ses seviyesi
// ---------------------------------------------------------------------------

float ses_seviyesi_ayarla(float yeni)
{
    g_seviye = std::clamp(yeni, SES_SEVIYESI_EN_AZ, SES_SEVIYESI_EN_FAZLA);
    return g_seviye;
}

float ses_seviyesi_degistir(float adim)
{
    return ses_seviyesi_ayarla(g_seviye + adim);
}

float ses_seviyesi()
{
    return g_seviye;
}

}  // namespace pati
