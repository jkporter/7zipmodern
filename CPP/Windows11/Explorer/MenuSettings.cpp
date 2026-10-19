// MenuSettings.cpp

#include "StdAfx.h"

#include <ShlObj_core.h>

#include "MenuSettings.h"

#include "../../Windows/FileIO.h"

#include "AppLaunch.h"

using namespace NWindows;
using namespace NFile;

bool CMenuSettings::Load(CFSTR path)
{
  NIO::CInFile file;
  // the app can rewrite the file at any time
  if (!file.Open(path, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, 0))
    return false;
  wchar_t buf[2048];
  size_t size = 0;
  if (!file.ReadFull(buf, sizeof(buf) - sizeof(wchar_t), size) || size < 2)
    return false;
  const wchar_t *p = buf;
  unsigned num = (unsigned)(size / sizeof(wchar_t));
  if (num != 0 && p[0] == 0xFEFF)
  {
    p++;
    num--;
  }
  UString s;
  s.SetFrom(p, num);
  _text = L"\n";
  _text += s;
  _text.RemoveChar(L'\r');
  return true;
}


void CMenuSettings::Read()
{
  _text.Empty();
  WCHAR localAppData[MAX_PATH];
  if (FAILED(::SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData)))
    return;
  UString pfn;
  if (GetPackageFamilyName(pfn) && !pfn.IsEmpty())
  {
    UString path (localAppData);
    path += "\\Packages\\";
    path += pfn;
    path += "\\LocalState\\contextmenu.ini";
    if (Load(us2fs(path)))
      return;
  }
  UString path (localAppData);
  path += "\\7-Zip WinUI\\contextmenu.ini";
  Load(us2fs(path));
}


bool CMenuSettings::IsOn(const char *key) const
{
  UString needle ('\n');
  needle += key;
  needle.Add_Char('=');
  const int pos = _text.Find(needle);
  if (pos < 0)
    return true;
  return *_text.Ptr((unsigned)pos + needle.Len()) != '0';
}
