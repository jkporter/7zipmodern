// CompressViewModel.cpp

#include "StdAfx.h"

#include <stdlib.h>

#include "CompressViewModel.h"

#include "../../Windows/FileFind.h"
#include "../../Windows/FileName.h"
#include "../../Windows/System.h"

#include "../Dispatcher.h"
#include "../HString.h"

#include "../Core/PathSanitizer.h"
#include "../Core/PropertyFormatter.h"

#include "App.h"
#include "DialogService.h"
#include "OperationService.h"
#include "ShellServices.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace NWindows;
using namespace NWindows11;

typedef winrt::Windows::Foundation::IInspectable CObject;

struct CSizeChoice
{
  const wchar_t *Label;
  UInt64 Size;
};

static const CSizeChoice k_VolumeChoices[] =
{
  { L"Don't split", 0 },
  { L"10 MB", (UInt64)10 << 20 },
  { L"100 MB", (UInt64)100 << 20 },
  { L"700 MB (CD)", (UInt64)700 << 20 },
  { L"1 GB", (UInt64)1 << 30 },
  { L"4 GB (FAT32 limit)", (UInt64)4092 << 20 },
  { L"4.7 GB (DVD)", (UInt64)4480 << 20 },
  { L"8.5 GB (DVD DL)", (UInt64)8128 << 20 },
  { L"25 GB (Blu-ray)", (UInt64)23040 << 20 }
};

static const UInt64 k_SolidFull = (UInt64)(Int64)-1;

static const CSizeChoice k_SolidChoices[] =
{
  { L"Automatic", 0 },
  { L"Non-solid (each file separate)", 0 },
  { L"16 MB", (UInt64)16 << 20 },
  { L"64 MB", (UInt64)64 << 20 },
  { L"256 MB", (UInt64)256 << 20 },
  { L"1 GB", (UInt64)1 << 30 },
  { L"4 GB", (UInt64)4 << 30 },
  { L"16 GB", (UInt64)16 << 30 },
  { L"Solid (smallest)", k_SolidFull }
};

static const struct
{
  const wchar_t *Label;
  NUpdateMode::EEnum Mode;
}
k_UpdateModes[] =
{
  { L"Add and replace files", NUpdateMode::kAddAndReplace },
  { L"Update and add files", NUpdateMode::kUpdateAndAdd },
  { L"Freshen existing files", NUpdateMode::kFreshen },
  { L"Synchronize files", NUpdateMode::kSynchronize }
};

static const struct
{
  const wchar_t *Label;
  NCompressPathMode::EEnum Mode;
}
k_PathModes[] =
{
  { L"Relative paths", NCompressPathMode::kRelative },
  { L"Full paths (no drive letter)", NCompressPathMode::kFull }
};

static const struct
{
  const wchar_t *Label;
  const wchar_t *Method;
}
k_ZipEncryptionChoices[] =
{
  { L"AES-256 (recommended)", L"AES256" },
  { L"ZipCrypto (legacy, weak)", L"ZipCrypto" }
};

static const UInt32 k_WordSizes_Ppmd[] = { 2, 3, 4, 6, 8, 10, 12, 16, 20, 24, 32 };
static const UInt32 k_WordSizes_Lz[] = { 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 273 };

static CObject BoxText(const wchar_t *s) { return box_value(hstring(s)); }

static CUiItemVector NewItemVector() { return single_threaded_observable_vector<CObject>(); }

static const wchar_t *GetLevelLabel(NCompressionLevel::EEnum level)
{
  switch (level)
  {
    case NCompressionLevel::kStore: return L"Store (no compression)";
    case NCompressionLevel::kFastest: return L"Fastest";
    case NCompressionLevel::kFast: return L"Fast";
    case NCompressionLevel::kNormal: return L"Normal";
    case NCompressionLevel::kMaximum: return L"Maximum";
    case NCompressionLevel::kUltra:
    default: return L"Ultra (smallest, slowest)";
  }
}

// C# "P0"
static UString FormatPercent(double fraction)
{
  UString s = NPropertyFormatter::FormatDecimal(fraction * 100, 0);
  s += L'%';
  return s;
}

static void RemoveTrailingSeparators(UString &path)
{
  while (!path.IsEmpty() && IS_PATH_SEPAR(path.Back()))
    path.DeleteBack();
}

// C# Path.Combine for a folder and a plain name
static UString CombinePath(const UString &folder, const UString &name)
{
  UString s = folder;
  if (!s.IsEmpty() && !IS_PATH_SEPAR(s.Back()))
    s.Add_PathSepar();
  s += name;
  return s;
}

static bool HasSuffix_NoCase(const UString &s, const UString &suffix)
{
  return s.Len() >= suffix.Len() && StringsAreEqualNoCase(s.Ptr(s.Len() - suffix.Len()), suffix);
}

// "a.tar.gz" -> "a": removes the longest known archive extension
static UString StripArchiveExtensions(const UString &path)
{
  const UString dir = GetDirectoryPart(path);
  UString name = GetFileNamePart(path);
  unsigned best = 0;
  for (unsigned i = 0; i < NFormatCapabilities::GetCount(); i++)
  {
    UString ext (L".");
    ext += NFormatCapabilities::GetByIndex(i).Extension;
    if (ext.Len() > best && HasSuffix_NoCase(name, ext))
      best = ext.Len();
  }
  name.DeleteFrom(name.Len() - best);
  return dir.IsEmpty() ? name : CombinePath(dir, name);
}

// C# Path.IsPathFullyQualified: "C:\..." or "\\server\..."
static bool IsFullyQualified(const UString &path)
{
  return NFile::NName::IsDrivePath(path)
      || (path.Len() > 2 && IS_PATH_SEPAR(path[0]) && IS_PATH_SEPAR(path[1]));
}


// ---------- CSourceItem ----------

CSourceItem::CSourceItem(const UString &path, const std::function<void(CSourceItem *item)> &onRemove):
    _iconLoaded(false),
    Path(path)
{
  if (onRemove)
    _removeCommand = make_self<CCommand>([this, onRemove] { onRemove(this); });

  AddProperty(L"Path", [this] { return BoxString(Path); });
  AddProperty(L"Name", [this] { return BoxString(GetName()); });
  AddProperty(L"Folder", [this] { return BoxString(GetFolder()); });
  AddProperty(L"Icon", [this]() -> CObject { return GetIcon(); });
  AddProperty(L"RemoveLabel", [this]
  {
    UString s (L"Remove ");
    s += GetName();
    return BoxString(s);
  });
  AddProperty(L"RemoveCommand", [this]() -> CObject
  {
    if (_removeCommand)
      return _removeCommand.as<Input::ICommand>();
    return nullptr;
  });
}

UString CSourceItem::GetName() const
{
  UString path = Path;
  RemoveTrailingSeparators(path);
  const UString name = GetFileNamePart(path);
  return name.IsEmpty() ? Path : name;
}

UString CSourceItem::GetFolder() const
{
  return GetDirectoryPart(Path);
}

Media::ImageSource CSourceItem::GetIcon()
{
  if (!_iconLoaded)
  {
    _iconLoaded = true;
    _icon = NFile::NFind::DoesDirExist_FollowLink(us2fs(Path)) ?
        NShellIcons::GetFolderIcon() :
        NShellIcons::GetIcon(Path);
  }
  return _icon;
}

CSourceItem *CSourceItem::FromObject(const CObject &item)
{
  return get_self<CSourceItem>(item.as<Data::ICustomPropertyProvider>());
}


// ---------- CCompressViewModel ----------

CCompressViewModel::CCompressViewModel(const UStringVector &sources, const UString &targetArchive):
    _nameTouched(false),
    _formatIndex(0),
    _level(NCompressionLevel::kNormal),
    _levelIndex(0),
    _methodIndex(-1),
    _dictionaryIndex(0),
    _wordSizeIndex(0),
    _solidIndex(0),
    _numProcessors(NSystem::GetNumberOfProcessors()),
    _threadsIndex(0),
    _volumeIndex(0),
    _updateModeIndex(0),
    _pathModeIndex(0),
    _zipEncryptionIndex(0),
    _encryptFileNames(false),
    _storeCreationTime(false),
    _storeAccessTime(false),
    _followLinks(false),
    _deleteAfter(false),
    _shareAfter(false),
    _totalSizeText(L"Calculating\u2026"),
    _isWorking(false),
    _measureGeneration(0),
    Sources(NewItemVector()),
    Formats(NewItemVector()),
    LevelChoices(NewItemVector()),
    MethodChoices(NewItemVector()),
    DictionaryChoices(NewItemVector()),
    WordSizeChoices(NewItemVector()),
    SolidChoices(NewItemVector()),
    ThreadChoices(NewItemVector()),
    VolumeChoices(NewItemVector()),
    UpdateModes(NewItemVector()),
    PathModes(NewItemVector()),
    ZipEncryptionChoices(NewItemVector()),
    Closed(false)
{
  if (_numProcessors == 0)
    _numProcessors = 1;
  const CAppSettings &s = g_App.Settings.Current;

  FOR_VECTOR (i, sources)
  {
    _sourcePaths.Add(sources[i]);
    Sources.Append(make<CSourceItem>(sources[i], [this](CSourceItem *item) { RemoveSource(item); }));
  }

  for (unsigned i = 0; i < NFormatCapabilities::GetCount(); i++)
    Formats.Append(BoxText(NFormatCapabilities::GetByIndex(i).DisplayName));

  ThreadChoices.Append(BoxText(L"Automatic"));
  for (UInt32 i = 1; i <= _numProcessors * 2; i++)
  {
    UString label;
    label.Add_UInt32(i);
    label += (i == 1 ? L" thread" : L" threads");
    ThreadChoices.Append(BoxString(label));
  }

  UString formatId = s.DefaultFormat;
  if (!targetArchive.IsEmpty())
  {
    const CFormatCapabilities *f = NFormatCapabilities::FromFileName(targetArchive);
    if (f)
      formatId = f->Id;
  }
  const CFormatCapabilities &format = NFormatCapabilities::Get(formatId);
  _level = format.GetMaxLevel();
  for (unsigned i = 0; i < format.NumLevels; i++)
    if (format.Levels[i] == s.DefaultLevel)
      _level = s.DefaultLevel;
  for (unsigned i = 0; i < NFormatCapabilities::GetCount(); i++)
    if (&NFormatCapabilities::GetByIndex(i) == &format)
      _formatIndex = i;
  OnFormatChanged();

  _encryptFileNames = s.DefaultEncryptFileNames;
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(k_VolumeChoices); i++)
    VolumeChoices.Append(BoxText(k_VolumeChoices[i].Label));
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(k_UpdateModes); i++)
    UpdateModes.Append(BoxText(k_UpdateModes[i].Label));
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(k_PathModes); i++)
    PathModes.Append(BoxText(k_PathModes[i].Label));
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(k_ZipEncryptionChoices); i++)
    ZipEncryptionChoices.Append(BoxText(k_ZipEncryptionChoices[i].Label));

  if (!targetArchive.IsEmpty())
  {
    _archivePath = targetArchive;
    _nameTouched = true;
  }
  SuggestName();

  // ---- bindable properties ----

  AddProperty(L"ArchivePath", [this] { return BoxString(_archivePath); },
      [this](const CObject &v) { ChangeArchivePath(UnboxString(v), false); });
  AddProperty(L"CustomVolume", [this] { return BoxString(_customVolume); },
      [this](const CObject &v) { _customVolume = UnboxString(v); });
  AddProperty(L"Parameters", [this] { return BoxString(_parameters); },
      [this](const CObject &v) { _parameters = UnboxString(v); });

  AddBoolProperty(L"EncryptFileNames", _encryptFileNames);
  AddBoolProperty(L"StoreCreationTime", _storeCreationTime);
  AddBoolProperty(L"StoreAccessTime", _storeAccessTime);
  AddBoolProperty(L"FollowLinks", _followLinks);
  AddBoolProperty(L"DeleteAfter", _deleteAfter);
  AddBoolProperty(L"ShareAfter", _shareAfter);

  AddProperty(L"FormatIndex", [this] { return box_value((int32_t)_formatIndex); },
      [this](const CObject &v)
      {
        const int i = unbox_value_or<int32_t>(v, -1);
        if (i < 0 || (unsigned)i == _formatIndex || (unsigned)i >= NFormatCapabilities::GetCount())
          return;
        _formatIndex = (unsigned)i;
        OnFormatChanged();
      });
  AddIndexProperty(L"LevelIndex", _levelIndex, [this] { OnLevelChanged(); });
  AddIndexProperty(L"MethodIndex", _methodIndex, [this] { OnMethodChanged(); });
  AddIndexProperty(L"DictionaryIndex", _dictionaryIndex, [this] { OnMemoryChanged(); });
  AddIndexProperty(L"WordSizeIndex", _wordSizeIndex, nullptr);
  AddIndexProperty(L"SolidIndex", _solidIndex, nullptr);
  AddIndexProperty(L"ThreadsIndex", _threadsIndex, [this] { OnMemoryChanged(); });
  AddIndexProperty(L"VolumeIndex", _volumeIndex, nullptr);
  AddIndexProperty(L"UpdateModeIndex", _updateModeIndex, nullptr);
  AddIndexProperty(L"PathModeIndex", _pathModeIndex, nullptr);
  AddIndexProperty(L"ZipEncryptionIndex", _zipEncryptionIndex, [this] { OnZipEncryptionChanged(); });

  AddProperty(L"SourceSummary", [this] { return BoxString(GetSourceSummary()); });
  AddProperty(L"TotalSizeText", [this] { return BoxString(_totalSizeText); });
  AddProperty(L"HasSources", [this] { return box_value(HasSources()); });
  AddProperty(L"FormatDescription", [this] { return BoxString(GetFormatDescription()); });
  AddProperty(L"MemoryText", [this] { return BoxString(GetMemoryText()); });
  AddProperty(L"ShowMethodOptions", [this] { return box_value(ShowMethodOptions()); });
  AddProperty(L"ShowSolid", [this] { return box_value(ShowSolid()); });
  AddProperty(L"ShowThreads", [this] { return box_value(ShowThreads()); });
  AddProperty(L"ShowEncryption", [this] { return box_value(ShowEncryption()); });
  AddProperty(L"ShowEncryptNames", [this] { return box_value(ShowEncryptNames()); });
  AddProperty(L"ShowZipEncryption", [this] { return box_value(ShowZipEncryption()); });
  AddProperty(L"ShowVolumes", [this] { return box_value(ShowVolumes()); });
  AddProperty(L"PasswordError", [this] { return BoxString(GetPasswordError()); });
  AddProperty(L"PasswordHint", [this] { return BoxString(GetPasswordHint()); });
  AddProperty(L"CanCreate", [this] { return box_value(CanCreate()); });
  AddProperty(L"IsWorking", [this] { return box_value(_isWorking); });
}

void CCompressViewModel::AddBoolProperty(const wchar_t *name, bool &field)
{
  AddProperty(name, [&field] { return box_value(field); },
      [&field](const CObject &v) { field = unbox_value_or<bool>(v, false); });
}

void CCompressViewModel::AddIndexProperty(const wchar_t *name, int &field, const std::function<void()> &onChanged)
{
  AddProperty(name, [&field] { return box_value((int32_t)field); },
      [&field, onChanged](const CObject &v)
      {
        // A ComboBox reports -1 while its items are replaced; the view model keeps its choice
        // and pushes it back with RaisePropertyChanged() after refilling the list.
        const int i = unbox_value_or<int32_t>(v, -1);
        if (i < 0 || i == field)
          return;
        field = i;
        if (onChanged)
          onChanged();
      });
}

void CCompressViewModel::RaiseMany(const wchar_t * const *names)
{
  for (; *names; names++)
    RaisePropertyChanged(*names);
}

const CFormatCapabilities &CCompressViewModel::GetSelectedFormat() const
{
  return NFormatCapabilities::GetByIndex(_formatIndex);
}

const CMethodOption *CCompressViewModel::GetSelectedMethod() const
{
  const CFormatCapabilities &f = GetSelectedFormat();
  if (_methodIndex < 0 || (unsigned)_methodIndex >= f.NumMethods)
    return NULL;
  return &f.Methods[_methodIndex];
}

UString CCompressViewModel::GetSelectedMethodName() const
{
  const CMethodOption *m = GetSelectedMethod();
  return m ? UString(m->Name) : UString();
}

// ---- reactions ----

void CCompressViewModel::OnFormatChanged()
{
  const CFormatCapabilities &f = GetSelectedFormat();
  _levels.Clear();
  LevelChoices.Clear();
  for (unsigned i = 0; i < f.NumLevels; i++)
  {
    _levels.Add(f.Levels[i]);
    LevelChoices.Append(BoxText(GetLevelLabel(f.Levels[i])));
  }
  int index = -1;
  FOR_VECTOR (i, _levels)
    if (_levels[i] == _level)
      index = (int)i;
  if (index < 0)
    FOR_VECTOR (i, _levels)
      if (_levels[i] == NCompressionLevel::kNormal)
        index = (int)i;
  if (index < 0)
    index = 0;
  _levelIndex = index;
  _level = _levels[(unsigned)index];

  RefreshMethods();

  // Keep the user's chosen name but swap the extension.
  if (!_archivePath.IsEmpty())
  {
    UString path = StripArchiveExtensions(_archivePath);
    path += L'.';
    path += f.Extension;
    // (the C# code marked the name as typed by the user here, which stopped later name suggestions)
    if (!_nameTouched)
      _lastAutoName = path;
    ChangeArchivePath(path, true);
  }

  static const wchar_t * const k_Names[] = { L"LevelIndex", L"FormatDescription", L"ShowSolid", L"ShowThreads",
      L"ShowEncryption", L"ShowEncryptNames", L"ShowZipEncryption", L"ShowVolumes", L"PasswordHint",
      L"CanCreate", L"MemoryText", NULL };
  RaiseMany(k_Names);
}

void CCompressViewModel::OnLevelChanged()
{
  if ((unsigned)_levelIndex < _levels.Size())
    _level = _levels[(unsigned)_levelIndex];
  RefreshDictionary();
  RaisePropertyChanged(L"MemoryText");
}

void CCompressViewModel::OnMethodChanged()
{
  RefreshDictionary();
  RaisePropertyChanged(L"MemoryText");
}

void CCompressViewModel::OnMemoryChanged()
{
  RaisePropertyChanged(L"MemoryText");
}

void CCompressViewModel::OnZipEncryptionChanged()
{
  RaisePropertyChanged(L"PasswordHint");
}

void CCompressViewModel::SetPassword(const UString &password)
{
  if (password == _password)
    return;
  _password = password;
  static const wchar_t * const k_Names[] = { L"PasswordError", L"PasswordHint", L"CanCreate", NULL };
  RaiseMany(k_Names);
}

void CCompressViewModel::SetConfirmPassword(const UString &password)
{
  if (password == _confirmPassword)
    return;
  _confirmPassword = password;
  static const wchar_t * const k_Names[] = { L"PasswordError", L"CanCreate", NULL };
  RaiseMany(k_Names);
}

void CCompressViewModel::SetArchivePath(const UString &path)
{
  ChangeArchivePath(path, true);
}

void CCompressViewModel::ChangeArchivePath(const UString &path, bool notifySelf)
{
  if (path == _archivePath)
    return;
  _archivePath = path;
  if (path != _lastAutoName)
    _nameTouched = true;
  if (notifySelf)
    RaisePropertyChanged(L"ArchivePath");
  RaisePropertyChanged(L"CanCreate");
}

void CCompressViewModel::RefreshMethods()
{
  const CFormatCapabilities &f = GetSelectedFormat();
  MethodChoices.Clear();
  for (unsigned i = 0; i < f.NumMethods; i++)
    MethodChoices.Append(BoxText(f.Methods[i].DisplayName));
  _methodIndex = (f.NumMethods != 0 ? 0 : -1);

  SolidChoices.Clear();
  _solidSizes.Clear();
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(k_SolidChoices); i++)
  {
    SolidChoices.Append(BoxText(k_SolidChoices[i].Label));
    _solidSizes.Add(k_SolidChoices[i].Size);
  }
  _solidIndex = 0;

  RefreshDictionary();
  static const wchar_t * const k_Names[] = { L"MethodIndex", L"SolidIndex", L"ShowMethodOptions", NULL };
  RaiseMany(k_Names);
}

void CCompressViewModel::RefreshDictionary()
{
  const UString method = GetSelectedMethodName();

  DictionaryChoices.Clear();
  _dictionaries.Clear();
  {
    UString label (L"Automatic (");
    label += NPropertyFormatter::FormatSize(NFormatCapabilities::GetDefaultDictionary(method, _level));
    label += L')';
    DictionaryChoices.Append(BoxString(label));
    _dictionaries.Add(0);
  }
  CRecordVector<UInt64> sizes;
  NFormatCapabilities::GetDictionarySizes(method, sizes);
  FOR_VECTOR (i, sizes)
  {
    DictionaryChoices.Append(BoxString(NPropertyFormatter::FormatSize(sizes[i])));
    _dictionaries.Add(sizes[i]);
  }
  _dictionaryIndex = 0;

  WordSizeChoices.Clear();
  _wordSizes.Clear();
  WordSizeChoices.Append(BoxText(L"Automatic"));
  _wordSizes.Add(0);
  const bool ppmd = method.IsEqualTo_Ascii_NoCase("PPMd");
  if (method.IsEqualTo_Ascii_NoCase("LZMA")
      || method.IsEqualTo_Ascii_NoCase("LZMA2")
      || ppmd
      || method.IsEqualTo_Ascii_NoCase("Deflate")
      || method.IsEqualTo_Ascii_NoCase("Deflate64"))
  {
    const UInt32 *words = ppmd ? k_WordSizes_Ppmd : k_WordSizes_Lz;
    const unsigned num = ppmd ? Z7_ARRAY_SIZE(k_WordSizes_Ppmd) : Z7_ARRAY_SIZE(k_WordSizes_Lz);
    for (unsigned i = 0; i < num; i++)
    {
      UString label;
      label.Add_UInt32(words[i]);
      WordSizeChoices.Append(BoxString(label));
      _wordSizes.Add(words[i]);
    }
  }
  _wordSizeIndex = 0;

  static const wchar_t * const k_Names[] = { L"DictionaryIndex", L"WordSizeIndex", L"ShowMethodOptions", NULL };
  RaiseMany(k_Names);
}

// ---- sources ----

UString CCompressViewModel::GetSourceSummary() const
{
  if (_sourcePaths.Size() == 1)
  {
    UString path = _sourcePaths[0];
    RemoveTrailingSeparators(path);
    const UString name = GetFileNamePart(path);
    return name.IsEmpty() ? _sourcePaths[0] : name;
  }
  UString s;
  s.Add_UInt32(_sourcePaths.Size());
  s += L" items";
  return s;
}

bool CCompressViewModel::AddSource(const UString &path)
{
  FOR_VECTOR (i, _sourcePaths)
    if (_sourcePaths[i].IsEqualTo_NoCase(path))
      return false;
  _sourcePaths.Add(path);
  Sources.Append(make<CSourceItem>(path, [this](CSourceItem *item) { RemoveSource(item); }));
  OnSourcesChanged();
  return true;
}

void CCompressViewModel::RemoveSource(CSourceItem *item)
{
  for (unsigned i = 0; i < Sources.Size(); i++)
    if (CSourceItem::FromObject(Sources.GetAt(i)) == item)
    {
      Sources.RemoveAt(i);
      _sourcePaths.Delete(i);
      OnSourcesChanged();
      return;
    }
}

void CCompressViewModel::OnSourcesChanged()
{
  SuggestName();
  static const wchar_t * const k_Names[] = { L"SourceSummary", L"HasSources", L"CanCreate", NULL };
  RaiseMany(k_Names);
  MeasureAsync();
}

void CCompressViewModel::SuggestName()
{
  if (_nameTouched || _sourcePaths.IsEmpty())
    return;
  UString first = _sourcePaths[0];
  RemoveTrailingSeparators(first);
  UString folder = GetDirectoryPart(first);
  if (folder.IsEmpty())
    folder = first;
  UString stem;
  if (_sourcePaths.Size() == 1)
    stem = NFile::NFind::DoesDirExist_FollowLink(us2fs(first)) ?
        GetFileNamePart(first) :
        GetFileNameWithoutExtension(first);
  else
  {
    UString parent = folder;
    RemoveTrailingSeparators(parent);
    stem = GetFileNamePart(parent);
    if (parent.Len() == 2 && parent[1] == ':') // drive root
      stem.Empty();
  }
  if (stem.IsEmpty())
    stem = L"Archive";
  stem += L'.';
  stem += GetSelectedFormat().Extension;
  _lastAutoName = CombinePath(folder, stem);
  ChangeArchivePath(_lastAutoName, true);
}

// C# DirectoryInfo.EnumerateFiles: recursive, hidden and system files included,
// inaccessible folders skipped. Returns false when the file limit is reached.
static bool MeasureFolder(const FString &folder, UInt64 &size, UInt64 &count)
{
  FString prefix = folder;
  if (!prefix.IsEmpty() && !IS_PATH_SEPAR(prefix.Back()))
    prefix.Add_PathSepar();
  NFile::NFind::CEnumerator enumerator;
  enumerator.SetDirPrefix(prefix);
  NFile::NFind::CFileInfo fi;
  for (;;)
  {
    bool found;
    if (!enumerator.Next(fi, found) || !found)
      return true;
    if (fi.IsDir())
    {
      // links and junctions are not followed (no cycles)
      if (!fi.HasReparsePoint() && !MeasureFolder(prefix + fi.Name, size, count))
        return false;
      continue;
    }
    size += fi.Size;
    if (++count > 2000000)
      return false;
  }
}

static void MeasurePaths(const UStringVector &paths, UInt64 &size, UInt64 &count)
{
  FOR_VECTOR (i, paths)
  {
    const FString path = us2fs(paths[i]);
    NFile::NFind::CFileInfo fi;
    if (!fi.Find_FollowLink(path))
      continue;
    if (!fi.IsDir())
    {
      size += fi.Size;
      count++;
    }
    else
      MeasureFolder(path, size, count);
  }
}

void CCompressViewModel::SetTotalSizeText(const UString &text)
{
  _totalSizeText = text;
  RaisePropertyChanged(L"TotalSizeText");
}

winrt::fire_and_forget CCompressViewModel::MeasureAsync()
{
  auto strong = get_strong();
  // a newer measurement (sources changed again) supersedes this one
  const unsigned generation = ++_measureGeneration;
  const UStringVector paths = _sourcePaths;
  SetTotalSizeText(L"Calculating\u2026");
  UInt64 size = 0;
  UInt64 count = 0;
  try
  {
    co_await resume_background();
    MeasurePaths(paths, size, count);
    co_await ResumeForeground(g_App.Dispatcher);
  }
  catch (const hresult_error &)
  {
    co_return;
  }
  if (generation != _measureGeneration)
    co_return;
  UString text = NPropertyFormatter::FormatCount((Int64)count, L"file");
  text += L" \u00B7 ";
  text += NPropertyFormatter::FormatSize(size);
  SetTotalSizeText(text);
}

void CCompressViewModel::SetIsWorking(bool value)
{
  _isWorking = value;
  static const wchar_t * const k_Names[] = { L"IsWorking", L"CanCreate", NULL };
  RaiseMany(k_Names);
}

// ---- computed properties ----

UString CCompressViewModel::GetFormatDescription() const
{
  return GetSelectedFormat().Description;
}

bool CCompressViewModel::ShowMethodOptions() const
{
  return GetSelectedFormat().NumMethods > 1 || _dictionaries.Size() > 1;
}

bool CCompressViewModel::ShowSolid() const
{
  const CFormatCapabilities &f = GetSelectedFormat();
  return f.Solid && wcscmp(f.Id, L"7z") == 0;
}

bool CCompressViewModel::ShowThreads() const { return GetSelectedFormat().MultiThread; }
bool CCompressViewModel::ShowEncryption() const { return GetSelectedFormat().Encryption; }
bool CCompressViewModel::ShowEncryptNames() const { return GetSelectedFormat().EncryptFileNames; }
bool CCompressViewModel::ShowZipEncryption() const { return wcscmp(GetSelectedFormat().Id, L"zip") == 0; }
bool CCompressViewModel::ShowVolumes() const { return !GetSelectedFormat().IsTarballOrSingle(); }

UString CCompressViewModel::GetPasswordError() const
{
  if (!_password.IsEmpty() && !_confirmPassword.IsEmpty() && _password != _confirmPassword)
    return UString(L"Passwords don't match.");
  return UString();
}

UString CCompressViewModel::GetPasswordHint() const
{
  if (_password.IsEmpty())
    return UString();
  if (_password.Len() < 8)
    return UString(L"Short passwords can be guessed quickly. Use 12+ characters.");
  if (ShowZipEncryption())
  {
    if (wcscmp(k_ZipEncryptionChoices[_zipEncryptionIndex].Method, L"ZipCrypto") == 0)
      return UString(L"ZipCrypto is easy to break. Prefer AES-256.");
    return UString(L"File names in ZIP archives stay visible. Use 7z to hide them.");
  }
  return UString();
}

bool CCompressViewModel::CanCreate() const
{
  if (!HasSources() || _archivePath.IsEmpty() || !GetPasswordError().IsEmpty() || _isWorking)
    return false;
  if (!GetSelectedFormat().SingleFile)
    return true;
  return _sourcePaths.Size() == 1 && NFile::NFind::DoesFileExist_FollowLink(us2fs(_sourcePaths[0]));
}

UString CCompressViewModel::GetMemoryText() const
{
  if (_level == NCompressionLevel::kStore)
    return UString(L"No compression: files are stored as-is.");
  const CFormatCapabilities &f = GetSelectedFormat();
  UString method = GetSelectedMethodName();
  if (method.IsEmpty() && f.NumMethods != 0)
    method = f.Methods[0].Name;
  const UInt64 dict = (_dictionaryIndex > 0 && (unsigned)_dictionaryIndex < _dictionaries.Size()) ?
      _dictionaries[(unsigned)_dictionaryIndex] :
      NFormatCapabilities::GetDefaultDictionary(method, _level);
  const UInt32 threads = _threadsIndex > 0 ? (UInt32)_threadsIndex : _numProcessors;
  UInt64 compress = 0, decompress = 0;
  NFormatCapabilities::EstimateMemory(method, dict, threads, compress, decompress);
  UString s (L"Needs about ");
  s += NPropertyFormatter::FormatSize(compress);
  s += L" of memory to compress and ";
  s += NPropertyFormatter::FormatSize(decompress);
  s += L" to extract.";
  return s;
}

// ---- job ----

void CCompressViewModel::BuildSettings(CCompressionSettings &s) const
{
  const CFormatCapabilities &f = GetSelectedFormat();
  s.Format = f.Id;
  s.Level = _level;
  s.Method.Empty();
  if (_methodIndex > 0 && (unsigned)_methodIndex < f.NumMethods) // the first method is the format default
    s.Method = f.Methods[_methodIndex].Name;
  s.DictionarySize_Defined = (_dictionaryIndex > 0 && (unsigned)_dictionaryIndex < _dictionaries.Size());
  if (s.DictionarySize_Defined)
    s.DictionarySize = _dictionaries[(unsigned)_dictionaryIndex];
  s.WordSize_Defined = (_wordSizeIndex > 0 && (unsigned)_wordSizeIndex < _wordSizes.Size());
  if (s.WordSize_Defined)
    s.WordSize = _wordSizes[(unsigned)_wordSizeIndex];
  s.SolidBlockSize_Defined = (ShowSolid() && _solidIndex > 0 && (unsigned)_solidIndex < _solidSizes.Size());
  if (s.SolidBlockSize_Defined)
    s.SolidBlockSize = _solidSizes[(unsigned)_solidIndex];
  s.Threads_Defined = (ShowThreads() && _threadsIndex > 0);
  if (s.Threads_Defined)
    s.Threads = (UInt32)_threadsIndex;
  s.Password.Empty();
  if (ShowEncryption())
    s.Password = _password;
  s.EncryptFileNames = ShowEncryptNames() && _encryptFileNames;
  s.ZipEncryption.Empty();
  if (ShowZipEncryption())
    s.ZipEncryption = k_ZipEncryptionChoices[_zipEncryptionIndex].Method;

  UInt64 volume = k_VolumeChoices[_volumeIndex].Size; // 0 = don't split
  UString custom = _customVolume;
  custom.Trim();
  UInt64 customSize;
  if (ShowVolumes() && !custom.IsEmpty() && TryParseSize(custom, customSize))
    volume = customSize;
  s.VolumeSize_Defined = (ShowVolumes() && volume != 0);
  if (s.VolumeSize_Defined)
    s.VolumeSize = volume;

  s.UpdateMode = k_UpdateModes[_updateModeIndex].Mode;
  s.PathMode = k_PathModes[_pathModeIndex].Mode;
  s.StoreCreationTime = _storeCreationTime;
  s.StoreAccessTime = _storeAccessTime;
  s.FollowLinks = _followLinks;
  s.ExtraParameters = _parameters;
  s.ExtraParameters.Trim();
}

// C# double.TryParse(NumberStyles.Float, InvariantCulture): [sign] digits [. digits] [e [sign] digits]
static bool IsFloatNumber(const wchar_t *s)
{
  if (*s == '+' || *s == '-')
    s++;
  unsigned numDigits = 0;
  while (*s >= '0' && *s <= '9') { s++; numDigits++; }
  if (*s == '.')
  {
    s++;
    while (*s >= '0' && *s <= '9') { s++; numDigits++; }
  }
  if (numDigits == 0)
    return false;
  if (*s == 'e' || *s == 'E')
  {
    s++;
    if (*s == '+' || *s == '-')
      s++;
    if (!(*s >= '0' && *s <= '9'))
      return false;
    while (*s >= '0' && *s <= '9')
      s++;
  }
  return *s == 0;
}

bool CCompressViewModel::TryParseSize(const UString &text, UInt64 &bytes)
{
  bytes = 0;
  UString s = text;
  s.Trim();
  s.MakeLower_Ascii();
  s.RemoveChar(L' ');
  while (!s.IsEmpty() && s.Back() == 'b')
    s.DeleteBack();
  if (s.IsEmpty())
    return false;
  double mul = 1;
  switch (s.Back())
  {
    case 'k': mul = (double)((UInt64)1 << 10); s.DeleteBack(); break;
    case 'm': mul = (double)((UInt64)1 << 20); s.DeleteBack(); break;
    case 'g': mul = (double)((UInt64)1 << 30); s.DeleteBack(); break;
    case 't': mul = (double)((UInt64)1 << 40); s.DeleteBack(); break;
    default: break;
  }
  if (!IsFloatNumber(s))
    return false;
  const double v = wcstod(s, NULL);
  if (!(v > 0))
    return false;
  const double b = v * mul;
  bytes = (b >= 18446744073709551615.0) ? (UInt64)(Int64)-1 : (UInt64)b;
  return bytes >= 1024;
}

winrt::Windows::Foundation::IAsyncOperation<bool> CCompressViewModel::CreateAsync(IWindowHost *host)
{
  auto strong = get_strong();
  CCompressionSettings settings;
  BuildSettings(settings);
  UString target = _archivePath;
  target.Trim();
  const FStringVector sources = _sourcePaths;
  const CFormatCapabilities &format = GetSelectedFormat();
  const UString name = GetFileNamePart(target);

  if (!IsFullyQualified(target) || name.IsEmpty())
  {
    co_await host->Dialogs().ShowErrorAsync(L"Choose a location", L"Enter a full path for the new archive, or use Browse.");
    co_return false;
  }

  // Adding to an existing archive of the same format updates it (classic 7-Zip behaviour).
  const bool exists = NFile::NFind::DoesFileExist_FollowLink(us2fs(target));
  bool update = exists && !settings.VolumeSize_Defined && !settings.IsTarball() && !format.SingleFile;
  if (exists)
  {
    UString message (L"\u201C");
    message += name;
    message += update ?
        L"\u201D already exists. Add the files to it, or replace it with a new archive?" :
        L"\u201D already exists. Replace it?";
    Controls::ContentDialog dialog;
    dialog.Title(BoxText(L"Archive already exists"));
    dialog.Content(BoxString(message));
    dialog.PrimaryButtonText(update ? L"Add to it" : L"Replace");
    if (update)
      dialog.SecondaryButtonText(L"Replace");
    dialog.CloseButtonText(L"Cancel");
    dialog.DefaultButton(Controls::ContentDialogButton::Primary);
    const Controls::ContentDialogResult answer = co_await host->Dialogs().ShowAsync(dialog);
    if (answer == Controls::ContentDialogResult::None || Closed)
      co_return false;
    update = update && answer == Controls::ContentDialogResult::Primary;
  }

  SetIsWorking(true);
  try
  {
    UString title (L"Creating ");
    title += name;
    com_ptr<COperationViewModel> op = make_self<COperationViewModel>(
        update ? NOperationKind::kUpdate : NOperationKind::kCompress, title, &host->Dialogs(), target);
    op->SetEfficiencyMode(g_App.Settings.Current.EfficiencyModeForBackground);
    CCompressResult result;
    const HRESULT hr = co_await host->Operations().RunAsync(op,
        [&](COperationContext &ctx, UString &errorMessage) -> HRESULT
        {
          if (!update)
            return NArchiveWriter::Create(us2fs(target), sources, settings, ctx, result, errorMessage);
          CArchiveSession session;
          CArchiveOpenOptions options;
          if (!settings.Password.IsEmpty())
          {
            options.Password_Defined = true;
            options.Password = settings.Password;
          }
          RINOK(session.Open(us2fs(target), options, ctx, errorMessage))
          CUpdatePlan plan;
          plan.Add = sources;
          return NArchiveWriter::Update(session, plan, &settings, ctx, result, errorMessage);
        },
        [&]
        {
          UString s = NPropertyFormatter::FormatSize(result.OutputBytes);
          s += L" \u00B7 ";
          s += FormatPercent(result.GetRatio());
          s += L" of original";
          return s;
        },
        [&] { return result.OutputFiles.IsEmpty() ? UString() : fs2us(result.OutputFiles[0]); });

    if (hr == E_ABORT)
    {
      SetIsWorking(false);
      co_return false;
    }
    if (hr != S_OK)
    {
      if (!Closed)
        co_await host->Dialogs().ShowErrorAsync(L"Couldn't create the archive", op->GetErrorMessage());
      SetIsWorking(false);
      co_return false;
    }
    if (op->Messages.Size() != 0 && !Closed)
      co_await host->Dialogs().ShowMessagesAsync(L"Archive created with warnings", op->Messages);
    if (_deleteAfter && result.Skipped == 0 && !op->Context.HasErrors() && !Closed)
    {
      UStringVector paths;
      FOR_VECTOR (i, sources)
        paths.Add(fs2us(sources[i]));
      NShellActions::Recycle(paths, host->GetHwnd());
    }
    LastResult = result;
  }
  catch (...)
  {
    SetIsWorking(false);
    throw;
  }
  SetIsWorking(false);
  co_return true;
}
