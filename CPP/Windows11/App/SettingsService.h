// SettingsService.h

#ifndef ZIP7_INC_SETTINGS_SERVICE_H
#define ZIP7_INC_SETTINGS_SERVICE_H

#include "../../Common/MyString.h"
#include "../../Common/MyVector.h"

#include "../../Windows/Synchronization.h"

#include "../Core/CompressionSettings.h"
#include "../Core/ExtractOptions.h"

namespace NAppTheme
{
  enum EEnum
  {
    kSystem,
    kLight,
    kDark
  };
}

namespace NBackdropKind
{
  enum EEnum
  {
    kMica,
    kMicaAlt,
    kAcrylic
  };
}

struct CRecentArchive
{
  UString Path;
  FILETIME LastOpened; // UTC
};

/*
User preferences. Replaces HKCU\Software\7-Zip with a per-user JSON document
(settings.json: the same file, keys and enum names as the C# app, so existing
settings keep working).
*/

struct CAppSettings
{
  NAppTheme::EEnum Theme;
  NBackdropKind::EEnum Backdrop;

  // Extraction
  bool SmartExtractFolder;
  bool OpenFolderAfterExtract;
  NOverwriteMode::EEnum OverwriteMode;
  NZonePropagation::EEnum ZonePropagation;
  bool RestoreSymbolicLinks;

  // Compression
  UString DefaultFormat;
  NCompressionLevel::EEnum DefaultLevel;
  bool DefaultEncryptFileNames;

  // Browsing
  bool ShowDetailsPane;
  bool ConfirmDelete;
  bool AutoOpenTarballs;

  // Explorer context menu (read by SevenZip.ShellExtension.dll from contextmenu.ini)
  bool MenuCascaded;
  bool MenuOpen;
  bool MenuExtract;
  bool MenuExtractHere;
  bool MenuExtractSmart;
  bool MenuTest;
  bool MenuCompress;
  bool MenuCompress7z;
  bool MenuCompressZip;
  bool MenuCompressShare;
  bool MenuHash;

  // Background work
  bool NotifyOnCompletion;
  bool EfficiencyModeForBackground;
  bool OfferSavedPasswords;

  CObjectVector<CRecentArchive> Recent; // newest first

  CAppSettings();
};

/*
g_App.Settings. Change fields of Current on the UI thread, then call Save().
Folder: packaged = the package's LocalState, unpackaged = %LOCALAPPDATA%\7-Zip WinUI.
*/

class CSettingsService
{
  FString _file;
  NWindows::NSynchronization::CCriticalSection _cs;

  void WriteShellConfig();
  Z7_CLASS_NO_COPY(CSettingsService)
public:
  FString Folder; // with a trailing backslash
  CAppSettings Current;

  CSettingsService() {}
  void Load();  // called once at startup
  void Save();  // writes settings.json and contextmenu.ini

  void AddRecent(const UString &path); // also feeds Explorer's Recent list
  void RemoveRecent(const UString &path);
  void ClearRecent();
};

#endif
