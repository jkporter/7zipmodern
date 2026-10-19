// Windows11/Control/SettingsCard.h

#ifndef ZIP7_INC_WINDOWS11_CONTROL_SETTINGS_CARD_H
#define ZIP7_INC_WINDOWS11_CONTROL_SETTINGS_CARD_H

#include "../WinUI.h"

namespace NWindows11 {
namespace NControl {

/*
Windows 11 Settings style cards made of stock controls (no toolkit).
AddSettingsCardStyles() adds these styles to a ResourceDictionary; it needs
the converters from Converters.h in the same dictionary or above it.

  SettingsCardStyle            ContentControl: icon | header + description | content
  SettingsCardStackedStyle     ContentControl: content below the header, left aligned
  SettingsCardButtonStyle      Button: clickable card with a chevron
  SettingsExpanderStyle        Expander that looks like a card
  SettingsExpanderHeaderStyle  ContentControl for Expander.Header (card layout, no chrome)
  SettingsExpanderItemStyle    ContentControl for a row inside a SettingsExpander

The card text comes from attached properties, which also give the card its
accessible name:
  AutomationProperties.Name      header
  AutomationProperties.HelpText  description (optional)
  Tag                            icon glyph (optional), e.g. Tag="&#xE713;"

  <ContentControl Style="{StaticResource SettingsCardStyle}"
      AutomationProperties.Name="Theme" Tag="&#xE790;">
    <ComboBox x:Name="ThemeBox" />
  </ContentControl>
*/

void AddSettingsCardStyles(const winrt::Microsoft::UI::Xaml::ResourceDictionary &resources);

}}

#endif
