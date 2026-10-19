// ExtractOptionsPanel.h

#ifndef ZIP7_INC_EXTRACT_OPTIONS_PANEL_H
#define ZIP7_INC_EXTRACT_OPTIONS_PANEL_H

#include "ExtractOptionsModel.h"
#include "PageFactory.h"

/*
The "Extract" options panel (ExtractOptionsPanel.xaml + CExtractOptionsModel).
Created through CreateExtractOptionsPanel() (PageFactory.h); ArchivePage puts it
into a ContentDialog, OperationWindow shows it inline.
*/

class CExtractOptionsPanel:
  public winrt::implements<CExtractOptionsPanel, winrt::Windows::Foundation::IInspectable>,
  public IExtractOptionsPanel
{
  IWindowHost *_host;
  winrt::com_ptr<CExtractOptionsModel> _model;
  winrt::Microsoft::UI::Xaml::FrameworkElement _root { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::InfoBar _errorBar { nullptr };

  winrt::fire_and_forget OnBrowse();
  void ShowError(const UString &message);
public:
  Z7_IFACE_IMP(IExtractOptionsPanel)

  CExtractOptionsPanel(IWindowHost *host, const FString &parentFolder, const UString &folderName,
      bool suggestSubfolder, bool needsPassword);
  void Init();
};

#endif
