// SevenZipEngine.h

#ifndef ZIP7_INC_SEVEN_ZIP_ENGINE_H
#define ZIP7_INC_SEVEN_ZIP_ENGINE_H

#include "../../Common/MyCom.h"
#include "../../Common/MyString.h"

#include "../../Windows/PropVariant.h"

#include "../../7zip/Archive/IArchive.h"
#include "../../7zip/ICoder.h"

/*
Loads 7z.dll (built unchanged from CPP/7zip/Bundles/Format7zF) and exposes its flat exports.
The engine is used exactly as 7-Zip's own front ends use it: through CreateObject and the
IInArchive / IOutArchive / IHashers interfaces (see CPP/7zip/UI/Client7z/Client7z.cpp).
All functions are thread-safe; the first call loads the engine.
*/

namespace NSevenZipEngine {

// Searches (explicitPath), then "7z.dll" and "Engine\7z.dll" next to the application.
// Returns HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND) and a message if the engine was not found.
HRESULT EnsureLoaded(const FString &explicitPath, UString &errorMessage);
HRESULT EnsureLoaded();

bool IsLoaded();
FString GetLoadedPath();

HRESULT GetNumberOfFormats(UInt32 &numFormats);
HRESULT GetHandlerProperty(UInt32 formatIndex, PROPID propID, NWindows::NCOM::CPropVariant &value);
HRESULT GetNumberOfMethods(UInt32 &numMethods);
HRESULT GetMethodProperty(UInt32 index, PROPID propID, NWindows::NCOM::CPropVariant &value);

// Engine version as reported by GetModuleProp(kVersion). Returns false if not available.
bool GetEngineVersion(UInt32 &major, UInt32 &minor);

/* Runs the handler's quick signature probe (GetIsArc).
   Returns false when the handler has no probe.
   result: k_IsArc_Res_NO (0) = not this format, k_IsArc_Res_YES (1), k_IsArc_Res_NEED_MORE (2). */
bool ProbeIsArc(UInt32 formatIndex, const Byte *header, size_t size, UInt32 &result);

HRESULT CreateObject(const GUID &classId, const GUID &iid, void **outObject);
HRESULT CreateInArchive(const GUID &classId, CMyComPtr<IInArchive> &archive);
HRESULT CreateOutArchive(const GUID &classId, CMyComPtr<IOutArchive> &archive);
HRESULT GetHashers(CMyComPtr<IHashers> &hashers);

}

#endif
