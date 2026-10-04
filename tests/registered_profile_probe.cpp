#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <msctf.h>
#include <filesystem>
#include <fstream>
#include "identity.h"
static std::string escaped(const wchar_t* value){
    int n=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);
    std::string text(n,'\0');WideCharToMultiByte(CP_UTF8,0,value,-1,text.data(),n,nullptr,nullptr);text.pop_back();
    std::string result="\"";for(char ch:text){if(ch=='"'||ch=='\\')result+='\\';result+=ch;}return result+"\"";
}
int wmain(int argc,wchar_t** argv){
    if(argc!=3&&argc!=4)return 2;
    auto init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(init))return 3;
    ITfTextInputProcessor* service=nullptr;
    auto hr=CoCreateInstance(ETypeClsid,nullptr,CLSCTX_INPROC_SERVER,IID_ITfTextInputProcessor,(void**)&service);
    wchar_t path[32768]{};HMODULE module=nullptr;
    if(service){auto table=*reinterpret_cast<void***>(service);
        if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)table[0],&module))GetModuleFileNameW(module,path,32768);}
    ITfInputProcessorProfileMgr* profiles=nullptr;TF_INPUTPROCESSORPROFILE active{};HRESULT activeHr=E_FAIL;
    HRESULT activationHr=S_FALSE;
    unsigned unifiedProfiles=0,legacyProfiles=0;HRESULT enumerationHr=E_FAIL;
    if(SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_ITfInputProcessorProfileMgr,(void**)&profiles))){
        if(argc==4&&wcscmp(argv[3],L"--activate-session")==0){auto layout=LoadKeyboardLayoutW(L"00000804",KLF_NOTELLSHELL);if(layout)ActivateKeyboardLayout(layout,0);
            activationHr=profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,ETypeLanguage,ETypeClsid,ETypeProfile,nullptr,ETypeEnableProfile|ETypeProfileForProcess|ETypeProfileForSession);}
        activeHr=profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&active);
        IEnumTfInputProcessorProfiles* enumeration=nullptr;enumerationHr=profiles->EnumProfiles(ETypeLanguage,&enumeration);
        if(SUCCEEDED(enumerationHr)){TF_INPUTPROCESSORPROFILE profile{};ULONG fetched=0;
            constexpr GUID legacy={0xdc168f35,0x18ea,0x4ec5,{0xb3,0x91,0xc4,0x43,0x0c,0x3f,0x3e,0xd9}};
            while((enumerationHr=enumeration->Next(1,&profile,&fetched))==S_OK&&fetched){if(profile.clsid==ETypeClsid)++unifiedProfiles;if(profile.clsid==legacy)++legacyProfiles;}
            enumeration->Release();}
        profiles->Release();}
    wchar_t activeClsid[40]{},activeProfile[40]{};StringFromGUID2(active.clsid,activeClsid,40);StringFromGUID2(active.guidProfile,activeProfile,40);
    bool activeSentence=SUCCEEDED(activeHr)&&active.clsid==ETypeClsid&&active.guidProfile==ETypeProfile;
    bool passed=SUCCEEDED(hr)&&std::filesystem::path(path)==std::filesystem::path(argv[2])&&(argc==3||(activationHr==S_OK&&activeSentence));
    std::ofstream report{std::filesystem::path(argv[1])};
    report<<"{\"passed\":"<<(passed?"true":"false")<<",\"scope\":\"registered-COM-fresh-process\",\"factory_hr\":"<<(unsigned long)hr<<",\"loaded_module\":"<<escaped(path)<<",\"activation_hr\":"<<(unsigned long)activationHr<<",\"active_profile_hr\":"<<(unsigned long)activeHr<<",\"active_clsid\":"<<escaped(activeClsid)<<",\"active_profile\":"<<escaped(activeProfile)<<",\"sentence_profile_active\":"<<(activeSentence?"true":"false")<<",\"enumeration_hr\":"<<(unsigned long)enumerationHr<<",\"unified_profiles\":"<<unifiedProfiles<<",\"legacy_profiles\":"<<legacyProfiles<<"}";
    if(service)service->Release();CoUninitialize();return passed?0:1;
}
