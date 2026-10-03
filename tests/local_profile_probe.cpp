#include <windows.h>
#include <msctf.h>
#include <iostream>
#include <filesystem>
#include "identity.h"
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 auto path=std::filesystem::absolute(argv[1]).wstring();auto dll=LoadLibraryW(path.c_str());
 using Fn=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);auto fn=(Fn)GetProcAddress(dll,"DllGetClassObject");
 IClassFactory* factory=nullptr;fn(ETypeClsid,IID_IClassFactory,(void**)&factory);DWORD cookie=0;
 auto hr=CoRegisterClassObject(ETypeClsid,factory,CLSCTX_INPROC_SERVER,REGCLS_MULTIPLEUSE,&cookie);std::cout<<"class=0x"<<std::hex<<(unsigned long)hr<<"\n";
 ITfThreadMgr* t=nullptr;CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_ITfThreadMgr,(void**)&t);TfClientId id=0;t->Activate(&id);
 auto layout=LoadKeyboardLayoutW(L"00000804",KLF_NOTELLSHELL);ActivateKeyboardLayout(layout,0);
 ITfInputProcessorProfileMgr* p=nullptr;CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_ITfInputProcessorProfileMgr,(void**)&p);
 ITfCategoryMgr* category=nullptr;CoCreateInstance(CLSID_TF_CategoryMgr,nullptr,CLSCTX_INPROC_SERVER,IID_ITfCategoryMgr,(void**)&category);
 auto catHr=category->RegisterCategory(ETypeClsid,GUID_TFCAT_TIP_KEYBOARD,ETypeClsid);std::cout<<"keyboard_category=0x"<<std::hex<<(unsigned long)catHr<<"\n";
 hr=p->RegisterProfile(ETypeClsid,ETypeLanguage,ETypeProfile,ETypeName,(ULONG)wcslen(ETypeName),path.c_str(),(ULONG)path.size(),0,nullptr,0,TRUE,4);std::cout<<"profile=0x"<<std::hex<<(unsigned long)hr<<"\n";
 hr=p->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,ETypeLanguage,ETypeClsid,ETypeProfile,nullptr,0x10000001);std::cout<<"activate=0x"<<std::hex<<(unsigned long)hr<<"\n";
 p->DeactivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,ETypeLanguage,ETypeClsid,ETypeProfile,nullptr,0x10000000);p->Release();
 if(SUCCEEDED(catHr))category->UnregisterCategory(ETypeClsid,GUID_TFCAT_TIP_KEYBOARD,ETypeClsid);category->Release();
 t->Deactivate();t->Release();CoRevokeClassObject(cookie);factory->Release();FreeLibrary(dll);CoUninitialize();return 0;
}
