#pragma once
#include <windows.h>
#include <msctf.h>
#ifdef ETYPE_ONLINE
inline constexpr GUID ETypeClsid={0x2508c9af,0x571f,0x45ac,{0x87,0x09,0x5c,0xd3,0xc0,0xb1,0x43,0x66}};
inline constexpr GUID ETypeProfile={0x078cbb5e,0x8cb6,0x4cae,{0xb5,0xe3,0x65,0x24,0xa3,0x48,0x5a,0x53}};
inline constexpr wchar_t ETypeName[]=L"EType 在线测试版";
#elif defined(ETYPE_SENTENCE_TRIAL_PROFILE)
// Separate preview identity and directory avoid an already mapped word DLL.
inline constexpr GUID ETypeClsid={0xb61c1452,0x3e9a,0x4616,{0x9e,0xa3,0x18,0xb4,0xe5,0x86,0x2c,0xa4}};
inline constexpr GUID ETypeProfile={0xd6b9b4a4,0x8d60,0x4f7e,{0xa6,0x73,0x30,0x1e,0x64,0xc9,0x11,0x58}};
inline constexpr wchar_t ETypeName[]=L"EType 单词与句子";
#else
inline constexpr GUID ETypeClsid={0xdc168f35,0x18ea,0x4ec5,{0xb3,0x91,0xc4,0x43,0x0c,0x3f,0x3e,0xd9}};
inline constexpr GUID ETypeProfile={0x8b12d042,0xc278,0x4c60,{0xa8,0x86,0x03,0xfc,0x5d,0x14,0x5e,0x6c}};
inline constexpr wchar_t ETypeName[]=L"EType 英文词汇输入法";
#endif
inline constexpr LANGID ETypeLanguage=0x0804;
// Windows SDK activation flags (not declared by this MinGW header).
inline constexpr DWORD ETypeEnableProfile=0x00000001;
inline constexpr DWORD ETypeProfileForProcess=0x10000000;
inline constexpr DWORD ETypeProfileForSession=0x20000000;
// Private host interface: the preview embeds the same input service in a
// TSF-aware text host without registering a system keyboard profile.
inline constexpr GUID ETypePreviewId={0xc1583597,0x74da,0x45f6,{0xab,0x51,0xda,0x86,0x67,0x32,0x5a,0x01}};
struct IPreviewTextService:public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE InitializeHost(ITfThreadMgr*,TfClientId)=0;
};
inline constexpr GUID ETypeOnlineStateId={0x58e23588,0x52a7,0x4882,{0xb0,0x1b,0x67,0xc8,0xb7,0x6b,0x44,0xe2}};
struct IOnlinePreviewState:public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetState(DWORD* flags,UINT* count)=0;
    virtual HRESULT STDMETHODCALLTYPE FindCorrection(LPCSTR word,INT* index)=0;
};
