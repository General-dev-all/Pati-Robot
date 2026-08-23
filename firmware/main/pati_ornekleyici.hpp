// Pati'nin sesini ureten matematik — yeniden ornekleme ve sinirlama
//
// ===========================================================================
// NEDEN AYRI BIR BASLIK, NEDEN pati_ses.cpp'NIN ICINDE DEGIL
// ===========================================================================
//
// Cunku KONAKTA SINANIYOR. Bu dosyayi hem firmware (pati_ses.cpp) hem
// konak testi (test/ses_karsilastir.cpp) ice aliyor, yani test gercek
// kodu olcuyor — kopyasini degil.
//
// Ayni gerekce goz cizicide de vardi ve orada uc gercek hata buldu.
// Burada risk daha da yuksek: bu kod Pati'nin SESINI uretiyor ve
// bozuldugunda "kotu ses" diye duyuluyor, "hata" diye degil. Kart
// Eylul'de gelecek; o zamana kadar dogrulanmasinin tek yolu bu.
//
// ===========================================================================
// NE YAPIYOR
// ===========================================================================
//
// Gemini 24 kHz PCM gonderiyor. ES8311 48 kHz'de kosuyor (mikrofonla
// ayni hatti paylastigi icin baska secenek yok). Arada oran cevirmek
// gerekiyor ve ayni islemde Pati'nin afacan sesini veren HIZ CARPANI
// da uygulaniyor:
//
//     adim = 24000 x carpan / 48000 = carpan / 2
//
// 1.30'da adim 0,65. Yani her cikis ornegi kaynakta 0,65 ilerliyor ve
// aradaki degerler DOGRUSAL ARA DEGERLE uretiliyor.
//
// 🔴 ADIM HER ZAMAN 1'DEN KUCUK ve bu tesaduf degil — carpan tavani
// 1.60, yani adim tavani 0,80. Adim 1'in altindayken ara deger
// uretiliyor: hicbir ornek atilmiyor ve KATLANMA (aliasing) olusmuyor.
// Adim 1'in ustunde olsaydi seyreltme yapardik, tiz sesler bandin
// icine katlanirdi ve Pati cizirdayarak konusurdu.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace pati {

// ---------------------------------------------------------------------------
// YUMUSAK SINIRLAYICI — 1.0 ustu ses seviyesini mumkun kilan sey
// ---------------------------------------------------------------------------
//
// Esigin ALTINDA hicbir sey yapmiyor: sesin buyuk kismi dokunulmadan
// geciyor. Ustunde artan bir oranla sikisip 32767'ye ASIMPTOT olarak
// yaklasiyor, yani girdi ne kadar buyurse buyusun cikti tavani ASMIYOR.
//
// NEDEN DUZ KIRPMA DEGIL: kirpma dalga tepesini duz keser ve bu, kulaga
// catirti gibi gelir — cocuk icin en rahatsiz edici bozulma bicimi.
// Asimptotik sikisma tepeyi YUVARLAR; yuksek seviyede hafif bir tokluk
// birakir, catirti birakmaz.
//
// Esik 28000 (~0,85 tam genlik) bilincli: konusmanin buyuk bolumu
// bunun altinda kaliyor ve hic sekillendirilmiyor.
constexpr float SINIR_ESIK  = 28000.0f;
constexpr float SINIR_TAVAN = 32767.0f;

inline std::int16_t yumusak_sinirla(float v)
{
    const float a = (v < 0.0f) ? -v : v;
    if (a <= SINIR_ESIK) {
        return static_cast<std::int16_t>(v);
    }
    constexpr float PAY = SINIR_TAVAN - SINIR_ESIK;
    const float fazla = a - SINIR_ESIK;
    const float y = SINIR_ESIK + PAY * (fazla / (fazla + PAY));
    return static_cast<std::int16_t>((v < 0.0f) ? -y : y);
}

// ---------------------------------------------------------------------------
// YENIDEN ORNEKLEYICI
// ---------------------------------------------------------------------------
//
// 🔴 DURUM TASIYOR VE TASIMAK ZORUNDA.
//
// Gemini sesi parca parca geliyor ama parcalar KESINTISIZ tek bir
// akisin dilimleri. Yeniden ornekleme her parcada sifirdan baslasaydi
// her sinirda kucuk bir sicrama olurdu — saniyede birkac kez duyulan
// bir tik. O yuzden iki sey parcalar ARASINDA tasiniyor:
//
//   faz_    : bir sonraki cikis orneginin kaynaktaki kesirli konumu
//   onceki_ : onceki parcanin SON ornegi
//
// onceki_ olmadan sinirdaki ilk ara deger uretilemez: kaynak[0] ile
// ondan ONCEKI ornek arasinda deger istiyoruz ve o ornek artik elimizde
// olmayan parcada.
class YenidenOrnekleyici {
public:
    // Akisi bastan baslat. Ses atildiginda (barge-in) cagriliyor:
    // atilan sesin fazini saklamak, sonraki cumleyi yarim bir ornekten
    // baslatmak olurdu.
    void sifirla()
    {
        faz_ = 0.0f;
        onceki_ = 0;
    }

    // kaynak'i yeniden ornekler, seviye ile olcekler, sinirlayicidan
    // gecirir.
    //
    // Cikti tek parca halinde DONMUYOR: `tampon` doldukca `yaz`
    // cagriliyor. Sebep bellek — bir parca en yavas hizda 2,5 kata
    // kadar uzayabiliyor ve tamamini bir yerde tutmak gereksiz.
    // `yaz` false donerse islem durur (hedef kuyruk dolmus demektir).
    //
    // Doner: TUKETILEN KAYNAK ornegi. Uretilen cikis ornegi degil —
    // ikisi farkli ve cagirani ilgilendiren, verdiginin ne kadarinin
    // islendigi.
    template <class Yaz>
    std::size_t isle(std::span<const std::int16_t> kaynak, float adim,
                     float seviye, std::span<std::int16_t> tampon, Yaz yaz)
    {
        if (kaynak.empty() || tampon.empty()) {
            return 0;
        }

        const std::size_t N = kaynak.size();
        std::size_t n = 0;
        bool kesildi = false;

        // ---------------------------------------------------------------
        // 🔴 KONUM "TAMSAYI + KESIR" TUTULUYOR, TEK BIR float DEGIL.
        // ---------------------------------------------------------------
        //
        // Once tek bir `float t` vardi ve her ornekte `t += adim`
        // yapiliyordu. Calisiyor gorunuyordu; konak sinamasi iki gercek
        // hata buldu (23.08.2026):
        //
        //   1. Tek parca halinde islenen ses ile dilimler halinde
        //      islenen ses AYRISIYORDU.
        //   2. 500 saniyelik akista 1.661 ornek fazla uretiliyordu.
        //
        // Sebep ayni: float'in hassasiyeti buyuklukle azaliyor. t bir
        // dilimin sonunda 6000'e yaklasinca en kucuk adim ~0,00036
        // oluyor ve 0,65 eklemek her seferinde YUVARLANIYOR. Yuvarlama
        // tek yone birikince konum kayiyor. Ustelik kayma t'nin
        // BUYUKLUGUNE bagli oldugu icin ayni ses, farkli dilim
        // boyutlarinda farkli sonuc veriyordu.
        //
        // Cozum: kesri hicbir zaman 1'in ustune cikarmamak. [0,1)
        // araliginda float'in en kucuk adimi ~6e-8, yani buyuklukten
        // gelen kayip tamamen ortadan kalkiyor. Tamsayi kismi ayri bir
        // sayacta duruyor ve tamsayi aritmetigi kayipsiz.
        //
        // Yan fayda: sonuc artik dilim boyutundan BAGIMSIZ. Ayni ses,
        // nasil bolunurse bolunsun ayni ciktiyi veriyor.
        //
        // Sanal dizi: v[0] onceki parcanin son ornegi, v[1..N] bu parca.
        // Sinirda ara deger ancak boyle uretilebiliyor.
        std::size_t i = 0;
        float f = faz_;  // her zaman [0, 1)

        while (i + 1 <= N) {
            const float a = (i == 0) ? static_cast<float>(onceki_)
                                     : static_cast<float>(kaynak[i - 1]);
            const float b = static_cast<float>(kaynak[i]);

            tampon[n++] = yumusak_sinirla((a + (b - a) * f) * seviye);

            if (n == tampon.size()) {
                if (!yaz(std::span<const std::int16_t>(tampon.data(), n))) {
                    kesildi = true;
                    break;
                }
                n = 0;
            }

            f += adim;
            while (f >= 1.0f) {
                f -= 1.0f;
                ++i;
            }
        }

        if (!kesildi && n > 0) {
            if (!yaz(std::span<const std::int16_t>(tampon.data(), n))) {
                kesildi = true;
            }
        }

        if (kesildi) {
            // Kalan atildi, yani sureklilik zaten koptu. Fazi ve
            // sinir ornegini saklamak yanlis olurdu.
            faz_ = 0.0f;
            onceki_ = 0;
            return (i < N) ? i : N;
        }

        // Yeni v[0], bu parcanin son ornegi olacak (sanal dizide v[N]).
        // Adim 1'den kucuk oldugu icin i tam olarak N'de duruyor, yani
        // kalan faz dogrudan f. Genel halde (adim >= 1) de dogru olsun
        // diye fark yaziliyor.
        onceki_ = kaynak[N - 1];
        faz_ = static_cast<float>(i - N) + f;
        return N;
    }

    // Sinama icin: ic durum. Firmware kullanmiyor.
    float faz() const { return faz_; }

private:
    float faz_ = 0.0f;
    std::int16_t onceki_ = 0;
};

}  // namespace pati
