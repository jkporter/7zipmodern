using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using SevenZip.App.Services;
using SevenZip.App.ViewModels;
using Windows.ApplicationModel.DataTransfer;

namespace SevenZip.App.Views;

public sealed partial class CompressPage : UserControl, ITabPage
{
    private readonly IWindowHost _host;

    public CompressPage(IWindowHost host, CompressViewModel viewModel)
    {
        _host = host;
        ViewModel = viewModel;
        InitializeComponent();
    }

    public CompressViewModel ViewModel { get; }
    public string TabTitle => ViewModel.HasSources ? $"New archive · {ViewModel.SourceSummary}" : "New archive";
    public IconSource TabIcon => new FontIconSource { Glyph = "" };
    public void OnTabClosed() { }

    private async void OnAddFilesClick(object sender, RoutedEventArgs e)
    {
        foreach (var p in await Pickers.PickFilesAsync(_host.WindowId, title: "Add files"))
            AddSource(p);
    }

    private async void OnAddFolderClick(object sender, RoutedEventArgs e)
    {
        foreach (var p in await Pickers.PickFoldersAsync(_host.WindowId, "Add folders"))
            AddSource(p);
    }

    private void AddSource(string path)
    {
        if (!ViewModel.Sources.Any(s => string.Equals(s.Path, path, StringComparison.OrdinalIgnoreCase)))
            ViewModel.Sources.Add(new SourceItem(path));
        _host.UpdateTab(this, TabTitle);
    }

    private void OnRemoveSourceClick(object sender, RoutedEventArgs e)
    {
        if ((sender as FrameworkElement)?.Tag is SourceItem item)
            ViewModel.Sources.Remove(item);
        _host.UpdateTab(this, TabTitle);
    }

    private async void OnBrowseClick(object sender, RoutedEventArgs e)
    {
        var f = ViewModel.SelectedFormat;
        if (await Pickers.PickSaveAsync(_host.WindowId, ViewModel.ArchivePath, $"{f.DisplayName} archive", f.Extension) is { } path)
            ViewModel.ArchivePath = path;
    }

    private void OnCancelClick(object sender, RoutedEventArgs e)
    {
        if (_host is MainWindow main)
            main.CloseTab(this);
    }

    private async void OnCreateClick(object sender, RoutedEventArgs e)
    {
        if (RememberCheck.IsChecked == true)
        {
            var s = App.Current.Settings.Current;
            s.DefaultFormat = ViewModel.SelectedFormat.Id;
            s.DefaultLevel = ViewModel.Level;
            s.DefaultEncryptFileNames = ViewModel.EncryptFileNames;
            App.Current.Settings.Save();
        }

        var result = await ViewModel.CreateAsync(_host);
        if (result is null)
            return;

        if (ViewModel.ShareAfter)
            ShareService.ShareFiles(_host.Hwnd, result.OutputFiles, Path.GetFileName(result.OutputFiles[0]));
        else
            ShellActions.RevealInExplorer(result.OutputFiles);

        if (_host is MainWindow main)
            main.CloseTab(this);
    }

    private void OnDragOver(object sender, DragEventArgs e)
    {
        if (e.DataView.Contains(StandardDataFormats.StorageItems))
        {
            e.AcceptedOperation = DataPackageOperation.Copy;
            e.DragUIOverride.Caption = "Add to the new archive";
        }
    }

    private async void OnDrop(object sender, DragEventArgs e)
    {
        if (!e.DataView.Contains(StandardDataFormats.StorageItems))
            return;
        foreach (var item in await e.DataView.GetStorageItemsAsync())
            if (!string.IsNullOrEmpty(item.Path))
                AddSource(item.Path);
    }
}
