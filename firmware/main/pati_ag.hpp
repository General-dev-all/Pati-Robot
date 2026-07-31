// Pati'nin ag baglantisi — kurulumu cocugun ebeveyni yapiyor.
//
// ===========================================================================
// BU DOSYA pati_wifi.cpp'NIN YERINE GELDI
// ===========================================================================
//
// Eski dosyada wifi adi ve sifresi FIRMWARE'E GOMULUYDU ve basinda
// "silinmek uzere yazildi" diye yaziyordu. Sebebi Asama 2'nin tek
// amaciydi: gecikme olcmek. Simdi gercek robota geciyoruz ve gomulu
// wifi ile robot TESLIM EDILEMEZ:
//
//   - Cocuk baska bir eve gidince robot oluyor
//   - Router sifresi degisince robot oluyor
//   - Duzeltmek icin bilgisayara baglayip yeniden derlemek gerekiyor
//
// Projenin hedefi tam tersi: "bilgisayarsiz, kurulumsuz".
// Bu yuzden captive portal secildi (30.07.2026).
//
// ===========================================================================
// UC DURUM
// ===========================================================================
//
//   ARIYOR    NVS'te kayitli ag var, baglanmaya calisiyor
//   BAGLI     ev agina baglandi, Gemini'ye cikabiliyor
//   KURULUM   kayitli ag yok ya da baglanamadi
//             -> kendi agini yayinliyor: "Pati-A3F2"
//             -> telefon baglaninca sayfa KENDILIGINDEN aciliyor
//
// KURULUM modunda mod APSTA: AP telefon icin, STA ise TARAMA icin.
// esp_wifi_scan_start() STA arayuzu istiyor; sadece AP modunda olsak
// ebeveyne ag listesi gosteremezdik ve sifreyi elle yazdirmak
// zorunda kalirdik.
//
// ===========================================================================
// AP SIFRESIZ — bilincli
// ===========================================================================
//
// Kurulum agi acik. Sebep: sifreli yapsak sifreyi bir yere yazmak
// gerekirdi (kutuya etiket, kilavuz) ve ebeveyn onu bulamazsa robot
// kurulamaz. Acik agin riski, o kisa pencerede komsunun baglanip
// ev wifi sifresini GIRMESI degil — GORMESI. O yuzden:
//
//   - Kayitli sifre panelde HIC gosterilmiyor (yazilabilir, okunamaz)
//   - Baglanti kurulunca AP KAPANIYOR, pencere kapaniyor

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <esp_err.h>

namespace pati {

enum class AgDurumu {
    Kapali,
    Ariyor,
    Bagli,
    Kurulum,     // kendi agini yayinliyor
};

struct BulunanAg {
    std::string ad;
    int guc = 0;        // 1..4 (cubuk sayisi)
    bool kilit = true;
};

// Agi baslatir: NVS'te kayit varsa baglanmayi dener, yoksa dogrudan
// kurulum moduna gecer.
//
// BLOKLAMIYOR — durum `ag_durumu()` ile izleniyor. Bloklasaydi ekran
// ve gozler baglanti bitene kadar olu kalirdi.
esp_err_t ag_baslat();

AgDurumu ag_durumu();
const char* ag_ip();

// Bagli oldugumuz agin adi; kurulum modunda kendi AP adimiz.
const char* ag_adi();

// 1..4 cubuk. Bagli degilse 0.
int ag_gucu();

// Ag taramasi. Kurulum modunda ve bagliyken de calisiyor (APSTA / STA).
//
// BLOKLAR (~2 sn). HTTP isteginin icinden cagriliyor; tarayici zaten
// bekliyor. Sohbet gorevini etkilemiyor cunku ayri gorevde.
std::vector<BulunanAg> ag_tara();

// Yeni ag bilgisi: NVS'e yazar ve baglanmayi dener.
//
// Basarisiz olursa ESKI KAYIT GERI YUKLENIYOR. Yoksa ebeveyn sifreyi
// yanlis yazdiginda robot calisan agini da kaybediyor ve elde hicbir
// sey kalmiyor.
esp_err_t ag_kaydet_ve_bagla(const std::string& ad, const std::string& sifre);

// Kayitli agi siler (fabrika ayarlari).
void ag_unut();

// Kayitli bir ag var mi? Sifreyi DONDURMUYOR — panelde gosterilmemesi
// icin okuma yolu bilincli olarak yok.
bool ag_kayitli_var();

}  // namespace pati
