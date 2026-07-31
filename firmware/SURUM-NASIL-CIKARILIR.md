# Yeni sürüm nasıl çıkarılır

**Tek dosya değiştir, push et. Bitti.**

```
firmware/surum.txt
```

```
2.3.0
Gözler daha akıcı, uyku sayacı düzeltildi.
```

**İlk satır** sürüm numarası — `2.3.0` gibi üç sayı, başka biçim olmaz.
**Altındaki satırlar** annenin panelde okuyacağı not.

Sonra normal işini yap:

```
git add -A
git commit -m "..."
git push
```

Gerisini GitHub yapıyor: firmware'i kendi sunucusunda derliyor, bir
Release açıyor, içine `pati.bin` ile `surum.json` koyuyor. Beş on dakika
sürüyor.

Bitince anne panelden **"Güncellemeleri kontrol et"** deyince yeni
sürümü görüyor.

---

## Nasıl gittiğini nereden görürsün

Depo sayfasında **Actions** sekmesi. Yeşil tik = yayımlandı. Kırmızı
çarpı = bir şey ters gitti ve **hiçbir şey yayımlanmadı** — anne yanlış
bir şey görmez, eski sürümde kalır.

---

## Sürümü yükseltmeyi unutursan

Hiçbir şey olmaz. İş akışı yalnızca `surum.txt` değişince çalışıyor;
başka dosyaları push etmek Release açmıyor. Yani kod değişikliğini push
edip sürümü unutursan, **anne güncellemeyi görmez** — sessizce yanlış
bir şey yüklenmez.

Fark ettiğinde `surum.txt`'yi yükseltip push etmen yeterli.

---

## Aynı sürüm numarasını iki kez kullanırsan

İş akışı daha derlemeye başlamadan durur ve *"v2.3.0 zaten var"* der.
Yükselt, tekrar push et.

---

## Bir şey ters giderse

**Derleme kırmızı yandı.** Actions sekmesinde hangi adımda durduğu
yazıyor. En sık iki sebep:

- `surum.txt`'nin ilk satırı `2.3.0` biçiminde değil
- İlk satırın altında not yok — anneye gösterilecek metin boş kalamaz

**Anne güncelledi ama Pati açılmadı.** Kendiliğinden eski sürüme döner
(`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`). Yeni yapı, ağ katmanı ve
panel ayağa kalkana kadar "deneme" sayılıyor; oraya varamayan bir yapı
bir sonraki açılışta geri alınıyor. Kablo gerekmez.

**İndirme yarıda kaldı.** Çalışan sürüme dokunulmuyor — indirme boştaki
bölüme yazılıyor. Pati eski sürümde çalışmaya devam eder, anne tekrar
deneyebilir.

---

## Neden `surum.json` artık depoda değil

Depodaydı ve sessiz bir kusuru vardı: push, manifesti **anında** yayına
sokuyor ama `pati.bin`'i oluşturmuyor. Derleme bitene kadar geçen
dakikalarda panel "güncelleme var" der, anne basar, indirme 404'e
düşerdi.

Şimdi ikisi de aynı Release'in içinde. Manifest, tarif ettiği dosyadan
**önce var olamıyor** — dikkatle değil, yapısı gereği.

---

## Kabloyla yükleme ne zaman gerekir

Neredeyse hiç. İki durum dışında:

1. **Bölüm tablosu değişirse** (`firmware/partitions.csv`). OTA yalnızca
   uygulama bölümüne yazıyor, tabloya dokunmuyor.
2. **Robot açılamayacak kadar bozulursa** — geri alma bunu zaten
   karşılıyor, ama geri alınacak sağlam bir sürüm de yoksa.

İkisi de `firmware/KARTA-YUKLE.bat` ile. `app-flash` yetmez, tam
`flash` gerekir.
