// ExtractOptionsModel.cpp

#include "StdAfx.h"

#include "ExtractOptionsModel.h"

#include "../HString.h"

#include "App.h"

using namespace winrt;
using namespace NWindows11;

static FString CombinePath(const FString &folder, const UString &name)
{
  FString s (folder);
  if (!s.IsEmpty() && !IS_PATH_SEPAR(s.Back()))
    s.Add_PathSepar();
  s += us2fs(name);
  return s;
}

static bool IsSeparator(wchar_t c) { return c == L'\\' || c == L'/'; }

// .NET Path.IsPathFullyQualified: "C:\..." or "\\server\..." / "\\?\..."
static bool IsPathFullyQualified(const UString &path)
{
  if (path.Len() >= 2 && IsSeparator(path[0]) && IsSeparator(path[1]))
    return true;
  if (path.Len() < 3)
    return false;
  const wchar_t c = path[0];
  return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) && path[1] == ':' && IsSeparator(path[2]);
}

static int UnboxInt(const CInspectable &v, int defaultValue)
{
  return unbox_value_or<int32_t>(v, defaultValue);
}

static bool UnboxBool(const CInspectable &v)
{
  return unbox_value_or<bool>(v, false);
}

CExtractOptionsModel::CExtractOptionsModel(const FString &parentFolder, const UString &folderName,
    bool suggestSubfolder, bool needsPassword):
    _parentFolder(parentFolder),
    _folderName(folderName),
    _createSubfolder(suggestSubfolder),
    _pathModeIndex(0),
    _restoreTimestamps(true),
    _needsPassword(needsPassword)
{
  const CAppSettings &s = g_App.Settings.Current;
  _destination = fs2us(suggestSubfolder ? GetPathWithFolder() : parentFolder);
  _overwriteIndex = (int)s.OverwriteMode;
  _openFolderWhenDone = s.OpenFolderAfterExtract;
  _zoneIndex = (int)s.ZonePropagation;
  _restoreSymbolicLinks = s.RestoreSymbolicLinks;

  AddProperty(L"Destination", [this] { return BoxString(_destination); },
      [this](const CInspectable &v) { SetDestination(UnboxString(v)); });
  AddProperty(L"CreateSubfolder", [this] { return box_value(_createSubfolder); },
      [this](const CInspectable &v) { SetCreateSubfolder(UnboxBool(v)); });
  AddProperty(L"OverwriteIndex", [this] { return box_value((int32_t)_overwriteIndex); },
      [this](const CInspectable &v) { _overwriteIndex = UnboxInt(v, _overwriteIndex); });
  AddProperty(L"PathModeIndex", [this] { return box_value((int32_t)_pathModeIndex); },
      [this](const CInspectable &v) { _pathModeIndex = UnboxInt(v, _pathModeIndex); });
  AddProperty(L"OpenFolderWhenDone", [this] { return box_value(_openFolderWhenDone); },
      [this](const CInspectable &v) { _openFolderWhenDone = UnboxBool(v); });
  AddProperty(L"ZoneIndex", [this] { return box_value((int32_t)_zoneIndex); },
      [this](const CInspectable &v) { _zoneIndex = UnboxInt(v, _zoneIndex); });
  AddProperty(L"RestoreTimestamps", [this] { return box_value(_restoreTimestamps); },
      [this](const CInspectable &v) { _restoreTimestamps = UnboxBool(v); });
  AddProperty(L"RestoreSymbolicLinks", [this] { return box_value(_restoreSymbolicLinks); },
      [this](const CInspectable &v) { _restoreSymbolicLinks = UnboxBool(v); });
  AddProperty(L"Password", [this] { return BoxString(_password); },
      [this](const CInspectable &v) { _password = UnboxString(v); });
  AddProperty(L"NeedsPassword", [this] { return box_value(_needsPassword); });
  AddProperty(L"SubfolderLabel", [this]
  {
    UString s (L"Extract into a new folder named \x201C");
    s += _folderName;
    s += L"\x201D";
    return BoxString(s);
  });
}

FString CExtractOptionsModel::GetPathWithFolder() const
{
  return CombinePath(_parentFolder, _folderName);
}

void CExtractOptionsModel::SetDestination(const UString &value)
{
  if (_destination == value)
    return;
  _destination = value;
  RaisePropertyChanged(L"Destination");
}

void CExtractOptionsModel::SetCreateSubfolder(bool value)
{
  if (_createSubfolder == value)
    return;
  _createSubfolder = value;
  // Keep the destination in sync while the user hasn't typed a custom location.
  const UString withFolder = fs2us(GetPathWithFolder());
  if (value && _destination.IsEqualTo_NoCase(fs2us(_parentFolder)))
    SetDestination(withFolder);
  else if (!value && _destination.IsEqualTo_NoCase(withFolder))
    SetDestination(fs2us(_parentFolder));
  RaisePropertyChanged(L"CreateSubfolder");
}

void CExtractOptionsModel::ToOptions(const UString &basePath, CExtractOptions &options) const
{
  options = CExtractOptions();
  options.Destination = us2fs(_destination);
  options.BasePath = basePath;
  options.Overwrite = (NOverwriteMode::EEnum)_overwriteIndex;
  options.PathMode = _pathModeIndex == 1 ? NExtractPathMode::kNoPaths : NExtractPathMode::kFullPaths;
  options.ZonePropagation = (NZonePropagation::EEnum)_zoneIndex;
  options.RestoreTimestamps = _restoreTimestamps;
  options.RestoreSymbolicLinks = _restoreSymbolicLinks;
  options.Password_Defined = _needsPassword && !_password.IsEmpty();
  if (options.Password_Defined)
    options.Password = _password;
}

UString CExtractOptionsModel::Validate() const
{
  UString d (_destination);
  d.Trim();
  if (d.IsEmpty())
    return UString(L"Choose where to extract the files.");
  if (!IsPathFullyQualified(_destination))
    return UString(L"Enter a full folder path, for example C:\\Users\\you\\Downloads.");
  return UString();
}
