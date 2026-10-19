// ExtractOptionsModel.h

#ifndef ZIP7_INC_EXTRACT_OPTIONS_MODEL_H
#define ZIP7_INC_EXTRACT_OPTIONS_MODEL_H

#include "../../Common/MyString.h"

#include "../Bindable.h"

#include "../Core/ExtractOptions.h"

/*
State of the "Extract" options (dialog in the main window, or the compact
shell window). DataContext of ExtractOptionsPanel.xaml.
Bindable: Destination, CreateSubfolder, OverwriteIndex, PathModeIndex,
OpenFolderWhenDone, ZoneIndex, RestoreTimestamps, RestoreSymbolicLinks,
Password (TwoWay); NeedsPassword, SubfolderLabel.
*/

class CExtractOptionsModel: public NWindows11::CBindableT<CExtractOptionsModel>
{
  FString _parentFolder;
  UString _folderName;
  UString _destination;
  bool _createSubfolder;
  int _overwriteIndex;  // NOverwriteMode::EEnum: Ask, Replace, Skip, Keep both, Rename existing
  int _pathModeIndex;   // 0 = keep folders, 1 = flatten
  bool _openFolderWhenDone;
  int _zoneIndex;       // NZonePropagation::EEnum
  bool _restoreTimestamps;
  bool _restoreSymbolicLinks;
  bool _needsPassword;
  UString _password;

  FString GetPathWithFolder() const;
  void SetCreateSubfolder(bool value);
public:
  CExtractOptionsModel(const FString &parentFolder, const UString &folderName, bool suggestSubfolder, bool needsPassword);

  const UString &GetFolderName() const { return _folderName; }
  const UString &GetDestination() const { return _destination; }
  void SetDestination(const UString &value);
  bool GetCreateSubfolder() const { return _createSubfolder; }
  int GetOverwriteIndex() const { return _overwriteIndex; }
  int GetZoneIndex() const { return _zoneIndex; }
  bool GetOpenFolderWhenDone() const { return _openFolderWhenDone; }

  // basePath: "" = none
  void ToOptions(const UString &basePath, CExtractOptions &options) const;
  // returns an error message, "" = valid
  UString Validate() const;
};

#endif
