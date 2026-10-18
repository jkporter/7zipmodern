using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using SevenZip.App.Services;
using SevenZip.App.ViewModels;
using Windows.ApplicationModel.DataTransfer;

namespace SevenZip.App.Views;

public sealed partial class HashPage : UserControl, ITabPage
{
    private readonly IWindowHost _host;

    public HashPage(IWindowHost host, HashViewModel viewModel)
    {
        _host = host;
        ViewModel = viewModel;
        InitializeComponent();
        ViewModel.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(HashViewModel.VerifyOk))
                VerifyBar.Severity = ViewModel.VerifyOk switch { true => InfoBarSeverity.Success, false => InfoBarSeverity.Error, _ => InfoBarSeverity.Informational };
        };
        Loaded += async (_, _) =>
        {
            if (ViewModel.Sources.Count > 0 && ViewModel.Results.Count == 0)
                await ViewModel.ComputeAsync(_host);
        };
    }

    public HashViewModel ViewModel { get; }
    public string TabTitle => "Checksums";
    public IconSource TabIcon => new FontIconSource { Glyph = "" };
    public void OnTabClosed() { }

    private async void OnAddFilesClick(object sender, RoutedEventArgs e)
    {
        foreach (var p in await Pickers.PickFilesAsync(_host.WindowId, title: "Choose files to check"))
            ViewModel.Sources.Add(new SourceItem(p));
    }

    private void OnClearClick(object sender, RoutedEventArgs e)
    {
        ViewModel.Sources.Clear();
        ViewModel.Results.Clear();
    }

    private async void OnComputeClick(object sender, RoutedEventArgs e) => await ViewModel.ComputeAsync(_host);

    private void OnCopyDigestClick(object sender, RoutedEventArgs e)
    {
        if ((sender as FrameworkElement)?.Tag is string value)
            CopyText(value);
    }

    private void OnCopyAllClick(object sender, RoutedEventArgs e) => CopyText(ViewModel.CopyAllText());

    private static void CopyText(string text)
    {
        var package = new DataPackage();
        package.SetText(text);
        Clipboard.SetContent(package);
    }

    private async void OnPasteQuery(AutoSuggestBox sender, AutoSuggestBoxQuerySubmittedEventArgs args)
    {
        var content = Clipboard.GetContent();
        if (content.Contains(StandardDataFormats.Text))
            ViewModel.Expected = (await content.GetTextAsync()).Trim();
    }

    private async void OnExportClick(object sender, RoutedEventArgs e)
    {
        var algorithm = ViewModel.Algorithms.FirstOrDefault(a => a.IsSelected && a.Name.StartsWith("SHA", StringComparison.OrdinalIgnoreCase))
                        ?? ViewModel.Algorithms.FirstOrDefault(a => a.IsSelected);
        if (algorithm is null || ViewModel.Results.Count == 0)
            return;
        var ext = algorithm.Name.ToLowerInvariant().Replace("-", "");
        var first = ViewModel.Results[0].Result.Path;
        var suggested = Path.Combine(Path.GetDirectoryName(first)!, (ViewModel.Results.Count == 1 ? Path.GetFileName(first) : "checksums") + "." + ext);
        if (await Pickers.PickSaveAsync(_host.WindowId, suggested, $"{algorithm.Name} checksum file", ext) is { } path)
            await File.WriteAllTextAsync(path, ViewModel.ExportText(algorithm.Name));
    }

    private void OnDragOver(object sender, DragEventArgs e)
    {
        if (e.DataView.Contains(StandardDataFormats.StorageItems))
        {
            e.AcceptedOperation = DataPackageOperation.Copy;
            e.DragUIOverride.Caption = "Compute checksums";
        }
    }

    private async void OnDrop(object sender, DragEventArgs e)
    {
        if (!e.DataView.Contains(StandardDataFormats.StorageItems))
            return;
        foreach (var item in await e.DataView.GetStorageItemsAsync())
            if (!string.IsNullOrEmpty(item.Path))
                ViewModel.Sources.Add(new SourceItem(item.Path));
        await ViewModel.ComputeAsync(_host);
    }
}
