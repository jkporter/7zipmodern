// ShellServices.cpp

#include "StdAfx.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4668) // 'NTDDI_WIN10_GA' is not defined as a preprocessor macro
#endif
#include <appmodel.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <ShlObj_core.h>
#include <ShObjIdl_core.h>
#include <shellapi.h>

#include "ShellServices.h"

#include "../../Common/MyBuffer.h"
#include "../../Common/MyCom.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml::Media;

namespace NShellIcons {

struct CIconItem
{
  UString Key;
  ImageSource Icon { nullptr };
};

struct CTypeItem
{
  UString Key;
  UString Name;
};

static CObjectVector<CIconItem> g_Icons;
static CObjectVector<CTypeItem> g_Types;
static const wchar_t * const kFolderKey = L"<folder>";

static UString GetExtension(const UString &name)
{
  const int slash = name.ReverseFind_PathSepar();
  const int dot = name.ReverseFind_Dot();
  if (dot <= slash)
    return UString();
  return UString(name.Ptr((unsigned)dot));
}

static ImageSource ToBitmap(HICON hIcon)
{
  ICONINFO ii;
  if (!::GetIconInfo(hIcon, &ii))
    return nullptr;
  ImageSource result { nullptr };
  if (ii.hbmColor)
  {
    BITMAP bm;
    if (::GetObjectW(ii.hbmColor, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0)
    {
      const int w = bm.bmWidth, h = bm.bmHeight;
      const size_t size = (size_t)w * (size_t)h * 4;
      CByteBuffer pixels(size);
      BITMAPINFO bi;
      memset(&bi, 0, sizeof(bi));
      bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
      bi.bmiHeader.biWidth = w;
      bi.bmiHeader.biHeight = -h; // top-down
      bi.bmiHeader.biPlanes = 1;
      bi.bmiHeader.biBitCount = 32;
      bi.bmiHeader.biCompression = BI_RGB;
      const HDC dc = ::CreateCompatibleDC(NULL);
      ::GetDIBits(dc, ii.hbmColor, 0, (UINT)h, pixels, &bi, DIB_RGB_COLORS);

      Byte *p = pixels;
      bool hasAlpha = false;
      for (size_t i = 3; i < size; i += 4)
        if (p[i] != 0)
        {
          hasAlpha = true;
          break;
        }
      if (!hasAlpha && ii.hbmMask)
      {
        CByteBuffer mask(size);
        ::GetDIBits(dc, ii.hbmMask, 0, (UINT)h, mask, &bi, DIB_RGB_COLORS);
        const Byte *m = mask;
        for (size_t i = 0; i < size; i += 4)
          p[i + 3] = (Byte)(m[i] == 0 ? 255 : 0);
      }
      ::DeleteDC(dc);

      // WriteableBitmap expects premultiplied BGRA
      for (size_t i = 0; i < size; i += 4)
      {
        const unsigned a = p[i + 3];
        if (a == 255)
          continue;
        p[i]     = (Byte)(p[i]     * a / 255);
        p[i + 1] = (Byte)(p[i + 1] * a / 255);
        p[i + 2] = (Byte)(p[i + 2] * a / 255);
      }

      Imaging::WriteableBitmap bitmap(w, h);
      const winrt::Windows::Storage::Streams::IBuffer buffer = bitmap.PixelBuffer();
      if (buffer.Capacity() >= size)
      {
        memcpy(buffer.data(), p, size);
        bitmap.Invalidate();
        result = bitmap;
      }
    }
  }
  if (ii.hbmColor)
    ::DeleteObject(ii.hbmColor);
  if (ii.hbmMask)
    ::DeleteObject(ii.hbmMask);
  return result;
}

static ImageSource GetIconCore(const UString &key, const UString &probe, DWORD attributes)
{
  FOR_VECTOR (i, g_Icons)
    if (g_Icons[i].Key.IsEqualTo_NoCase(key))
      return g_Icons[i].Icon;
  ImageSource icon { nullptr };
  try
  {
    SHFILEINFOW info;
    memset(&info, 0, sizeof(info));
    if (::SHGetFileInfoW(probe, attributes, &info, sizeof(info),
          SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES) != 0 && info.hIcon)
    {
      icon = ToBitmap(info.hIcon);
      ::DestroyIcon(info.hIcon);
    }
  }
  catch (...)
  {
    icon = nullptr;
  }
  CIconItem &item = g_Icons.AddNew();
  item.Key = key;
  item.Icon = icon;
  return icon;
}

ImageSource GetFolderIcon()
{
  return GetIconCore(UString(kFolderKey), UString(L"folder"), FILE_ATTRIBUTE_DIRECTORY);
}

ImageSource GetIcon(const UString &fileName)
{
  // Executables and shortcuts carry their own icons; use the generic one for the extension.
  const UString ext = GetExtension(fileName);
  UString probe (L"file");
  probe += ext;
  return GetIconCore(ext.IsEmpty() ? UString(L"<none>") : ext, probe, FILE_ATTRIBUTE_NORMAL);
}

ImageSource GetIconForPath(const UString &path)
{
  return GetIcon(path);
}

UString GetTypeName(const UString &fileName, bool isFolder)
{
  const UString key = isFolder ? UString(kFolderKey) : GetExtension(fileName);
  FOR_VECTOR (i, g_Types)
    if (g_Types[i].Key.IsEqualTo_NoCase(key))
      return g_Types[i].Name;
  UString name;
  SHFILEINFOW info;
  memset(&info, 0, sizeof(info));
  UString probe (isFolder ? L"folder" : L"file");
  if (!isFolder)
    probe += key;
  if (::SHGetFileInfoW(probe, isFolder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL,
        &info, sizeof(info), SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES) != 0)
    name = info.szTypeName;
  if (name.IsEmpty())
  {
    if (isFolder)
      name = L"File folder";
    else if (key.Len() > 1)
    {
      name = key.Ptr(1);
      ::CharUpperBuffW(name.GetBuf(), name.Len());
      name.ReleaseBuf_SetLen(name.Len());
      name += " File";
    }
    else
      name = L"File";
  }
  CTypeItem &item = g_Types.AddNew();
  item.Key = key;
  item.Name = name;
  return name;
}

}


namespace NShellActions {

static UString GetDirPart(const UString &path)
{
  UString s (path);
  while (s.Len() > 3 && IS_PATH_SEPAR(s.Back()))
    s.DeleteBack();
  const int slash = s.ReverseFind_PathSepar();
  if (slash < 0)
    return UString();
  s.DeleteFrom((unsigned)(slash == 2 && s[1] == ':' ? 3 : slash));
  return s;
}

void RevealInExplorer(const UStringVector &paths)
{
  if (paths.IsEmpty())
    return;
  const UString folder = GetDirPart(paths[0]);
  if (folder.IsEmpty())
    return;
  PIDLIST_ABSOLUTE folderPidl = ::ILCreateFromPathW(folder);
  if (!folderPidl)
    return;
  CRecordVector<PIDLIST_ABSOLUTE> items;
  FOR_VECTOR (i, paths)
  {
    if (!GetDirPart(paths[i]).IsEqualTo_NoCase(folder))
      continue;
    PIDLIST_ABSOLUTE p = ::ILCreateFromPathW(paths[i]);
    if (p)
      items.Add(p);
  }
  ::SHOpenFolderAndSelectItems(folderPidl, items.Size(),
      items.IsEmpty() ? NULL : (PCUITEMID_CHILD_ARRAY)(const void *)&items[0], 0);
  ::ILFree(folderPidl);
  FOR_VECTOR (i, items)
    ::ILFree(items[i]);
}

void RevealInExplorer(const UString &path)
{
  UStringVector v;
  v.Add(path);
  RevealInExplorer(v);
}

void OpenFolder(const UString &folder)
{
  ::ShellExecuteW(NULL, L"open", folder, NULL, NULL, SW_SHOWNORMAL);
}

void Launch(const UString &path)
{
  const UString dir = GetDirPart(path);
  ::ShellExecuteW(NULL, NULL, path, NULL, dir.IsEmpty() ? NULL : dir.Ptr(), SW_SHOWNORMAL);
}

void OpenWith(HWND hwnd, const UString &path)
{
  OPENASINFO info;
  memset(&info, 0, sizeof(info));
  info.pcszFile = path;
  info.oaifInFlags = OAIF_EXEC | OAIF_ALLOW_REGISTRATION;
  ::SHOpenWithDialog(hwnd, &info);
}

void AddToRecentDocuments(const UString &path)
{
  ::SHAddToRecentDocs(SHARD_PATHW, path.Ptr());
}

HRESULT Recycle(const UStringVector &paths, HWND owner)
{
  CMyComPtr<IFileOperation> op;
  RINOK(::CoCreateInstance(CLSID_FileOperation, NULL, CLSCTX_ALL, IID_IFileOperation, (void **)&op))
  RINOK(op->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOFX_RECYCLEONDELETE))
  if (owner)
    op->SetOwnerWindow(owner);
  unsigned count = 0;
  FOR_VECTOR (i, paths)
  {
    CMyComPtr<IShellItem> item;
    if (FAILED(::SHCreateItemFromParsingName(paths[i], NULL, IID_IShellItem, (void **)&item)))
      continue;
    if (SUCCEEDED(op->DeleteItem(item, NULL)))
      count++;
  }
  if (count == 0)
    return S_OK;
  return op->PerformOperations();
}

bool IsPackaged()
{
  UINT32 len = 0;
  return ::GetCurrentPackageFullName(&len, NULL) != APPMODEL_ERROR_NO_PACKAGE;
}

}
