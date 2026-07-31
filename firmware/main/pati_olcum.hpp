// Pati olcum defteri — prototype/olcum.py'nin ESP32 karsiligi
//
// ASAMA 2'NIN BUTUN AMACI BU DOSYA. Firmware'in geri kalani (wifi, I2S,
// Gemini istemcisi) buranin dogru sayi uretmesi icin var.
//
// Olculen sey PLAN.md'teki kriter: cocuk sustu -> ilk ses <= 1500 ms.
// Kriter olcumden ONCE yazildi; sonucu tartisilmiyor, sayiya bakiliyor.
//
// ---------------------------------------------------------------------------
// PC'DEKI SAYIYLA KARSILASTIRILABILIR OLMASI SART
// ---------------------------------------------------------------------------
// Asama 1'de PC'de medyan 1325 ms olculdu. Bu kartta olculen sayinin
// onunla karsilastirilabilmesi icin medyan ve p90 hesabinin BIREBIR AYNI
// olmasi gerekiyor — farkli yontem, farkli sayi, yanlis karar demek.
//
// Python'un iki ozelligi kopyalandi (ayrintisi .cpp'de):
//   · statistics.median  -> cift sayida degerde ortadaki IKISININ ortalamasi
//   · round()            -> BANKACI yuvarlamasi (0.5 en yakin cift sayiya)
//
// C++'in std::lround'u 0.5'i yukari yuvarliyor. 6 turluk bir kosuda bile
// p90 farkli cikardi.

#pragma once

#include <cstddef>
#include <cstdint>

namespace pati {

// Bir tur = cocuk konustu, robot cevap verdi.
//
// Zaman damgalarinin hepsi esp_timer_get_time() mikrosaniyesi. Hicbiri
// "yaklasik" degil; her biri kodun belirli bir satirinda, olayin oldugu
// anda aliniyor.
struct Tur {
    // Cocuk tarafi
    std::int64_t t_sustu = 0;          // <-- OLCUMUN SIFIR NOKTASI

    // Robot tarafi
    std::int64_t t_ilk_paket = 0;      // ilk ses baytinin SOKETTEN gelisi
    std::int64_t t_ilk_hoparlor = 0;   // ilk ses baytinin I2S'E gidisi
    std::int64_t t_tur_bitti = 0;

    // Sozunu kesme
    std::int64_t t_kesme_konusma = 0;  // cocuk robotun uzerine konustu
    std::int64_t t_kesme_onay = 0;     // sunucudan "iptal edildi" geldi
    bool kesildi = false;

    std::size_t ses_bayt = 0;

    // KRITER BU: cocuk sustu -> ilk ses hoparlorden cikti.
    // Doner: ms, ya da olculemezse -1.
    double gecikme_ms() const;

    // Cocuk sustu -> ilk ses PAKETI soketten geldi.
    //
    // Bu parca Gemini'nin "sustu" karari + ag gidis-donusu + modelin
    // dusunup ses uretmesi. PC'de olculen degerle DOGRUDAN
    // karsilastirilabilir olan kisim bu.
    double ag_gecikmesi_ms() const;

    // Ilk paket geldi -> I2S'e verildi. PC'deki karsiligi PortAudio
    // yigitiydi; buradaki I2S DMA. Ikisi FARKLI, o yuzden ayri duruyor.
    double yerel_ms() const;

    bool tamam_mi() const;
};

// ---------------------------------------------------------------------------
// Defter
// ---------------------------------------------------------------------------
//
// Sabit boyutlu; ESP32'de olcum sirasinda bellek ayirmak istemiyoruz —
// ayirma gecikmesi tam olctugumuz seye karisir.
constexpr std::size_t EN_FAZLA_TUR = 200;

void olcum_baslat();

// Yeni tur ac. Doner: acilan turun isaretcisi, ya da defter dolduysa
// nullptr (dolmasi 200 turdan sonra; 20 dakikalik kosuda ~60 tur oluyor).
Tur* tur_ac();
Tur* acik_tur();
void tur_kapat();

// Damgalar.
//
// us parametresi: 0 verilirse "simdi" (esp_timer_get_time). Sifirdan
// buyuk verilirse O AN kullaniliyor.
//
// NEDEN ONEMLI: olaylar bir FreeRTOS kuyrugundan geciyor ve kuyrukta
// bekleme suresi olcume KARISMAMALI. stackchan'in ConversationEvent'i
// emit_us alaninda olayin URETILDIGI ani veriyor; damgalarken onu
// gecirmek dogru sayiyi verir, kuyruktan aldigimiz an degil.
void damga_sustu(std::int64_t us = 0);
void damga_ilk_paket(std::size_t bayt, std::int64_t us = 0);
void damga_ilk_hoparlor(std::int64_t us = 0);
void damga_kesme_konusma(std::int64_t us = 0);
void damga_kesme_onay(std::int64_t us = 0);

// Wifi yetisemedigi icin dusen mikrofon parcasi sayisi.
//
// NEDEN ONEMLI: olcum kotu cikarsa sucun Gemini'de mi bizim wifi'de mi
// oldugunu bu ayiriyor. stackchan'in istemcisi tx_evicted_chunks() ile
// veriyor, biz her tur sonunda okuyup kaydediyoruz.
void dusen_parca_bildir(std::uint32_t toplam);

// Tur sonunda tek satir ozet basar.
void tur_ozeti_yaz();

// Kosu sonunda tam rapor basar: dagilim, kriter karari, ve NELERI
// OLCMEDIGIMIZ.
void rapor_yaz();

// ---------------------------------------------------------------------------
// OZ-TEST — matematigin PC ile ayni oldugunu CIHAZDA kanitliyor
// ---------------------------------------------------------------------------
//
// Neden cihazda: bu makinede host C++ derleyicisi yok, yani medyan/p90
// cevirisinin dogrulugu burada sinanamiyor. Sinav cihaza gomuldu; kart
// ilk acildiginda sonucu soyluyor.
//
// Beklenen degerler Python'dan alindi (statistics.median ve olcum.py'nin
// p90 formulu). Girdi n=6 olarak KASITLI secildi: 0.9*(6-1)=4.5, yani
// bankaci yuvarlamasi ile lround'un ayristigi tam nokta. Ceviri yanlissa
// p90 1400 yerine 1500 cikar ve test bunu yakalar.
//
// Doner: true = matematik PC ile ayni.
bool oz_test();

}  // namespace pati
