// Standalone integration test: production Config.cpp + installed Menu Framework.
// CPU ImGui frames and real mouse events, not a replacement button return value.
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "../build/reference-l/MenuFramework/include/cimgui.h"
#include <cassert>
#include <cstdio>
#include <cstring>

template<class T> T Export(HMODULE module,const char* name){
    auto proc=GetProcAddress(module,name);if(!proc){std::printf("Missing %s\n",name);std::abort();}
    return reinterpret_cast<T>(proc);
}
static decltype(Settings::Menu::button) realButton;
static void (*getMin)(ImVec2*);
static void (*getMax)(ImVec2*);
static ImVec2 savePoint,readPoint;
static unsigned clicked=0;
static bool TrackButton(const char* label,Settings::Menu::Vec2 size){
    const bool result=realButton(label,size);
    if(std::strcmp(label,"保存并应用")==0){
        ImVec2 lo,hi;getMin(&lo);getMax(&hi);savePoint={(lo.x+hi.x)/2,(lo.y+hi.y)/2};
        if(result)++clicked;
    }
    if(std::strcmp(label,"从 INI 重新读取并应用")==0){ImVec2 lo,hi;getMin(&lo);getMax(&hi);readPoint={(lo.x+hi.x)/2,(lo.y+hi.y)/2};}
    return result;
}
int wmain(int argc,wchar_t** argv){
    assert(argc==2);
    auto dll=LoadLibraryW(argv[1]);if(!dll){std::printf("LoadLibrary error %lu\n",GetLastError());return 2;}
    auto version=Export<const char*(*)()>(dll,"igGetVersion");
    std::printf("Actual Framework ImGui %s\n",version());std::fflush(stdout);
    assert(std::strcmp(version(),"1.90.8")==0);
    auto create=Export<ImGuiContext*(*)(ImFontAtlas*)>(dll,"igCreateContext");create(nullptr);
    auto io=Export<ImGuiIO*(*)()>(dll,"igGetIO")();
    io->DisplaySize={1600,1400};io->DeltaTime=1.f/60;io->IniFilename=nullptr;
    unsigned char* pixels;int w,h,bpp;
    Export<void(*)(ImFontAtlas*,unsigned char**,int*,int*,int*)>(dll,"ImFontAtlas_GetTexDataAsRGBA32")(io->Fonts,&pixels,&w,&h,&bpp);
    auto frame=Export<void(*)()>(dll,"igNewFrame");auto endFrame=Export<void(*)()>(dll,"igEndFrame");
    auto begin=Export<bool(*)(const char*,bool*,int)>(dll,"igBegin");auto end=Export<void(*)()>(dll,"igEnd");
    auto windowSize=Export<void(*)(ImVec2,int)>(dll,"igSetNextWindowSize");
    auto pos=Export<void(*)(ImGuiIO*,float,float)>(dll,"ImGuiIO_AddMousePosEvent");
    auto mouse=Export<void(*)(ImGuiIO*,int,bool)>(dll,"ImGuiIO_AddMouseButtonEvent");
    // Isolated working directory, never writes the game's INI.
    std::filesystem::create_directories("build/menu-runtime/Data/SKSE/Plugins");
    std::filesystem::current_path("build/menu-runtime");
    Settings::Initialize();
    auto initial=Settings::Get();initial.logLevel=4;initial.npcScale=1.9f;initial.speedMultiplier=1.6f;Settings::Set(initial);
#define MENU_API(field,name) Settings::Menu::field=Export<decltype(Settings::Menu::field)>(dll,name)
    MENU_API(text,"igTextUnformatted");MENU_API(slider,"igSliderFloat");MENU_API(checkbox,"igCheckbox");
    MENU_API(button,"igButton");MENU_API(inputInt,"igInputInt");MENU_API(separator,"igSeparatorText");
#undef MENU_API
    realButton=Settings::Menu::button;Settings::Menu::button=TrackButton;
    getMin=Export<decltype(getMin)>(dll,"igGetItemRectMin");getMax=Export<decltype(getMax)>(dll,"igGetItemRectMax");
    for(int i=0;i<202;++i){
        if(i>=2){const int step=(i-2)%4;const auto p=step<2?savePoint:readPoint;pos(io,p.x,p.y);mouse(io,0,step%2==0);}
        frame();windowSize({1400,1300},1);begin("NPCMounts integration",nullptr,0);
        Settings::Menu::Render();end();endFrame();
    }
    assert(clicked==50);
    bool read=false;auto loaded=Settings::Read(Settings::iniPath,&read);assert(read&&loaded.logLevel==4&&loaded.npcScale==1.9f&&loaded.speedMultiplier==1.6f);
    std::printf("PASS: %u real save clicks plus reload through production Render with installed DLL; LogLevel and user scale/speed retained. Not in-game QA.\n",clicked);
}
