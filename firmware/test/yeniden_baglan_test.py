"""Gerçek toparlama gövdesini sahte istemciyle MSVC üzerinde sınar."""
from pathlib import Path
import subprocess
import os

root = Path(__file__).resolve().parents[1]
source = (root / 'main/pati_sohbet.cpp').read_text(encoding='utf-8')
begin = source.index('void kopmayi_toparla()')
end = source.index('\n// ---------------------------------------------------------------------------', begin)
function = source[begin:end]
begin = source.index('void yenileme_gerekirse()')
end = source.index('\n}', begin) + 2
renew = source[begin:end]
begin = source.index('        if (olay.state == ConversationState::Listening && g_deneme > 0')
end = source.index('        // Gemini Speaking', begin)
ready = source[begin:end]
out = root / 'build/yeniden-baglan-test'
out.mkdir(parents=True, exist_ok=True)
cpp = r'''
#include <algorithm>
#include <cstdint>
#include <cassert>
#include <cstdio>
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
enum class ConversationState { Connecting, Listening, Error };
bool g_koptu=true, g_calisiyor=true, g_yenileniyor=false, g_uykuda=false;
bool g_konusuyor=false, g_goaway=false;
std::uint32_t g_deneme=0, g_kopma_sayisi=0;
std::int64_t g_son_deneme_us=0, now=0;
constexpr int GERI_CEKILME_TABAN_MS=2000, GERI_CEKILME_EN_COK_MS=30000;
int g_ayar=0, starts=0, successes=0;
bool synchronous_failure=false, immediate_error=false;
bool changed=false;
bool ayar_yenileme_gerekli() { return changed; }
void* acik_tur() { return nullptr; }
std::int64_t esp_timer_get_time() { return now; }
void tur_kapat() {} void beden_konusma_bildir(bool) {}
void kullanim_duraklat() {} void prompt_kur() {}
void anahtar_baglanti_hatasi() {} void gozler_durum(const char*) {}
void ayar_yenileme_temizle() { changed=false; } void anahtar_kod_bildir(int) { ++successes; }
void kullanim_devam() {} void bekci_sifirla() {} void gozler_dinliyor() {}
struct Result { bool has_value() const { return !synchronous_failure; } };
struct Client {
    ConversationState current=ConversationState::Connecting;
    void stop() {}
    Result start(int) {
        ++starts;
        current=ConversationState::Connecting;
        if (immediate_error && !g_yenileniyor && !g_koptu) g_koptu=true;
        return {};
    }
    ConversationState state() { return current; }
} client, *g_istemci=&client;
struct Event { ConversationState state; };
'''
cpp += function + '\n' + renew + '\nvoid ready_event(Event olay) {\n' + ready + '\n}\n'
cpp += r'''
int main() {
    kopmayi_toparla(); assert(starts==1 && g_deneme==1 && successes==0);
    const int delays[]={2000,4000,8000,16000,30000,30000};
    for (int delay: delays) {
        g_koptu=true;
        int before=starts;
        now=g_son_deneme_us+delay*1000LL-1;
        kopmayi_toparla(); assert(starts==before);
        ++now; kopmayi_toparla(); assert(starts==before+1 && successes==0);
    }
    // Kuyrukta kalmış hazır olayı, bağlanmakta olan istemciyi başarılı saymaz.
    ready_event({ConversationState::Listening}); assert(g_deneme>0 && successes==0);
    client.current=ConversationState::Listening;
    ready_event({ConversationState::Listening}); assert(g_deneme==0 && successes==1);
    // Uzun kesinti ve sayaç sınırı: kaydırma taşmadan 30 saniyede kalır.
    for (std::uint32_t count: {31u,32u,33u,100u,UINT32_MAX}) {
        g_deneme=count; g_koptu=true; int before=starts;
        now=g_son_deneme_us+29999999; kopmayi_toparla(); assert(starts==before);
        ++now; kopmayi_toparla(); assert(starts==before+1 && g_deneme>0);
    }
    g_deneme=0; g_koptu=true; synchronous_failure=true;
    kopmayi_toparla(); assert(g_koptu && g_deneme==1);
    synchronous_failure=false; immediate_error=true;
    now=g_son_deneme_us+2000000;
    kopmayi_toparla(); assert(g_koptu && g_deneme==2);
    puts("OK: asenkron hata, gercek hazirlik, geri cekilme, sayac sinirlari");

    // Uyuyan robotta ayar beklemeli; oturumu gizlice açarsa normal
    // uyanma, istemcinin zaten açık olması nedeniyle başarısız olur.
    immediate_error=false; synchronous_failure=false;
    g_koptu=false; g_deneme=0; g_uykuda=true; changed=true;
    int before=starts;
    yenileme_gerekirse();
    if (starts!=before || !changed) { puts("HATA: uykuda ayar oturumu acti"); return 1; }
    g_uykuda=false;
    yenileme_gerekirse();
    assert(starts==before+1 && !changed);

    // Ayar yolundaki eşzamanlı hata da tek toparlama yoluna aktarılmalı.
    changed=true; synchronous_failure=true; g_deneme=0; g_koptu=false;
    before=starts;
    yenileme_gerekirse();
    assert(starts==before+1);
    for(int i=0;i<10;++i) { now+=100000; kopmayi_toparla(); yenileme_gerekirse(); }
    if(starts!=before+1) { puts("HATA: yenileme geri cekilmeyi atladi"); return 1; }
    now=g_son_deneme_us+2000000;
    kopmayi_toparla(); assert(starts==before+2 && g_deneme==2);
    synchronous_failure=false; immediate_error=true; g_koptu=false;
    changed=true;
    yenileme_gerekirse();
    assert(g_koptu && !g_yenileniyor);
    puts("OK: uykuda yenileme ertelendi; ayar hatasi geri cekilmeye uydu");
}
'''
(out / 'test.cpp').write_text(cpp, encoding='utf-8')
vcvars = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/2019/BuildTools/VC/Auxiliary/Build/vcvars64.bat'
# ".\\" ONEKI SART. derle.bat ayni tuzagi zaten yaziyor: bazi
# ortamlarda cmd gecerli klasoru PATH'te aramiyor
# (NoDefaultCurrentDirectoryInExePath) ve "komut bulunamadi" diyor —
# oysa exe orada duruyor. Onek olmadan test DERLENIYOR ama HIC
# CALISMIYOR, yani sessizce hicbir sey sinamiyor.
command = (f'@call "{vcvars}" >nul\r\n'
           '@cl /nologo /std:c++17 /EHsc /utf-8 test.cpp /Fe:test.exe\r\n'
           '@if errorlevel 1 exit /b 1\r\n'
           '@.\\test.exe\r\n')
(out / 'run.bat').write_bytes(command.encode())
subprocess.run(['cmd.exe', '/c', str(out / 'run.bat')], cwd=out, check=True)
