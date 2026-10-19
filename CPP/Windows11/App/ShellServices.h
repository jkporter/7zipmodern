// ShellServices.h

#ifndef ZIP7_INC_SHELL_SERVICES_H
#define ZIP7_INC_SHELL_SERVICES_H

#include "../../Common/MyString.h"

/*
Real Explorer icons and type names for items inside archives, looked up by
extension (SHGetFileInfo with SHGFI_USEFILEATTRIBUTES) and cached.
UI thread only (the icons are WriteableBitmaps). Returns nullptr if there is no icon.
*/

namespace NShellIcons
{
  winrt::Microsoft::UI::Xaml::Media::ImageSource GetFolderIcon();
  winrt::Microsoft::UI::Xaml::Media::ImageSource GetIcon(const UString &fileName);
  // icon of a real file on disk (archives in the recent list)
  winrt::Microsoft::UI::Xaml::Media::ImageSource GetIconForPath(const UString &path);
  UString GetTypeName(const UString &fileName, bool isFolder);
}

namespace NShellActions
{
  // Opens File Explorer with the items selected (instead of just opening the folder).
  void RevealInExplorer(const UStringVector &paths);
  void RevealInExplorer(const UString &path);
  void OpenFolder(const UString &folder);
  // opens a file with its associated app
  void Launch(const UString &path);
  // the system "Open with" picker
  void OpenWith(HWND hwnd, const UString &path);
  // feeds Explorer's Recent list and the jump list's "Recent" category
  void AddToRecentDocuments(const UString &path);
  // moves files to the Recycle Bin with IFileOperation (undoable)
  HRESULT Recycle(const UStringVector &paths, HWND owner);
  bool IsPackaged();
}

#endif
