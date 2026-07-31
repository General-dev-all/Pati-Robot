// Panelden guncelleme — GitHub'dan indirip kendini yeniden yazar.
//
// ===========================================================================
// AKIS
// ===========================================================================
//
//   Mert           firmware/surum.txt'yi yukseltir, push eder. Baska
//                  hicbir sey yapmaz.
//   GitHub         derler, Release acar, icine pati.bin + surum.json
//                  koyar (.github/workflows/surum.yml)
//   Anne           pati.local -> "Güncellemeleri kontrol et"
//   Pati           surum.json'i ceker, kendi surumuyle karsilastirir
//   Anne           yeni surum varsa "Güncelle"
//   Pati           pati.bin'i bos OTA bolumune indirir, yeniden baslar
//
// ===========================================================================
// IKISI DE AYNI RELEASE'IN ICINDE — ve bu bilincli
// ===========================================================================
//
// surum.json bir donem DEPODA durdu. Sessiz bir kusuru vardi: push,
// manifesti ANINDA yayina sokuyor ama pati.bin'i olusturmuyor. Derleme
// bitene kadarki dakikalarda panel "guncelleme var" der, anne basar ve
// indirme 404'e duserdi.
//
// Ikisi ayni Release'te olunca manifest, tarif ettigi dosyadan ONCE VAR
// OLAMIYOR. Bosluk dikkatle degil, YAPISI GEREGI kapali.
//
// pati.bin depoya girmiyor: .gitignore'un ilk satiri "Depoya SADECE
// kaynak kod girer" diyor ve her surumde 1,4 MB'lik bir ikili dosyayi
// git gecmisine gomsek o kural bir yalana donerdi.
//
// ===========================================================================
// BEDELI: HER IKI ADRES DE YONLENDIRIYOR
// ===========================================================================
//
// Hem `releases/latest/download/surum.json` hem `releases/download/.../
// pati.bin`, imzali bir indirme adresine 302 ile atiyor. 01.08.2026'da
// gercek kartta olculdu: ikinci Location basligi 897 KARAKTER, ve
// `esp_http_client`'in varsayilan tamponu 512. Ikisinde de tampon
// buyutuldu.
//
// Hedef sunucunun adi hicbir yere yazilmiyor: GitHub'in bilecegi is ve
// degisebilir. Sertifika demeti ACIK oldugu icin hangisi olursa olsun
// dogrulaniyor — kendi sertifikamizi gomseydik burasi kirilirdi.
//
// ⚠️ Yonlendirme KENDILIGINDEN izlenmiyor. `esp_https_ota` izliyor ama
// duz `esp_http_client`'ta yalnizca `perform()` izliyor; elle acilan
// istekte 30x sadece bir durum kodu olarak geliyor. Manifest cekimi tam
// bu yuzden bir kez kirildi (ayrinti pati_guncelleme.cpp'de).
//
// ===========================================================================
// NEDEN SHA256 YOK
// ===========================================================================
//
// Ilk tasarimda surum.json'a sha256 alani konmustu. Cikarildi: cihaz onu
// DOGRULAYAMIYOR (esp_https_ota veriyi kendi iceride yaziyor, akisa
// dokunmuyoruz) ve dogrulanmayan bir alan, dogrulaniyormus izlenimi
// verdigi icin yoklugundan kotudur.
//
// Yerine cihazin GERCEKTEN yapabildigi iki denetim var:
//
//   1. Uygulama imzasi. ESP-IDF her uygulama imajinin sonuna kendi
//      SHA256'sini koyuyor; esp_ota_end ve onyukleyici onu dogruluyor.
//      Yarim inen ya da bozulan dosya zaten kabul edilmiyor.
//   2. Surum esleme. Inen imajin icindeki surum, surum.json'un vaat
//      ettigi surumle ayni mi? Degilse yanlis dosya yuklenmis demektir
//      (Release'e eski bin birakmak, etiketi karistirmak). Panel "2.1.0
//      yuklendi" derken cihazda 2.0.0 kosmasi, sessizce yanlis olan tam
//      da o durum.
//
// ===========================================================================
// GERI ALMA
// ===========================================================================
//
// CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE acik. Yeni yapi "denemede"
// aciliyor; `guncelleme_onayla()` cagrilmadan once yeniden baslarsa
// onyukleyici eski yapiya donuyor. Cagrildigi yer ve gerekcesi
// app_main.cpp'de.

#pragma once

#include <string>

#include <esp_err.h>

namespace pati {

enum class GuncellemeDurumu {
    Bos,        // henuz bakilmadi
    Bakiliyor,  // surum.json cekiliyor
    Guncel,     // en son surum zaten kurulu
    Var,        // yeni surum bulundu, bekliyor
    Iniyor,     // pati.bin iniyor
    Bitti,      // yazildi; cihaz yeniden basliyor
    Hata,
};

// Flash'ta KOSAN yapinin surumu (esp_app_desc). surum.txt'den geliyor.
const char* guncelleme_surumu();

esp_err_t guncelleme_baslat();

// surum.json'i ceker ve karsilastirir. HEMEN DONER, isi ayri bir gorevde
// yapar: panel istegi HTTP sunucusunun gorevinde isleniyor ve orada
// saniyelerce beklemek paneli butunuyle kilitler.
void guncelleme_kontrol_et();

// Indirmeyi baslatir. Hemen doner. Yeni surum bulunmamissa hicbir sey
// yapmaz.
//
// Sohbeti kendisi durduruyor: indirme sirasinda hem TLS tamponu hem ses
// kuyruklari ayni anda ayakta olursa PSRAM'de sikisiyoruz, ustelik
// cocugun cumlesinin ortasinda yeniden baslatmak da dogru olmaz.
void guncelleme_indir();

// Kosan yapiyi SAGLAM olarak isaretler ve geri almayi iptal eder.
// Cagrilmazsa bir sonraki acilista onyukleyici eski yapiya doner.
void guncelleme_onayla();

// Panelin gosterecegi hali (JSON parcasi, disi suslu parantez yok).
std::string guncelleme_json();

}  // namespace pati
