using Microsoft.UI;
using Microsoft.UI.Composition.SystemBackdrops;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;

namespace SevenZip.App.Services;

/// <summary>Theme + system backdrop (Mica / Mica Alt for tabbed windows / Acrylic) per Windows 11 guidance.</summary>
public static class ThemeHelper
{
    public static void Apply(Window window)
    {
        var s = App.Current.Settings.Current;
        if (window.Content is FrameworkElement root)
        {
            root.RequestedTheme = s.Theme switch
            {
                AppTheme.Light => ElementTheme.Light,
                AppTheme.Dark => ElementTheme.Dark,
                _ => ElementTheme.Default,
            };
        }

        window.SystemBackdrop = s.Backdrop switch
        {
            BackdropKind.Acrylic => new DesktopAcrylicBackdrop(),
            BackdropKind.Mica => new MicaBackdrop { Kind = MicaKind.Base },
            _ => new MicaBackdrop { Kind = MicaKind.BaseAlt },
        };

        UpdateCaptionButtons(window);
    }

    /// <summary>Caption button colours must follow the in-app theme when the title bar is custom.</summary>
    public static void UpdateCaptionButtons(Window window)
    {
        if (window.Content is not FrameworkElement root || !AppWindowTitleBar.IsCustomizationSupported())
            return;
        var tb = window.AppWindow.TitleBar;
        bool dark = root.ActualTheme == ElementTheme.Dark;
        tb.ButtonBackgroundColor = Colors.Transparent;
        tb.ButtonInactiveBackgroundColor = Colors.Transparent;
        tb.ButtonForegroundColor = dark ? Colors.White : Colors.Black;
        tb.ButtonHoverBackgroundColor = dark ? Windows.UI.Color.FromArgb(0x18, 0xFF, 0xFF, 0xFF) : Windows.UI.Color.FromArgb(0x10, 0, 0, 0);
        tb.ButtonHoverForegroundColor = tb.ButtonForegroundColor;
        tb.ButtonPressedBackgroundColor = dark ? Windows.UI.Color.FromArgb(0x10, 0xFF, 0xFF, 0xFF) : Windows.UI.Color.FromArgb(0x08, 0, 0, 0);
        tb.ButtonInactiveForegroundColor = dark ? Windows.UI.Color.FromArgb(0x80, 0xFF, 0xFF, 0xFF) : Windows.UI.Color.FromArgb(0x80, 0, 0, 0);
    }
}
