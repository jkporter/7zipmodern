// PageFactory.h

#ifndef ZIP7_INC_PAGE_FACTORY_H
#define ZIP7_INC_PAGE_FACTORY_H

#include "../../Common/MyString.h"

#include "../Core/ExtractOptions.h"

#include "WindowHost.h"

/*
The windows create the pages through these functions, so they don't depend
on the page classes. Each function is defined in the .cpp of its page:
  CreateArchivePage           ArchivePage.cpp
  CreateCompressPage          CompressPage.cpp
  CreateHashPage              HashPage.cpp
  CreateBenchmarkPage         BenchmarkPage.cpp
  CreateExtractOptionsPanel   ExtractOptionsPanel.cpp
  CreateHomePage              HomePage.cpp
  CreateSettingsPage          SettingsPage.cpp
Strings: "" = not specified (C# null). Called on the UI thread.
*/

// C# new ArchivePage(host, new ArchiveViewModel(host, path, parent, pathInParent, format)) + vm.OpenAsync().
// parent != NULL: a nested archive extracted from (parent) to the temp file (path).
CTabPageRef CreateArchivePage(IWindowHost *host, const UString &path, const UString &format,
    CArchiveViewModel *parent, const UString &pathInParent);

// C# new CompressPage(host, new CompressViewModel(paths, targetArchive))
CTabPageRef CreateCompressPage(IWindowHost *host, const UStringVector &paths, const UString &targetArchive);

// C# new HashPage(host, new HashViewModel(paths, algorithm))
CTabPageRef CreateHashPage(IWindowHost *host, const UStringVector &paths, const UString &algorithm);

// C# new BenchmarkPage(new BenchmarkViewModel())
CTabPageRef CreateBenchmarkPage(IWindowHost *host);

CTabPageRef CreateHomePage(IWindowHost *host);
CTabPageRef CreateSettingsPage(IWindowHost *host);


Z7_PURE_INTERFACES_BEGIN

/*
The "Extract" options panel (C# ExtractOptionsPanel + ExtractOptionsModel).
ArchivePage shows it in a ContentDialog; OperationWindow (the "Extract files..."
shell verb) shows it inline.
  GetRoot()                the panel's root element
  ValidateAndShowError()   C# model.Validate() + panel.ShowError(): false = invalid, the error is shown
  GetOptions()             C# model.ToOptions(basePath) ("" = null)
  GetCreateSubfolder()     C# model.CreateSubfolder
  GetOpenFolderWhenDone()  C# model.OpenFolderWhenDone
  SaveDefaults()           C# ArchivePage.SaveExtractDefaults(model): writes overwrite mode,
                           "open folder" and zone propagation to g_App.Settings and saves
*/

#define Z7_IFACEN_IExtractOptionsPanel(x) \
  virtual winrt::Microsoft::UI::Xaml::FrameworkElement GetRoot() x \
  virtual bool ValidateAndShowError() x \
  virtual void GetOptions(const UString &basePath, CExtractOptions &options) x \
  virtual bool GetCreateSubfolder() x \
  virtual bool GetOpenFolderWhenDone() x \
  virtual void SaveDefaults() x \

Z7_IFACE_DECL_PURE(IExtractOptionsPanel)

Z7_PURE_INTERFACES_END

typedef CUiObjectRef<IExtractOptionsPanel> CExtractOptionsPanelRef;

// C# new ExtractOptionsPanel(new ExtractOptionsModel(parentFolder, folderName, suggestSubfolder, needsPassword), host.WindowId)
CExtractOptionsPanelRef CreateExtractOptionsPanel(IWindowHost *host, const FString &parentFolder,
    const UString &folderName, bool suggestSubfolder, bool needsPassword);

#endif
