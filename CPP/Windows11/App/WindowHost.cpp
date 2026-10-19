// WindowHost.cpp

#include "StdAfx.h"

#include "WindowHost.h"

#include "../../Windows/FileFind.h"

#include "../HString.h"

#include "../Core/ArchiveFormat.h"

using namespace winrt;
using namespace winrt::Microsoft::Windows::Storage::Pickers;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;
using namespace NWindows;
using namespace NWindows11;

namespace NPickers {

static IVectorView<hstring> EmptyPaths()
{
  return single_threaded_vector<hstring>().GetView();
}

CPathsOperation PickFilesAsync(Microsoft::UI::WindowId window, UString title, UStringVector filter)
{
  FileOpenPicker picker(window);
  picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
  picker.ViewMode(PickerViewMode::List);
  if (!title.IsEmpty())
    picker.Title(ToHString(title));
  if (filter.IsEmpty())
    picker.FileTypeFilter().Append(L"*");
  else
    FOR_VECTOR (i, filter)
      picker.FileTypeFilter().Append(ToHString(filter[i]));
  const auto results = co_await picker.PickMultipleFilesAsync();
  if (!results)
    co_return EmptyPaths();
  std::vector<hstring> paths;
  for (const PickFileResult &r: results)
    paths.push_back(r.Path());
  co_return single_threaded_vector<hstring>(std::move(paths)).GetView();
}

IAsyncOperation<hstring> PickArchiveAsync(Microsoft::UI::WindowId window)
{
  FileOpenPicker picker(window);
  picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
  picker.ViewMode(PickerViewMode::List);
  picker.Title(L"Open archive");
  UStringVector exts;
  NFormatRegistry::GetAllExtensions(exts);
  std::vector<hstring> archives;
  FOR_VECTOR (i, exts)
  {
    if (archives.size() >= 250)
      break;
    UString e (L".");
    e += exts[i];
    archives.push_back(ToHString(e));
  }
  picker.FileTypeChoices().Insert(L"Archives", single_threaded_vector<hstring>(std::move(archives)));
  picker.FileTypeChoices().Insert(L"All files", single_threaded_vector<hstring>({ hstring(L"*") }));
  const PickFileResult result = co_await picker.PickSingleFileAsync();
  co_return result ? result.Path() : hstring();
}

CPathsOperation PickFoldersAsync(Microsoft::UI::WindowId window, UString title)
{
  FolderPicker picker(window);
  picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
  if (!title.IsEmpty())
    picker.Title(ToHString(title));
  const auto results = co_await picker.PickMultipleFoldersAsync();
  if (!results)
    co_return EmptyPaths();
  std::vector<hstring> paths;
  for (const PickFolderResult &r: results)
    paths.push_back(r.Path());
  co_return single_threaded_vector<hstring>(std::move(paths)).GetView();
}

IAsyncOperation<hstring> PickFolderAsync(Microsoft::UI::WindowId window, UString start, UString title)
{
  FolderPicker picker(window);
  picker.SuggestedStartLocation(PickerLocationId::Downloads);
  if (!start.IsEmpty() && NFile::NFind::DoesDirExist_FollowLink(us2fs(start)))
    picker.SuggestedStartFolder(ToHString(start));
  if (!title.IsEmpty())
    picker.Title(ToHString(title));
  const PickFolderResult result = co_await picker.PickSingleFolderAsync();
  co_return result ? result.Path() : hstring();
}

IAsyncOperation<hstring> PickSaveAsync(Microsoft::UI::WindowId window, UString suggestedPath, UString typeLabel, UString extension)
{
  FileSavePicker picker(window);
  const int slash = suggestedPath.ReverseFind_PathSepar();
  picker.SuggestedFileName(ToHString(UString(suggestedPath.Ptr((unsigned)(slash + 1)))));
  UString ext (L".");
  ext += extension;
  picker.DefaultFileExtension(ToHString(ext));
  if (slash > 0)
  {
    UString dir (suggestedPath);
    dir.DeleteFrom((unsigned)slash);
    if (NFile::NFind::DoesDirExist_FollowLink(us2fs(dir)))
      picker.SuggestedStartFolder(ToHString(dir));
  }
  picker.FileTypeChoices().Insert(ToHString(typeLabel), single_threaded_vector<hstring>({ ToHString(ext) }));
  const PickFileResult result = co_await picker.PickSaveFileAsync();
  co_return result ? result.Path() : hstring();
}

void ToUStringVector(const IVectorView<hstring> &paths, UStringVector &result)
{
  result.Clear();
  if (!paths)
    return;
  for (const hstring &p: paths)
    if (!p.empty())
      result.Add(ToUString(p));
}

}
