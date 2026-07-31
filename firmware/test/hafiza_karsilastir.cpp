// -*- coding: utf-8 -*-
//
// C++ hafiza mantigi, Python'la AYNI sonuclari mi veriyor?
//
// ===========================================================================
// NEDEN BU TEST VAR
// ===========================================================================
//
// Hafiza SISTEM PROMPTUNA giriyor. Prompt degisirse modelin davranisi
// degisiyor ve PC'de olculen sayilar (medyan 1325 ms, uyum %86) ESP32
// icin gecersiz olur. Yani "benzer bir sey yazdim" yetmiyor: ayni
// cumleler, ayni sirada, ayni sinirlarla uretilmeli.
//
// EN SINSI TUZAK govdeleme:
//
//     Python  k[:5]  -> ilk 5 KARAKTER
//     C++'ta bayt sayilirsa "çiçek" ortasindan kesilir -> bozuk dizi
//
// Bunu gozle gorup yakalamak imkansiz; sadece hafiza "bazen ayni seyi
// iki kez kaydediyor" diye gorunur ve sebebi aylarca aranir.
//
//   1. python test/hafiza_dok.py   -> hafiza_beklenen.txt
//   2. bu program okur ve karsilastirir
//
// Kayit bicimi: satir basi tur harfi, alanlar SEKME ile ayrilmis,
// satir sonu \n olarak kacirilmis.
//
//   K  metin              kucult(metin)
//   G  metin              kok1,kok2,...            (sirali)
//   B  a  b               benzerlik (6 basamak)
//   A  a  b               ayni_bilgi_mi (0/1)
//   P  durum              prompt_blogu()
//   R  metin              kez                      (ekleme sonrasi kayitlar)
//   N  uzunluk                                     (ebeveyn notu siniri)
//   D  ad                 bicim(0/1)   cocuk_adi(0/1)
//   E  ad                 cocuk_adi(0/1)           (robot adi "Osman" iken)
//   Y  ham                gecerli_yas              (0 = elendi)
//   Z  ad                 kabul edilen ad ("" = reddedildi)

#define _CRT_SECURE_NO_WARNINGS

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../main/pati_hafiza.hpp"

// Hafiza motoru. .cpp ice aliniyor ki isimsiz uzaydaki durum
// gorunur olsun (g_bilgiler vb.) — test seami budur.
#include "../main/pati_hafiza.cpp"

namespace {

int hata = 0;
int gecen = 0;

void bak(bool kosul, const std::string& ad)
{
    if (kosul) {
        ++gecen;
    } else {
        std::printf("  HATA  %s\n", ad.c_str());
        ++hata;
    }
}

std::string coz(const std::string& m)
{
    std::string s;
    for (size_t i = 0; i < m.size(); ++i) {
        if (m[i] == '\\' && i + 1 < m.size()) {
            ++i;
            if (m[i] == 'n') { s += '\n'; continue; }
            if (m[i] == 't') { s += '\t'; continue; }
            if (m[i] == '\\') { s += '\\'; continue; }
            s += m[i];
            continue;
        }
        s += m[i];
    }
    return s;
}

std::vector<std::string> ayir(const std::string& satir)
{
    std::vector<std::string> a;
    std::string p;
    std::istringstream ss(satir);
    while (std::getline(ss, p, '\t')) a.push_back(p);
    return a;
}

// Gorunur fark: ilk ayrilan yeri gostermek, "esit degil" demekten
// cok daha faydali.
std::string ilk_fark(const std::string& a, const std::string& b)
{
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    char t[256];
    std::snprintf(t, sizeof(t), "%u. baytta ayriliyor · beklenen \"%.28s\" · olan \"%.28s\"",
                  static_cast<unsigned>(i),
                  a.c_str() + std::min(i, a.size()),
                  b.c_str() + std::min(i, b.size()));
    return t;
}

}  // namespace

int main(int argc, char** argv)
{
    const std::string kok = (argc > 1) ? argv[1] : ".";
    const std::string yol = kok + "/hafiza_beklenen.txt";

    std::ifstream f(yol, std::ios::binary);
    if (!f) {
        std::printf("\n  ! %s yok. Once sunu calistir:\n", yol.c_str());
        std::printf("      python test/hafiza_dok.py\n\n");
        return 2;
    }

    std::printf("\n=== C++ hafiza <-> Python hafiza ===\n\n");

    // Prompt bolumu icin hafizayi ADIM ADIM kuruyoruz; Python dokumu
    // ayni sirayla uretildi.
    int prompt_adim = 0;
    // "E" satirlari robotun adi "Osman" iken uretildi; ilk E satirinda
    // ayni durumu kuruyoruz (bkz. asagida).
    bool ad_degisti = false;

    std::string satir;
    while (std::getline(f, satir)) {
        if (!satir.empty() && satir.back() == '\r') satir.pop_back();
        if (satir.empty()) continue;
        const auto a = ayir(satir);
        if (a.empty()) continue;
        const std::string& tur = a[0];

        if (tur == "K" && a.size() >= 3) {
            const std::string m = coz(a[1]);
            const std::string beklenen = coz(a[2]);
            const std::string olan = pati::hafiza_kucult(m);
            bak(olan == beklenen,
                "kucult(\"" + m + "\") · " + ilk_fark(beklenen, olan));

        } else if (tur == "G" && a.size() >= 3) {
            const std::string m = coz(a[1]);
            const std::string beklenen = coz(a[2]);
            auto k = pati::hafiza_kokler(m);
            std::sort(k.begin(), k.end());
            std::string olan;
            for (size_t i = 0; i < k.size(); ++i) {
                if (i) olan += ",";
                olan += k[i];
            }
            bak(olan == beklenen,
                "kokler(\"" + m + "\") · beklenen [" + beklenen +
                    "] · olan [" + olan + "]");

        } else if (tur == "B" && a.size() >= 4) {
            const std::string x = coz(a[1]), y = coz(a[2]);
            const float beklenen = std::strtof(a[3].c_str(), nullptr);
            const float olan = pati::hafiza_benzerlik(x, y);
            char t[320];
            std::snprintf(t, sizeof(t),
                          "benzerlik(\"%s\", \"%s\") beklenen %.6f olan %.6f",
                          x.c_str(), y.c_str(), beklenen, olan);
            bak(std::fabs(olan - beklenen) < 1e-5f, t);

        } else if (tur == "A" && a.size() >= 4) {
            const std::string x = coz(a[1]), y = coz(a[2]);
            const bool beklenen = (a[3] == "1");
            const bool olan = pati::hafiza_ayni_bilgi_mi(x, y);
            bak(olan == beklenen,
                std::string("ayni_bilgi_mi(\"") + x + "\", \"" + y +
                    "\") beklenen " + (beklenen ? "AYNI" : "AYRI") +
                    " olan " + (olan ? "AYNI" : "AYRI"));

        } else if (tur == "P" && a.size() >= 3) {
            const std::string durum = a[1];
            const std::string beklenen = coz(a[2]);

            // Python dokumu bu sirayla uretti; ayni adimlari uyguluyoruz.
            if (durum == "ad" && prompt_adim < 1) {
                pati::hafiza_cocugu_tanimla("Bulut", 0);
                prompt_adim = 1;
            } else if (durum == "ad_yas" && prompt_adim < 2) {
                pati::hafiza_cocugu_tanimla("Bulut", 6);
                prompt_adim = 2;
            } else if (durum == "not" && prompt_adim < 3) {
                pati::hafiza_ebeveyn_notu_kaydet(
                    "Aksam sekizde dis fircalamasi gerekiyor.");
                prompt_adim = 3;
            } else if (durum == "tam" && prompt_adim < 4) {
                const char* eklenecek[] = {
                    "Kedisinin adi Pamuk",
                    "Dinozorlari cok seviyor",
                    "Karanliktan biraz korkuyor",
                    "Öğretmeninin adı Sevgi",
                    "Basketbol oynuyor",
                    "Okul takiminda basketbol oynuyor",
                    "Brokoli sever",
                    "Brokoli sevmez",
                };
                for (const char* m : eklenecek) pati::hafiza_bilgi_ekle(m);
                pati::hafiza_bilgi_ekle("Kedisinin adi Pamuk");
                pati::hafiza_bilgi_ekle("Kedisinin adi Pamuk");
                prompt_adim = 4;
            }

            const std::string olan = pati::hafiza_prompt_blogu();
            bak(olan == beklenen,
                "prompt_blogu[" + durum + "] · " + ilk_fark(beklenen, olan));

        } else if (tur == "R" && a.size() >= 3) {
            const std::string metin = coz(a[1]);
            const int kez = std::atoi(a[2].c_str());
            const auto& b = pati::hafiza_bilgiler();
            const auto it = std::find_if(
                b.begin(), b.end(),
                [&](const pati::Bilgi& x) { return x.metin == metin; });
            if (it == b.end()) {
                bak(false, "kayit YOK: \"" + metin + "\"");
            } else {
                bak(it->kez == kez,
                    "kayit \"" + metin + "\" kez beklenen " +
                        std::to_string(kez) + " olan " + std::to_string(it->kez));
            }

        } else if (tur == "D" && a.size() >= 4) {
            // Ad suzgeci. Iki sutun cunku iki ayri kural var: BICIM
            // (yer tutucu / uzunluk / rakam) ve COCUK ADI (bicim +
            // "icinde robotun adi gecmesin"). "Pati" bicimce gecerli
            // bir ad ama cocugun adi olamaz — tek sutun olsaydi bu
            // ayrim test edilemezdi.
            const std::string ad = coz(a[1]);
            const bool bicim_bek = (a[2] == "1");
            const bool cocuk_bek = (a[3] == "1");
            const bool bicim = pati::hafiza_ad_bicimi_uygun_mu(ad);
            const bool cocuk = pati::hafiza_cocuk_adi_gecerli_mi(ad);
            bak(bicim == bicim_bek,
                "ad_bicimi(\"" + ad + "\") beklenen " +
                    (bicim_bek ? "1" : "0") + " olan " + (bicim ? "1" : "0"));
            bak(cocuk == cocuk_bek,
                "cocuk_adi(\"" + ad + "\") beklenen " +
                    (cocuk_bek ? "1" : "0") + " olan " + (cocuk ? "1" : "0"));

        } else if (tur == "E" && a.size() >= 3) {
            // Python dokumu bu satirlardan ONCE robotun adini "Osman"
            // yapti; ayni durumu kuruyoruz. Amac: suzgec SU ANKI ada da
            // bakiyor mu? Eski C++ surumu sadece sabit "pati" ariyordu
            // ve bu satirlarda kalirdi.
            if (!ad_degisti) {
                pati::hafiza_robot_adini_degistir("Osman");
                ad_degisti = true;
            }
            const std::string ad = coz(a[1]);
            const bool beklenen = (a[2] == "1");
            const bool olan = pati::hafiza_cocuk_adi_gecerli_mi(ad);
            bak(olan == beklenen,
                "cocuk_adi[robot=Osman](\"" + ad + "\") beklenen " +
                    (beklenen ? "1" : "0") + " olan " + (olan ? "1" : "0"));

        } else if (tur == "Y" && a.size() >= 3) {
            const int ham = std::atoi(a[1].c_str());
            const int beklenen = std::atoi(a[2].c_str());
            const int olan = pati::hafiza_yas_gecerli_mi(ham);
            bak(olan == beklenen,
                "yas(" + std::to_string(ham) + ") beklenen " +
                    std::to_string(beklenen) + " olan " +
                    std::to_string(olan));

        } else if (tur == "Z" && a.size() >= 3) {
            const std::string ad = coz(a[1]);
            const std::string beklenen = coz(a[2]);
            // Python None dondurunce dokume "" yaziliyor; bizde de
            // reddedilen ad ESKI adi degistirmemeli.
            const std::string olan = pati::hafiza_robot_adini_degistir(ad)
                                         ? pati::hafiza_robot_adi()
                                         : std::string();
            bak(olan == beklenen,
                "robot_adini_degistir(\"" + ad + "\") beklenen \"" +
                    beklenen + "\" olan \"" + olan + "\"");

        } else if (tur == "N" && a.size() >= 2) {
            const int beklenen = std::atoi(a[1].c_str());
            std::string uzun;
            for (int i = 0; i < 5000; ++i) uzun += "ç";
            pati::hafiza_ebeveyn_notu_kaydet(uzun);
            // Python KARAKTER sayiyor; bizde de kod noktasi sayilmali.
            const int olan =
                pati::kn_sayisi(pati::hafiza_ebeveyn_notu());
            bak(olan == beklenen,
                "ebeveyn notu siniri beklenen " + std::to_string(beklenen) +
                    " olan " + std::to_string(olan));
        }
    }

    // PANELIN OKUDUGU ALANLAR GERCEKTEN URETILIYOR MU?
    //
    // 31.07.2026'da bulunan hata: `panel/pati.js` §hafizaAl
    // `ozet.robot_adi`'ni okuyor ama firmware bu alani HIC gondermiyordu.
    // Panel varsayilana dusuyor ve cocuk robotun adini degistirmis olsa
    // bile ebeveyn hep "Pati" goruyordu — panel sessizce yalan
    // soyluyordu. Derleme bunu yakalamaz: eksik bir JSON alani gecerli
    // JSON'dur. O yuzden burada aciktan araniyor.
    {
        const std::string oz = pati::hafiza_ozet_json();
        for (const char* alan : {"\"robot_adi\"", "\"robot_adi_varsayilan\"",
                                 "\"cocuk\"", "\"bilgiler\"",
                                 "\"ebeveyn_notu\""}) {
            bak(oz.find(alan) != std::string::npos,
                std::string("ozet_json'da ") + alan + " YOK — panel bu "
                "alani okuyor (panel/pati.js)");
        }
    }

    // Python'da kaydedilen kayit SAYISI ile bizdeki ayni mi? Fazla kayit
    // = birlesme kaciridi, eksik = fazla birlestirdi.
    std::printf("\n  %d kontrol gecti, %d basarisiz\n", gecen, hata);
    std::printf("  C++ kayit sayisi: %u\n",
                static_cast<unsigned>(pati::hafiza_bilgiler().size()));

    std::printf("\n%s\n\n",
                hata == 0 ? "  IKI TARAF AYNI SONUCU VERIYOR"
                          : "  HAFIZA MANTIGI AYRILIYOR");
    return hata ? 1 : 0;
}
