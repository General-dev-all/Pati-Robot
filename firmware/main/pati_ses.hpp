// Pati ses hatti — I2S mikrofon girisi ve I2S amfi cikisi
//
// Iki AYRI I2S kanali kullaniliyor (I2S_NUM_0 giris, I2S_NUM_1 cikis)
// cunku hizlari farkli: mikrofon 16 kHz, hoparlor 24 kHz. Ayni kanali
// paylasmak mumkun degil.
//
// Bu dosya SADECE bayt tasiyor. Ne olcum yapiyor ne karar veriyor;
// olcum pati_olcum.hpp'de, karar app_main'de.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <esp_err.h>

namespace pati {

// ---------------------------------------------------------------------------
// Mikrofon (INMP441) — 16 kHz mono int16
// ---------------------------------------------------------------------------
//
// ⚠️ INMP441 24 BITLIK bir mikrofon ve verisini 32 bitlik yuvalarin
// SOLUNA yazar. Yani 16 bit isteyip okuyamayiz — 32 bit okuyup kendimiz
// kaydirmamiz gerekiyor. Bu, I2S mikrofonlarinda en sik yapilan hata:
// dogrudan 16 bit istenince ses ya cok kisik ya gurultu cikiyor.
//
// Bu yuzden okuma iki adimli: ham 32 bit -> int16'ya cevir.

esp_err_t mikrofon_baslat();

// Mikrofondan int16 ornek okur. Doner: okunan ORNEK sayisi (bayt degil).
// Blokluyor; timeout_ms kadar bekler.
//
// hedef'in kapasitesi kadar okumaya calisir. Ic tamponda 32 bit ham veri
// icin iki kat yer gerektigi icin tek cagrida en fazla PATI_OKUMA_ORNEK
// ornek doner.
size_t mikrofon_oku(std::span<std::int16_t> hedef, uint32_t timeout_ms = 100);

// Tek cagrida okunabilecek en fazla ornek. 16 kHz'de 20 ms = 320 ornek.
// Gemini'ye 20 ms'lik parcalar halinde gonderiyoruz; prototype'de olculen
// yol da buydu (PARCA_ORNEK = 320).
constexpr size_t PATI_OKUMA_ORNEK = 320;

// ---------------------------------------------------------------------------
// Hoparlor (MAX98357) — 24 kHz mono int16
// ---------------------------------------------------------------------------

esp_err_t hoparlor_baslat();

// Gemini'den gelen 24 kHz int16 orneklerini amfiye yazar.
// Doner: yazilan ornek sayisi.
//
// Ses seviyesi burada uygulaniyor (prototype/ses.py'deki carpanin aynisi).
size_t hoparlor_yaz(std::span<const std::int16_t> kaynak,
                    uint32_t timeout_ms = 200);

// Bekleyen sesi at — cocuk robotun sozunu kesti (barge-in).
//
// Atmazsak robot susmus gorunup birkac saniye sonra eski cumlesine
// devam ediyor; prototype'de bunun "cocuk icin en sinir bozucu davranis"
// oldugu not edilmisti.
esp_err_t hoparlor_temizle();

// ---------------------------------------------------------------------------
// Ses seviyesi — prototype/ayarlar.py'deki degerlerin aynisi
// ---------------------------------------------------------------------------
//
// Cocuk "sesini kis" diyebiliyor. Sinirlar orada da burada da ayni:
// tamamen sessize alma KASITLI olarak yok, cocuk sifira indirip "Pati
// bozuldu" sanmasin.

constexpr float SES_SEVIYESI_BASLANGIC = 0.85f;
constexpr float SES_SEVIYESI_EN_FAZLA  = 1.00f;
constexpr float SES_SEVIYESI_EN_AZ     = 0.15f;
constexpr float SES_SEVIYESI_ADIM      = 0.15f;

// Seviyeyi sinirlar icinde ayarlar, gercekte ne oldugunu dondurur.
float ses_seviyesi_ayarla(float yeni);
float ses_seviyesi_degistir(float adim);
float ses_seviyesi();

}  // namespace pati
