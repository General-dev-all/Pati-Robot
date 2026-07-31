// Panelden guncelleme — GitHub'dan indirip kendini yeniden yazar.
//
// ===========================================================================
// AKIS
// ===========================================================================
//
//   Mert           surum.txt'yi yukseltir, derler, pati.bin'i GitHub
//                  Release'ine koyar, surum.json'i gunceller, push eder
//   Anne           pati.local -> "Güncellemeleri kontrol et"
//   Pati           surum.json'i ceker, kendi surumuyle karsilastirir
//   Anne           yeni surum varsa "Güncelle"
//   Pati           pati.bin'i bos OTA bolumune indirir, yeniden baslar
//
// ===========================================================================
// NEDEN IKI AYRI ADRES (surum.json ve pati.bin)
// ===========================================================================
//
// surum.json depoda duruyor ve raw.githubusercontent.com'dan geliyor:
// kucuk, kaynak dosyasi, gecmisi git'te gorunuyor.
//
// pati.bin ise Release'e konuyor, DEPOYA GIRMIYOR. .gitignore'un ilk
// satiri "Depoya SADECE kaynak kod girer" diyor ve her surumde 1,4 MB'lik
// bir ikili dosyayi git gecmisine gomsek o kural bir yalana donerdi.
// Release tam bu is icin var.
//
// Bedeli: GitHub Release adresi 302 ile BASKA bir sunucuya yonleniyor.
// 01.08.2026'da gercek kartta olculdu, hedef
// `release-assets.githubusercontent.com` idi — ama bu ad GitHub'in
// bilecegi is ve degisebilir, o yuzden hicbir yere yazilmiyor.
// esp_https_ota yonlendirmeyi izliyor ve sertifika demeti ACIK oldugu
// icin ikinci sunucunun sertifikasi da dogrulaniyor. Kendi
// sertifikamizi gomseydik burasi kirilirdi.
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
