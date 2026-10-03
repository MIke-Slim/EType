#pragma once
#include <windows.h>
#include <msctf.h>
inline constexpr GUID ETypeClsid={0xdc168f35,0x18ea,0x4ec5,{0xb3,0x91,0xc4,0x43,0x0c,0x3f,0x3e,0xd9}};
inline constexpr GUID ETypeProfile={0x8b12d042,0xc278,0x4c60,{0xa8,0x86,0x03,0xfc,0x5d,0x14,0x5e,0x6c}};
inline constexpr wchar_t ETypeName[]=L"EType 英文词汇输入法";
inline constexpr LANGID ETypeLanguage=0x0804;
// Private host interface: the preview embeds the same input service in a
// TSF-aware text host without registering a system keyboard profile.
inline constexpr GUID ETypePreviewId={0xc1583597,0x74da,0x45f6,{0xab,0x51,0xda,0x86,0x67,0x32,0x5a,0x01}};
struct IPreviewTextService:public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE InitializeHost(ITfThreadMgr*,TfClientId)=0;
};
