// Windows11/Control/SettingsCard.cpp

#include "StdAfx.h"

#include "SettingsCard.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

namespace NWindows11 {
namespace NControl {

#define CARD_TEMPLATED(path) "{Binding " path ", RelativeSource={RelativeSource TemplatedParent}}"

#define CARD_ICON \
  "<FontIcon Glyph='" CARD_TEMPLATED("Tag") "' FontSize='20' VerticalAlignment='Center' Margin='0,0,16,0'" \
  " Visibility='{Binding Tag, RelativeSource={RelativeSource TemplatedParent}, Converter={StaticResource VisibleIfNotEmpty}}'" \
  " AutomationProperties.AccessibilityView='Raw' />"

#define CARD_TEXT(column) \
  "<StackPanel Grid.Column='" column "' VerticalAlignment='Center' Margin='0,0,16,0'>" \
    "<TextBlock Text='" CARD_TEMPLATED("(AutomationProperties.Name)") "' TextWrapping='Wrap' />" \
    "<TextBlock Text='" CARD_TEMPLATED("(AutomationProperties.HelpText)") "' TextWrapping='Wrap'" \
    " Style='{StaticResource CaptionTextBlockStyle}' Foreground='{ThemeResource TextFillColorSecondaryBrush}'" \
    " Visibility='{Binding (AutomationProperties.HelpText), RelativeSource={RelativeSource TemplatedParent}," \
    " Converter={StaticResource VisibleIfNotEmpty}}' />" \
  "</StackPanel>"

#define CARD_COLUMNS \
  "<Grid.ColumnDefinitions>" \
    "<ColumnDefinition Width='Auto' /><ColumnDefinition Width='*' /><ColumnDefinition Width='Auto' />" \
  "</Grid.ColumnDefinitions>"

#define CARD_CHROME \
  "<Setter Property='Background' Value='{ThemeResource CardBackgroundFillColorDefaultBrush}' />" \
  "<Setter Property='BorderBrush' Value='{ThemeResource CardStrokeColorDefaultBrush}' />" \
  "<Setter Property='BorderThickness' Value='1' />" \
  "<Setter Property='CornerRadius' Value='{ThemeResource ControlCornerRadius}' />" \
  "<Setter Property='Padding' Value='16,12' />" \
  "<Setter Property='MinHeight' Value='68' />" \
  "<Setter Property='HorizontalAlignment' Value='Stretch' />" \
  "<Setter Property='IsTabStop' Value='False' />"

#define CARD_GRID_BEGIN \
  "<Grid Background='{TemplateBinding Background}' BorderBrush='{TemplateBinding BorderBrush}'" \
  " BorderThickness='{TemplateBinding BorderThickness}' CornerRadius='{TemplateBinding CornerRadius}'" \
  " Padding='{TemplateBinding Padding}' MinHeight='{TemplateBinding MinHeight}'>"

#define CARD_CONTENT(attrs) \
  "<ContentPresenter " attrs " VerticalAlignment='Center'" \
  " HorizontalAlignment='{TemplateBinding HorizontalContentAlignment}' Content='{TemplateBinding Content}' />"

static const char * const kStyles =
"<ResourceDictionary"
" xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'"
" xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'>"

"<Style x:Key='SettingsCardStyle' TargetType='ContentControl'>"
  CARD_CHROME
  "<Setter Property='HorizontalContentAlignment' Value='Right' />"
  "<Setter Property='Template'><Setter.Value><ControlTemplate TargetType='ContentControl'>"
    CARD_GRID_BEGIN CARD_COLUMNS
      CARD_ICON
      CARD_TEXT("1")
      CARD_CONTENT("Grid.Column='2'")
    "</Grid>"
  "</ControlTemplate></Setter.Value></Setter>"
"</Style>"

"<Style x:Key='SettingsCardStackedStyle' TargetType='ContentControl'>"
  CARD_CHROME
  "<Setter Property='HorizontalContentAlignment' Value='Left' />"
  "<Setter Property='Template'><Setter.Value><ControlTemplate TargetType='ContentControl'>"
    CARD_GRID_BEGIN CARD_COLUMNS
      "<Grid.RowDefinitions><RowDefinition Height='Auto' /><RowDefinition Height='Auto' /></Grid.RowDefinitions>"
      CARD_ICON
      CARD_TEXT("1")
      CARD_CONTENT("Grid.Row='1' Grid.Column='1' Grid.ColumnSpan='2' Margin='0,8,0,0'")
    "</Grid>"
  "</ControlTemplate></Setter.Value></Setter>"
"</Style>"

"<Style x:Key='SettingsCardButtonStyle' TargetType='Button' BasedOn='{StaticResource DefaultButtonStyle}'>"
  "<Setter Property='Background' Value='{ThemeResource CardBackgroundFillColorDefaultBrush}' />"
  "<Setter Property='BorderBrush' Value='{ThemeResource CardStrokeColorDefaultBrush}' />"
  "<Setter Property='Padding' Value='16,12' />"
  "<Setter Property='MinHeight' Value='68' />"
  "<Setter Property='HorizontalAlignment' Value='Stretch' />"
  "<Setter Property='HorizontalContentAlignment' Value='Stretch' />"
  "<Setter Property='Template'><Setter.Value><ControlTemplate TargetType='Button'>"
    "<Grid x:Name='RootGrid' Background='{TemplateBinding Background}' BorderBrush='{TemplateBinding BorderBrush}'"
    " BorderThickness='{TemplateBinding BorderThickness}' CornerRadius='{TemplateBinding CornerRadius}'"
    " Padding='{TemplateBinding Padding}' MinHeight='{TemplateBinding MinHeight}'>" CARD_COLUMNS
      "<VisualStateManager.VisualStateGroups><VisualStateGroup x:Name='CommonStates'>"
        "<VisualState x:Name='Normal' />"
        "<VisualState x:Name='PointerOver'><VisualState.Setters>"
          "<Setter Target='RootGrid.Background' Value='{ThemeResource ControlFillColorSecondaryBrush}' />"
        "</VisualState.Setters></VisualState>"
        "<VisualState x:Name='Pressed'><VisualState.Setters>"
          "<Setter Target='RootGrid.Background' Value='{ThemeResource ControlFillColorTertiaryBrush}' />"
        "</VisualState.Setters></VisualState>"
        "<VisualState x:Name='Disabled' />"
      "</VisualStateGroup></VisualStateManager.VisualStateGroups>"
      CARD_ICON
      CARD_TEXT("1")
      "<StackPanel Grid.Column='2' Orientation='Horizontal' Spacing='16' VerticalAlignment='Center'>"
        "<ContentPresenter VerticalAlignment='Center' Content='{TemplateBinding Content}' />"
        "<FontIcon Glyph='&#xE974;' FontSize='12' VerticalAlignment='Center' AutomationProperties.AccessibilityView='Raw' />"
      "</StackPanel>"
    "</Grid>"
  "</ControlTemplate></Setter.Value></Setter>"
"</Style>"

"<Style x:Key='SettingsExpanderStyle' TargetType='Expander'>"
  "<Setter Property='HorizontalAlignment' Value='Stretch' />"
  "<Setter Property='HorizontalContentAlignment' Value='Stretch' />"
  "<Setter Property='Padding' Value='0' />"
  "<Setter Property='MinHeight' Value='68' />"
"</Style>"

"<Style x:Key='SettingsExpanderHeaderStyle' TargetType='ContentControl'>"
  "<Setter Property='HorizontalAlignment' Value='Stretch' />"
  "<Setter Property='HorizontalContentAlignment' Value='Right' />"
  "<Setter Property='IsTabStop' Value='False' />"
  "<Setter Property='Padding' Value='0,12' />"
  "<Setter Property='Template'><Setter.Value><ControlTemplate TargetType='ContentControl'>"
    "<Grid Padding='{TemplateBinding Padding}'>" CARD_COLUMNS
      CARD_ICON
      CARD_TEXT("1")
      CARD_CONTENT("Grid.Column='2'")
    "</Grid>"
  "</ControlTemplate></Setter.Value></Setter>"
"</Style>"

"<Style x:Key='SettingsExpanderItemStyle' TargetType='ContentControl'>"
  "<Setter Property='HorizontalAlignment' Value='Stretch' />"
  "<Setter Property='HorizontalContentAlignment' Value='Right' />"
  "<Setter Property='IsTabStop' Value='False' />"
  "<Setter Property='BorderBrush' Value='{ThemeResource CardStrokeColorDefaultBrush}' />"
  "<Setter Property='BorderThickness' Value='0,1,0,0' />"
  "<Setter Property='Padding' Value='58,8,16,8' />"
  "<Setter Property='MinHeight' Value='52' />"
  "<Setter Property='Template'><Setter.Value><ControlTemplate TargetType='ContentControl'>"
    "<Grid BorderBrush='{TemplateBinding BorderBrush}' BorderThickness='{TemplateBinding BorderThickness}'"
    " Padding='{TemplateBinding Padding}' MinHeight='{TemplateBinding MinHeight}'>" CARD_COLUMNS
      CARD_TEXT("1")
      CARD_CONTENT("Grid.Column='2'")
    "</Grid>"
  "</ControlTemplate></Setter.Value></Setter>"
"</Style>"

"</ResourceDictionary>";

void AddSettingsCardStyles(const ResourceDictionary &resources)
{
  resources.MergedDictionaries().Append(
      Markup::XamlReader::Load(to_hstring(kStyles)).as<ResourceDictionary>());
}

}}
