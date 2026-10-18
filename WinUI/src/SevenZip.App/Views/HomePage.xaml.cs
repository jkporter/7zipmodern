using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using SevenZip.App.Services;
using SevenZip.Core.Formats;
using Windows.ApplicationModel.DataTransfer;

namespace SevenZip.App.Views;

public sealed class RecentItem(string path, DateTime when)
{
    public string Path { get; } = path;
    public string Name => System.IO.Path.GetFileName(Path);
    public string Folder => System.IO.Path.GetDirectoryName(Path) ?? "";
    public ImageSource? Icon => ShellIcons.GetIconForPath(Path);
    public string When => when.Date == DateTime.Today ? when.ToString("t") : when.ToString("d");
}

public sealed partial class HomePage : UserControl, ITabPage
{
    private readonly IWindowHost _host;

    public HomePage(IWindowHost host)
    {
        _host = host;
        InitializeComponent();
        LoadRecent();
    }

    public string TabTitle => "Home";
    public IconSource TabIcon => new FontIconSource { Glyph = "" };
    public void OnTabClosed() { }

    private void LoadRecent()
    {
        var items = App.Current.Settings.Current.Recent
            .Where(r => File.Exists(r.Path))
            .Select(r => new RecentItem(r.Path, r.LastOpened))
            .ToList();
        RecentList.ItemsSource = items;
        EmptyRecentText.Visibility = items.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        ClearRecentButton.Visibility = items.Count == 0 ? Visibility.Collapsed : Visibility.Visible;
    }

    private async void OnOpenClick(object sender, RoutedEventArgs e)
    {
        if (await Pickers.PickArchiveAsync(_host.WindowId) is { } path)
            _host.OpenArchive(path);
    }

    private async void OnCompressClick(object sender, RoutedEventArgs e)
    {
        var files = await Pickers.PickFilesAsync(_host.WindowId, title: "Choose files to compress");
        _host.OpenCompress(files);
    }

    private void OnHashClick(object sender, RoutedEventArgs e) => _host.OpenHash([]);

    private void OnBenchmarkClick(object sender, RoutedEventArgs e) => _host.OpenBenchmark();

    private void OnRecentClick(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is RecentItem r)
            _host.OpenArchive(r.Path);
    }

    private void OnRecentShowInFolder(object sender, RoutedEventArgs e)
    {
        if ((sender as FrameworkElement)?.Tag is string path)
            ShellActions.RevealInExplorer([path]);
    }

    private void OnRecentRemove(object sender, RoutedEventArgs e)
    {
        if ((sender as FrameworkElement)?.Tag is string path)
        {
            App.Current.Settings.RemoveRecent(path);
            LoadRecent();
        }
    }

    private void OnClearRecentClick(object sender, RoutedEventArgs e)
    {
        App.Current.Settings.Current.Recent.Clear();
        App.Current.Settings.Save();
        LoadRecent();
    }

    // ---- drag & drop ----

    private void OnDragOver(object sender, DragEventArgs e)
    {
        if (!e.DataView.Contains(StandardDataFormats.StorageItems))
            return;
        e.AcceptedOperation = DataPackageOperation.Copy;
        e.DragUIOverride.Caption = "Open or compress";
        DropZone.BorderBrush = (Brush)Application.Current.Resources["AccentFillColorDefaultBrush"];
        DropZone.Background = (Brush)Application.Current.Resources["SubtleFillColorSecondaryBrush"];
    }

    private void OnDragLeave(object sender, DragEventArgs e) => ResetDropZone();

    private void ResetDropZone()
    {
        DropZone.BorderBrush = (Brush)Application.Current.Resources["ControlStrongStrokeColorDefaultBrush"];
        DropZone.Background = null;
    }

    private async void OnDrop(object sender, DragEventArgs e)
    {
        ResetDropZone();
        if (!e.DataView.Contains(StandardDataFormats.StorageItems))
            return;
        var items = await e.DataView.GetStorageItemsAsync();
        var paths = items.Select(i => i.Path).Where(p => !string.IsNullOrEmpty(p)).ToList();
        var archives = paths.Where(p => File.Exists(p) && FormatRegistry.LooksLikeArchive(p)).ToList();
        if (archives.Count == paths.Count)
            foreach (var a in archives)
                _host.OpenArchive(a);
        else
            _host.OpenCompress(paths);
    }
}
