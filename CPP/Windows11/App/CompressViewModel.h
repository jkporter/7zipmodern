// CompressViewModel.h

#ifndef ZIP7_INC_COMPRESS_VIEW_MODEL_H
#define ZIP7_INC_COMPRESS_VIEW_MODEL_H

#include "../../Common/MyString.h"
#include "../../Common/MyVector.h"

#include "../Bindable.h"
#include "../Command.h"

#include "../Core/ArchiveWriter.h"
#include "../Core/CompressionSettings.h"

#include "WindowHost.h"

typedef winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable>
    CUiItemVector;

/*
A file or folder in the source list of the Create archive and Checksums pages.
Bindable: Path, Name, Folder, Icon, RemoveLabel, RemoveCommand.
*/

class CSourceItem: public NWindows11::CBindableT<CSourceItem>
{
  winrt::Microsoft::UI::Xaml::Media::ImageSource _icon { nullptr };
  bool _iconLoaded;
  winrt::com_ptr<NWindows11::CCommand> _removeCommand;
public:
  const UString Path;

  // onRemove (optional) is called on the UI thread by RemoveCommand
  CSourceItem(const UString &path, const std::function<void(CSourceItem *item)> &onRemove = nullptr);
  UString GetName() const;
  UString GetFolder() const;
  winrt::Microsoft::UI::Xaml::Media::ImageSource GetIcon();

  static CSourceItem *FromObject(const winrt::Windows::Foundation::IInspectable &item);
};


/*
The "Create archive" page (successor of the classic Add to Archive dialog).
The page sets the ItemsSource of each list control to the vectors below and
binds SelectedIndex (TwoWay) to the *Index properties:
  Formats -> FormatIndex, LevelChoices -> LevelIndex, MethodChoices -> MethodIndex,
  DictionaryChoices -> DictionaryIndex, WordSizeChoices -> WordSizeIndex, SolidChoices -> SolidIndex,
  ThreadChoices -> ThreadsIndex, VolumeChoices -> VolumeIndex, UpdateModes -> UpdateModeIndex,
  PathModes -> PathModeIndex, ZipEncryptionChoices -> ZipEncryptionIndex.
Other bindable properties:
  ArchivePath, CustomVolume, Parameters (TwoWay strings),
  EncryptFileNames, StoreCreationTime, StoreAccessTime, FollowLinks, DeleteAfter, ShareAfter (TwoWay bools),
  SourceSummary, TotalSizeText, HasSources, FormatDescription, MemoryText, ShowMethodOptions, ShowSolid,
  ShowThreads, ShowEncryption, ShowEncryptNames, ShowZipEncryption, ShowVolumes,
  PasswordError, PasswordHint, CanCreate, IsWorking.
PasswordBox.Password is set with SetPassword() / SetConfirmPassword() from PasswordChanged.
UI thread only.
*/

class CCompressViewModel: public NWindows11::CBindableT<CCompressViewModel>
{
  bool _nameTouched;
  UString _lastAutoName;
  UString _archivePath;
  UStringVector _sourcePaths;    // parallel to Sources

  unsigned _formatIndex;
  NCompressionLevel::EEnum _level;
  CRecordVector<NCompressionLevel::EEnum> _levels; // values of LevelChoices
  int _levelIndex;
  int _methodIndex;              // -1 = the format has no methods (C# null)
  CRecordVector<UInt64> _dictionaries; // [0] = automatic
  int _dictionaryIndex;
  CRecordVector<UInt32> _wordSizes;    // [0] = automatic
  int _wordSizeIndex;
  CRecordVector<UInt64> _solidSizes;   // [0] = automatic
  int _solidIndex;
  UInt32 _numProcessors;
  int _threadsIndex;             // 0 = automatic, else the number of threads
  int _volumeIndex;
  UString _customVolume;
  int _updateModeIndex;
  int _pathModeIndex;
  int _zipEncryptionIndex;

  UString _password;
  UString _confirmPassword;
  bool _encryptFileNames;
  bool _storeCreationTime;
  bool _storeAccessTime;
  bool _followLinks;
  bool _deleteAfter;
  bool _shareAfter;
  UString _parameters;
  UString _totalSizeText;
  bool _isWorking;
  unsigned _measureGeneration;

  void AddBoolProperty(const wchar_t *name, bool &field);
  void AddIndexProperty(const wchar_t *name, int &field, const std::function<void()> &onChanged);
  void RaiseMany(const wchar_t * const *names);

  void OnFormatChanged();
  void OnLevelChanged();
  void OnMethodChanged();
  void OnMemoryChanged();
  void OnZipEncryptionChanged();
  void ChangeArchivePath(const UString &path, bool notifySelf);
  void RefreshMethods();
  void RefreshDictionary();
  void SuggestName();
  void OnSourcesChanged();
  void SetTotalSizeText(const UString &text);
  void SetIsWorking(bool value);
  winrt::fire_and_forget MeasureAsync();

  const CMethodOption *GetSelectedMethod() const;
  UString GetSelectedMethodName() const; // "" = none

public:
  CUiItemVector Sources;
  CUiItemVector Formats;
  CUiItemVector LevelChoices;
  CUiItemVector MethodChoices;
  CUiItemVector DictionaryChoices;
  CUiItemVector WordSizeChoices;
  CUiItemVector SolidChoices;
  CUiItemVector ThreadChoices;
  CUiItemVector VolumeChoices;
  CUiItemVector UpdateModes;
  CUiItemVector PathModes;
  CUiItemVector ZipEncryptionChoices;

  CCompressResult LastResult; // set by CreateAsync() on success
  bool Closed;                // the tab was closed: don't use the host after an await

  CCompressViewModel(const UStringVector &sources, const UString &targetArchive);

  // starts measuring the sources (needs get_strong(), so not in the constructor)
  void Start() { MeasureAsync(); }

  const CFormatCapabilities &GetSelectedFormat() const;
  NCompressionLevel::EEnum GetLevel() const { return _level; }
  bool GetEncryptFileNames() const { return _encryptFileNames; }
  bool GetShareAfter() const { return _shareAfter; }
  const UString &GetArchivePath() const { return _archivePath; }
  void SetArchivePath(const UString &path);
  void SetPassword(const UString &password);
  void SetConfirmPassword(const UString &password);

  bool HasSources() const { return !_sourcePaths.IsEmpty(); }
  UString GetSourceSummary() const;
  // C# Sources.Add after the page's duplicate check; false = already in the list
  bool AddSource(const UString &path);
  void RemoveSource(CSourceItem *item);

  UString GetFormatDescription() const;
  bool ShowMethodOptions() const;
  bool ShowSolid() const;
  bool ShowThreads() const;
  bool ShowEncryption() const;
  bool ShowEncryptNames() const;
  bool ShowZipEncryption() const;
  bool ShowVolumes() const;
  UString GetPasswordError() const;
  UString GetPasswordHint() const;
  bool CanCreate() const;
  UString GetMemoryText() const;

  void BuildSettings(CCompressionSettings &settings) const;

  // "650m", "4.5g", "100000" (bytes) -> bytes
  static bool TryParseSize(const UString &text, UInt64 &bytes);

  // Runs the job; true = created (see LastResult), false = cancelled or failed (already reported).
  winrt::Windows::Foundation::IAsyncOperation<bool> CreateAsync(IWindowHost *host);
};

#endif
