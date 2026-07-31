// URETILMIS DOSYA — ELLE DEGISTIRILMEZ.
//
// Kaynak : prototype/hafiza.py  (CIKARIM_PROMPTU, CIKARIM_MODELI)
// Ureten : firmware/prompt_uret.py

#pragma once

namespace pati {

// Ucuz model: cikarim konusma bittikten sonra calisiyor, cocuk
// beklemiyor. Oturum basina ~$0,00065.
inline constexpr const char* CIKARIM_MODELI = "gemini-3.1-flash-lite";

inline constexpr const char* CIKARIM_PROMPTU = R"PATIPROMPT(Asagidaki konusmadan SADECE COCUK hakkinda kalici bilgi cikar.

⚠ SADECE "Cocuk:" ile baslayan satirlara bak. "Robot:" satirlari yalnizca
baglam icindir; oradan BILGI CIKARMA. Robot yanlis duymus ya da uydurmus
olabilir — onun cumlesi kanit degildir.

YAZILACAKLAR (sadece cocugun KENDI soyledikleri):
yasi, ailesi, evcil hayvani, okulu, sinifi, sevdigi ve sevmedigi seyler,
hobileri, arkadaslari, hedefleri.

ASLA YAZILMAYACAKLAR:
- Robot hakkinda hicbir sey. Robotun adi, ne hissettigi, ne hatirladigi.
- Robotun sordugu sorular veya soyledikleri.
- O anki ruh hali, hava durumu, gecici olaylar.

⚠ COCUK ROBOTA YENI BIR AD TAKABILIR: "bundan sonra senin adin X",
"sana X diyecegim", "adin artik X olsun". Bu ROBOTUN adidir, COCUGUN
DEGIL. Bilgi olarak YAZMA, {"ad": ...} olarak da yazma; bunun yerine
{"robot_adi": "X"} nesnesi koy. Cocuk her istedigi zaman
degistirebilir; en son soyledigi gecerlidir.

⚠ COCUK "BUNU UNUTMA" DERSE: o cumledeki bilgiyi MUTLAKA yaz. Bu
satirlar dokumde (!) ile isaretli. Cocuk ozellikle istediyse
"emin degilsen yazma" kurali GECERSIZDIR — istedigi seyi yaz.

BICIM:
- Her bilgi tek cumle, ucuncu sahis, kisa. Ornek: "Kopeginin adi Karabas."
- Cumleye "Cocuk" veya "Robot" diye baslama.
- Emin degilsen yazma. Bos liste, yanlis bilgiden iyidir.
- Sadece JSON dizisi dondur, baska hicbir sey yazma.

COCUGUN ADI ayri tutuluyor. Adini ogrendiysen bilgi olarak YAZMA;
bunun yerine dizinin ilk elemani olarak {"ad": "..."} nesnesi koy.
Ad SADECE cocuk kendi adini soylediginde yazilir: "benim adim X",
"ben X", "bana X derler".

⚠ ADINI SOYLEMEDIYSE {"ad": ...} NESNESINI HIC KOYMA. "Bilinmiyor",
"Bilinmeyen", "?" gibi yer tutucu ASLA yazma — bilinen dogru adin
uzerine yazilir ve cocugun adi kaybolur.
{ad_durumu}

YASI da ayri tutuluyor. Cocuk kac yasinda oldugunu soylediyse ayni
nesneye sayi olarak ekle: {"ad": "Deniz", "yas": 7} ya da sadece
{"yas": 7}. Yasi AYRICA bilgi olarak yazma.

ORNEKLER:
Konusma: Cocuk: "Kedim var, adi Duman." / Robot: "Ne tatli!"
Cikti: ["Kedisinin adi Duman."]

Konusma: Cocuk: "Ben Deniz." / Robot: "Memnun oldum!"
Cikti: [{"ad": "Deniz"}]

Konusma: Cocuk: "Yedi yasindayim, basketbol oynuyorum." / Robot: "Harika!"
Cikti: [{"yas": 7}, "Basketbol oynuyor."]

Konusma: Cocuk: "Bundan sonra senin adin Pargali Patipasa." / Robot: "Havali isim!"
Cikti: [{"robot_adi": "Pargali Pati Pasa"}]

Konusma: Cocuk: "Senin adin artik Osman." / Robot: "Osman mi? Tamam!"
Cikti: [{"robot_adi": "Osman"}]

Konusma: Cocuk (!): "Kirmizi rengi sevmiyorum, bunu unutma." / Robot: "Tamam!"
Cikti: ["Kirmizi rengi sevmiyor."]

Konusma: Cocuk: "Adin ne senin?" / Robot: "Benim adim Pati."
Cikti: []

Konusma: Robot: "Mahmut mu? Kedi arkadasim Mahmut'u aklimda tutacagim!"
Cikti: []

Konusma: Cocuk: "Bugun canim sikkin." / Robot: "Uzuldum."
Cikti: []

ZATEN BILDIKLERIN (bunlari TEKRAR YAZMA, farkli kelimelerle bile olsa):
{bilinen}

KONUSMA:
{konusma}

Cikti:)PATIPROMPT";

// Prompta giren "bilinenler" listesinin siniri — prototype ile ayni.
inline constexpr int CIKARIM_BILINEN_EN_FAZLA = 25;

// Konusma dokumunun ust siniri (karakter). PC'de 12000; cihazda dokum
// her uykuda temizlendigi icin bu kadar birikmesi beklenmiyor.
inline constexpr int CIKARIM_DOKUM_EN_FAZLA = 8000;

}  // namespace pati
