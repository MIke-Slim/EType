#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <msctf.h>
#include <filesystem>
#include <iostream>
#include "identity.h"
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int checks=0,failures=0;auto check=[&](bool value,const char* name){++checks;if(!value){++failures;std::cerr<<"FAIL "<<name<<"\n";}};
    wchar_t clsid[64]{};StringFromGUID2(ETypeClsid,clsid,64);auto key=std::wstring(L"Software\\Classes\\CLSID\\")+clsid+L"\\InprocServer32";
    wchar_t path[32768]{};DWORD bytes=sizeof(path);
    auto status=RegGetValueW(HKEY_LOCAL_MACHINE,key.c_str(),nullptr,RRF_RT_REG_SZ,nullptr,path,&bytes);
    auto expected=std::filesystem::path(argv[1])/(sizeof(void*)==8?L"x64":L"x86")/L"EType.dll";
    check(status==ERROR_SUCCESS&&_wcsicmp(path,expected.c_str())==0,"installed registration path matches process architecture");
    ITfTextInputProcessor* tip=nullptr;auto hr=CoCreateInstance(ETypeClsid,nullptr,CLSCTX_INPROC_SERVER,IID_ITfTextInputProcessor,(void**)&tip);
    check(SUCCEEDED(hr)&&tip,"Windows creates the installed online input service");if(tip)tip->Release();
    ITfInputProcessorProfileMgr* mgr=nullptr;hr=CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_ITfInputProcessorProfileMgr,(void**)&mgr);
    check(SUCCEEDED(hr)&&mgr,"Windows profile manager");
    if(mgr){TF_INPUTPROCESSORPROFILE profile{};hr=mgr->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR,ETypeLanguage,ETypeClsid,ETypeProfile,nullptr,&profile);
        check(SUCCEEDED(hr)&&profile.clsid==ETypeClsid&&profile.guidProfile==ETypeProfile,"installed online TSF profile exists");
        ITfThreadMgr* thread=nullptr;TfClientId id=0;hr=CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_ITfThreadMgr,(void**)&thread);
        if(SUCCEEDED(hr))hr=thread->Activate(&id);
        check(SUCCEEDED(hr),"Windows TSF thread activation");
        if(SUCCEEDED(hr)){hr=mgr->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,ETypeLanguage,ETypeClsid,ETypeProfile,nullptr,ETypeEnableProfile|ETypeProfileForProcess);
            check(SUCCEEDED(hr),"installed online profile activates in this test process");
            TF_INPUTPROCESSORPROFILE active{};hr=mgr->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&active);
            check(SUCCEEDED(hr)&&active.clsid==ETypeClsid&&active.guidProfile==ETypeProfile,"Windows confirms online profile active");}
        if(thread){thread->Deactivate();thread->Release();}mgr->Release();
    }
    std::cout<<"registration_checks="<<checks<<" failures="<<failures<<"\n";CoUninitialize();return failures?1:0;
}
