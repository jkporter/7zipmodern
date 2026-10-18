using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using SevenZip.App.Interop;
using SevenZip.App.Services;
using SevenZip.Core.Archives;
using SevenZip.Core.Compression;
using SevenZip.Core.Interop;

namespace SevenZip.App.Views;

public sealed partial class SettingsPage : UserControl, ITabPage
{
    private static readonly CompressionLevel[] s_levels =
        [CompressionLevel.Store, CompressionLevel.Fastest, CompressionLevel.Fast, CompressionLevel.Normal, CompressionLevel.Maximum, CompressionLevel.Ultra];

    private readonly IWindowHost _host;
    private bool _loaded;

    public SettingsPage(IWindowHost host)
    {
        _host = host;
        InitializeComponent();

        FormatCombo.ItemsSource = FormatCapabilities.All;
        FormatCombo.SelectedItem = FormatCapabilities.Get(S.DefaultFormat);

        var version = typeof(App).Assembly.GetName().Version;
        var engine = SevenZipEngine.GetEngineVersion();
        VersionText.Text = $"App {version?.ToString(3)} · Engine {engine?.Major}.{engine?.Minor:D2} · {System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture}"
                           + (ShellActions.IsPackaged ? " · MSIX" : " · unpackaged");
        AboutExpander.Description = $"{SevenZip.Core.Formats.FormatRegistry.All.Count} archive formats · {SevenZip.Core.Hashing.HashEngine.Algorithms.Count} checksum algorithms";
        UpdateTempSize();
        Loaded += (_, _) => _loaded = true;
    }

    public AppSettings S => App.Current.Settings.Current;

    public string TabTitle => "Settings";
    public IconSource TabIcon => new FontIconSource { Glyph = "" };
    public void OnTabClosed() => App.Current.Settings.Save();

    public int ThemeIndex
    {
        get => (int)S.Theme;
        set
        {
            if (value < 0 || (int)S.Theme == value) return;
            S.Theme = (AppTheme)value;
            Save();
            App.Current.ApplyThemeToAll();
        }
    }

    public int BackdropIndex
    {
        get => (int)S.Backdrop;
        set
        {
            if (value < 0 || (int)S.Backdrop == value) return;
            S.Backdrop = (BackdropKind)value;
            Save();
            App.Current.ApplyThemeToAll();
        }
    }

    public int OverwriteIndex
    {
        get => (int)S.OverwriteMode;
        set { if (value >= 0) { S.OverwriteMode = (OverwriteMode)value; Save(); } }
    }

    public int ZoneIndex
    {
        get => (int)S.ZonePropagation;
        set { if (value >= 0) { S.ZonePropagation = (ZonePropagation)value; Save(); } }
    }

    public int LevelIndex
    {
        get => Array.IndexOf(s_levels, S.DefaultLevel);
        set { if (value >= 0) { S.DefaultLevel = s_levels[value]; Save(); } }
    }

    private void Save()
    {
        if (_loaded)
            App.Current.Settings.Save();
    }

    // x:Bind TwoWay writes the new value after the Toggled event, so save on the next tick.
    private void OnChanged(object sender, RoutedEventArgs e) => DispatcherQueue.TryEnqueue(Save);

    private void OnFormatChanged(object sender, SelectionChangedEventArgs e)
    {
        if (FormatCombo.SelectedItem is FormatCapabilities f)
        {
            S.DefaultFormat = f.Id;
            Save();
        }
    }

    private async void OnDefaultAppsClick(object sender, RoutedEventArgs e)
    {
        var uri = "ms-settings:defaultapps";
        if (ShellActions.IsPackaged)
        {
            try
            {
                uri += "?registeredAUMID=" + Uri.EscapeDataString(Windows.ApplicationModel.Package.Current.Id.FamilyName + "!App");
            }
            catch { }
        }
        await Windows.System.Launcher.LaunchUriAsync(new Uri(uri));
    }

    private async void OnForgetPasswordsClick(object sender, RoutedEventArgs e)
    {
        int n = PasswordStore.ForgetAll();
        await _host.Dialogs.ShowErrorAsync("Saved passwords", n == 0 ? "There were no saved passwords." : $"Removed {n} saved password{(n == 1 ? "" : "s")}.");
    }

    private void OnCleanTempClick(object sender, RoutedEventArgs e)
    {
        TempFiles.CleanUp(everything: true);
        UpdateTempSize();
    }

    private void UpdateTempSize() => TempSizeText.Text = PropertyFormatter.FormatSize((ulong)TempFiles.MeasureAll());
}
