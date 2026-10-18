using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using SevenZip.App.Services;
using SevenZip.App.ViewModels;
using SevenZip.Core.Formats;
using Windows.ApplicationModel.DataTransfer;
using Windows.Storage;
using Windows.System;

namespace SevenZip.App.Views;

public sealed partial class ArchivePage : UserControl, ITabPage, INotifyPropertyChanged
{
    private readonly IWindowHost _host;
    private readonly ColumnLayout _columns;

    public ArchivePage(IWindowHost host, ArchiveViewModel viewModel)
    {
        _host = host;
        ViewModel = viewModel;
        InitializeComponent();
        _columns = (ColumnLayout)Resources["Columns"];
        DetailsToggle.IsChecked = App.Current.Settings.Current.ShowDetailsPane;
        ApplyDetailsVisibility();
        ViewModel.PropertyChanged += OnViewModelChanged;
    }

    public ArchiveViewModel ViewModel { get; }
    public event PropertyChangedEventHandler? PropertyChanged;

    public string TabTitle => ViewModel.DisplayName;
    public IconSource TabIcon => new ImageIconSource { ImageSource = ShellIcons.GetIconForPath(ViewModel.FilePath) };
    public void OnTabClosed()
    {
        ViewModel.PropertyChanged -= OnViewModelChanged;
        ViewModel.Dispose();
    }

    public string EmptyText => ViewModel.IsSearching ? "No matches" : "This folder is empty";
    public Visibility EmptyVisibility => !ViewModel.IsLoading && ViewModel.LoadError is null && ViewModel.Items.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
    public Visibility ReadOnlyVisibility => !ViewModel.IsLoading && ViewModel.Session is not null && !ViewModel.CanModify ? Visibility.Visible : Visibility.Collapsed;

    private void OnViewModelChanged(object? sender, PropertyChangedEventArgs e)
    {
        PropertyChanged?.Invoke(this, new(nameof(EmptyText)));
        PropertyChanged?.Invoke(this, new(nameof(EmptyVisibility)));
        PropertyChanged?.Invoke(this, new(nameof(ReadOnlyVisibility)));
        if (e.PropertyName == nameof(ArchiveViewModel.IsLoading) && !ViewModel.IsLoading)
        {
            ExtractSmartItem.Text = $"Extract to “{SevenZip.Core.Archives.ExtractPlanner.GetFolderNameFor(ViewModel.DisplayName.Split(" › ").Last())}”";
            if (ViewModel.LoadError is null)
                ItemsList.Focus(FocusState.Programmatic);
        }
    }

    private IReadOnlyList<ItemViewModel> Selection => ItemsList.SelectedItems.OfType<ItemViewModel>().ToList();

    // ---------------- layout ----------------

    private void OnSizeChanged(object sender, SizeChangedEventArgs e)
    {
        bool details = DetailsToggle.IsChecked == true && e.NewSize.Width >= 820;
        DetailsPane.Visibility = details ? Visibility.Visible : Visibility.Collapsed;
        DetailsColumn.Width = details ? new GridLength(300) : new GridLength(0);
        _columns.Fit(e.NewSize.Width - (details ? 330 : 30));
    }

    private void ApplyDetailsVisibility()
    {
        bool on = DetailsToggle.IsChecked == true && (ActualWidth == 0 || ActualWidth >= 820);
        DetailsPane.Visibility = on ? Visibility.Visible : Visibility.Collapsed;
        DetailsColumn.Width = on ? new GridLength(300) : new GridLength(0);
        if (ActualWidth > 0)
            _columns.Fit(ActualWidth - (on ? 330 : 30));
    }

    private void OnDetailsToggleClick(object sender, RoutedEventArgs e)
    {
        App.Current.Settings.Current.ShowDetailsPane = DetailsToggle.IsChecked == true;
        App.Current.Settings.Save();
        ApplyDetailsVisibility();
    }

    // ---------------- navigation ----------------

    private void OnBreadcrumbClicked(BreadcrumbBar sender, BreadcrumbBarItemClickedEventArgs args)
    {
        if (args.Item is BreadcrumbSegment s)
            ViewModel.Navigate(s.Path);
    }

    private void OnUpClick(object sender, RoutedEventArgs e) => ViewModel.GoUp();

    private void OnUpAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        ViewModel.GoUp();
        args.Handled = true;
    }

    private void OnSearchAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        SearchBox.Focus(FocusState.Keyboard);
        args.Handled = true;
    }

    private void OnSortClick(object sender, RoutedEventArgs e)
    {
        if ((sender as FrameworkElement)?.Tag is string tag && Enum.TryParse<SortColumn>(tag, out var column))
            ViewModel.SetSort(column);
    }

    private void OnSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        var selection = Selection;
        ViewModel.ShowDetails(selection);
        RenameButton.IsEnabled = ViewModel.CanModify && selection.Count == 1;
        DeleteButton.IsEnabled = ViewModel.CanModify && selection.Count > 0;
    }

    private async void OnItemsDoubleTapped(object sender, DoubleTappedRoutedEventArgs e)
    {
        if ((e.OriginalSource as FrameworkElement)?.DataContext is ItemViewModel item)
            await ViewModel.OpenItemAsync(item);
    }

    private async void OnItemsKeyDown(object sender, KeyRoutedEventArgs e)
    {
        var selection = Selection;
        switch (e.Key)
        {
            case VirtualKey.Enter when selection.Count == 1:
                e.Handled = true;
                await ViewModel.OpenItemAsync(selection[0]);
                break;
            case VirtualKey.Back:
                e.Handled = true;
                ViewModel.GoUp();
                break;
            case VirtualKey.Delete when selection.Count > 0 && ViewModel.CanModify:
                e.Handled = true;
                await ViewModel.DeleteAsync(selection);
                break;
            case VirtualKey.F2 when selection.Count == 1 && ViewModel.CanModify:
                e.Handled = true;
                await ViewModel.RenameAsync(selection[0]);
                break;
        }
    }

    private void OnItemRightTapped(object sender, RightTappedRoutedEventArgs e)
    {
        if ((sender as FrameworkElement)?.DataContext is ItemViewModel item && !ItemsList.SelectedItems.Contains(item))
        {
            ItemsList.SelectedItems.Clear();
            ItemsList.SelectedItem = item;
        }
    }

    private void OnItemMenuOpening(object? sender, object e)
    {
        var selection = Selection;
        bool single = selection.Count == 1;
        bool file = single && !selection[0].IsFolder;
        MenuOpen.Visibility = Converters.Visible(single);
        MenuOpenWith.Visibility = Converters.Visible(file);
        MenuOpenInside.Visibility = Converters.Visible(file && FormatRegistry.LooksLikeArchive(selection[0].Name) is false);
        MenuRename.IsEnabled = single && ViewModel.CanModify;
        MenuDelete.IsEnabled = selection.Count > 0 && ViewModel.CanModify;
    }

    // ---------------- commands ----------------

    private async void OnExtractClick(object sender, RoutedEventArgs e) => await ExtractWithDialogAsync(Selection);

    private async void OnExtractAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        args.Handled = true;
        await ExtractWithDialogAsync(Selection);
    }

    private async Task ExtractWithDialogAsync(IReadOnlyList<ItemViewModel> selection)
    {
        if (ViewModel.Session is null)
            return;
        var model = ViewModel.CreateExtractOptions(selection);
        var panel = new ExtractOptionsPanel(model, _host.WindowId);
        var dialog = new ContentDialog
        {
            Title = selection.Count switch
            {
                0 => $"Extract {ViewModel.DisplayName}",
                1 => $"Extract “{selection[0].Name}”",
                _ => $"Extract {selection.Count} items",
            },
            Content = panel,
            PrimaryButtonText = "Extract",
            CloseButtonText = "Cancel",
            DefaultButton = ContentDialogButton.Primary,
        };
        dialog.PrimaryButtonClick += (d, args) =>
        {
            if (model.Validate() is { } error)
            {
                panel.ShowError(error);
                args.Cancel = true;
            }
        };
        if (await _host.Dialogs.ShowAsync(dialog) != ContentDialogResult.Primary)
            return;
        SaveExtractDefaults(model);
        await ViewModel.ExtractAsync(selection, model.ToOptions(null), model.OpenFolderWhenDone);
    }

    internal static void SaveExtractDefaults(ExtractOptionsModel model)
    {
        var s = App.Current.Settings.Current;
        s.OverwriteMode = (SevenZip.Core.Archives.OverwriteMode)model.OverwriteIndex;
        s.OpenFolderAfterExtract = model.OpenFolderWhenDone;
        s.ZonePropagation = (SevenZip.Core.Archives.ZonePropagation)model.ZoneIndex;
        App.Current.Settings.Save();
    }

    private async void OnExtractHereClick(object sender, RoutedEventArgs e) => await ViewModel.ExtractHereAsync(Selection);

    private async void OnExtractSmartClick(object sender, RoutedEventArgs e) => await ViewModel.ExtractSmartAsync(Selection);

    private async void OnTestClick(object sender, RoutedEventArgs e) => await ViewModel.TestAsync();

    private async void OnAddFilesClick(object sender, RoutedEventArgs e)
        => await ViewModel.AddAsync(await Pickers.PickFilesAsync(_host.WindowId, title: $"Add files to {ViewModel.DisplayName}"));

    private async void OnAddFolderClick(object sender, RoutedEventArgs e)
        => await ViewModel.AddAsync(await Pickers.PickFoldersAsync(_host.WindowId, $"Add folders to {ViewModel.DisplayName}"));

    private async void OnNewFolderClick(object sender, RoutedEventArgs e) => await ViewModel.NewFolderAsync();

    private async void OnRenameClick(object sender, RoutedEventArgs e)
    {
        if (Selection is [var item])
            await ViewModel.RenameAsync(item);
    }

    private async void OnDeleteClick(object sender, RoutedEventArgs e) => await ViewModel.DeleteAsync(Selection);

    private async void OnShareClick(object sender, RoutedEventArgs e)
    {
        var selection = Selection;
        if (selection.Count == 0)
        {
            ShareService.ShareFiles(_host.Hwnd, [ViewModel.FilePath], ViewModel.DisplayName);
            return;
        }
        var paths = await ViewModel.ExtractToTempAsync(selection);
        if (paths.Count > 0)
            ShareService.ShareFiles(_host.Hwnd, paths, selection.Count == 1 ? selection[0].Name : $"{selection.Count} items from {ViewModel.DisplayName}");
    }

    private async void OnMenuOpenClick(object sender, RoutedEventArgs e)
    {
        if (Selection is [var item])
            await ViewModel.OpenItemAsync(item);
    }

    private async void OnMenuOpenWithClick(object sender, RoutedEventArgs e)
    {
        if (Selection is [var item])
            await ViewModel.OpenItemAsync(item, openWith: true);
    }

    private async void OnMenuOpenInsideClick(object sender, RoutedEventArgs e)
    {
        if (Selection is [var item])
            await ViewModel.OpenInsideAsync(item);
    }

    private void OnMenuPropertiesClick(object sender, RoutedEventArgs e)
    {
        if (DetailsToggle.IsChecked != true)
        {
            DetailsToggle.IsChecked = true;
            OnDetailsToggleClick(sender, e);
        }
    }

    private async void OnMenuCopyClick(object sender, RoutedEventArgs e) => await CopyToClipboardAsync();

    private async void OnCopyAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        if (SearchBox.FocusState != FocusState.Unfocused)
            return;
        args.Handled = true;
        await CopyToClipboardAsync();
    }

    /// <summary>Copy puts real files on the clipboard, so Ctrl+V in File Explorer pastes them.</summary>
    private async Task CopyToClipboardAsync()
    {
        var selection = Selection;
        if (selection.Count == 0)
            return;
        var paths = await ViewModel.ExtractToTempAsync(selection);
        if (paths.Count == 0)
            return;
        var package = new DataPackage { RequestedOperation = DataPackageOperation.Copy };
        package.SetStorageItems(await ToStorageItemsAsync(paths));
        Clipboard.SetContent(package);
    }

    private async void OnPasteAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        if (SearchBox.FocusState != FocusState.Unfocused || !ViewModel.CanModify)
            return;
        var content = Clipboard.GetContent();
        if (!content.Contains(StandardDataFormats.StorageItems))
            return;
        args.Handled = true;
        var items = await content.GetStorageItemsAsync();
        await ViewModel.AddAsync(items.Select(i => i.Path).Where(p => !string.IsNullOrEmpty(p)).ToList());
    }

    private void OnRevealArchiveClick(object sender, RoutedEventArgs e) => ShellActions.RevealInExplorer([ViewModel.Parent?.FilePath ?? ViewModel.FilePath]);

    private void OnCopyPathClick(object sender, RoutedEventArgs e)
    {
        var package = new DataPackage();
        package.SetText(ViewModel.Parent?.FilePath ?? ViewModel.FilePath);
        Clipboard.SetContent(package);
    }

    private void OnHashArchiveClick(object sender, RoutedEventArgs e) => _host.OpenHash([ViewModel.FilePath]);

    private async void OnRefreshClick(object sender, RoutedEventArgs e) => await RefreshAsync();

    private async void OnRefreshAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        args.Handled = true;
        await RefreshAsync();
    }

    private async Task RefreshAsync()
    {
        var path = ViewModel.CurrentPath;
        var password = ViewModel.Session?.Password;
        ViewModel.Session?.Dispose();
        await ViewModel.OpenAsync(password);
        ViewModel.Navigate(path);
    }

    private async void OnRetryClick(object sender, RoutedEventArgs e) => await ViewModel.OpenAsync();

    private async void OnApplyEditClick(object sender, RoutedEventArgs e)
    {
        if ((sender as FrameworkElement)?.Tag is PendingEdit edit)
            await ViewModel.ApplyEditAsync(edit);
    }

    private void OnDiscardEditClick(object sender, RoutedEventArgs e)
    {
        if ((sender as FrameworkElement)?.Tag is PendingEdit edit)
            ViewModel.DiscardEdit(edit);
    }

    // ---------------- drag & drop ----------------

    /// <summary>Drag out to File Explorer: files are extracted only when the drop happens (delayed rendering).</summary>
    private void OnDragItemsStarting(object sender, DragItemsStartingEventArgs e)
    {
        var items = e.Items.OfType<ItemViewModel>().ToList();
        if (items.Count == 0)
        {
            e.Cancel = true;
            return;
        }
        e.Data.RequestedOperation = DataPackageOperation.Copy;
        e.Data.Properties.Title = items.Count == 1 ? items[0].Name : $"{items.Count} items";
        var dispatcher = DispatcherQueue;
        e.Data.SetDataProvider(StandardDataFormats.StorageItems, request =>
        {
            var deferral = request.GetDeferral();
            dispatcher.TryEnqueue(async () =>
            {
                try
                {
                    var paths = await ViewModel.ExtractToTempAsync(items, quiet: true);
                    request.SetData(await ToStorageItemsAsync(paths));
                }
                catch
                {
                }
                finally
                {
                    deferral.Complete();
                }
            });
        });
    }

    private static async Task<IReadOnlyList<IStorageItem>> ToStorageItemsAsync(IEnumerable<string> paths)
    {
        var list = new List<IStorageItem>();
        foreach (var p in paths)
            list.Add(Directory.Exists(p) ? await StorageFolder.GetFolderFromPathAsync(p) : await StorageFile.GetFileFromPathAsync(p));
        return list;
    }

    private void OnDragOver(object sender, DragEventArgs e)
    {
        if (!e.DataView.Contains(StandardDataFormats.StorageItems) || !ViewModel.CanModify)
            return;
        e.AcceptedOperation = DataPackageOperation.Copy;
        var folder = ViewModel.CurrentPath.Length == 0 ? ViewModel.DisplayName : ViewModel.CurrentPath.Split('\\').Last();
        e.DragUIOverride.Caption = $"Add to {folder}";
    }

    private async void OnDrop(object sender, DragEventArgs e)
    {
        if (!e.DataView.Contains(StandardDataFormats.StorageItems) || !ViewModel.CanModify)
            return;
        var deferral = e.GetDeferral();
        IReadOnlyList<IStorageItem> items;
        try
        {
            items = await e.DataView.GetStorageItemsAsync();
        }
        finally
        {
            deferral.Complete();
        }
        var paths = items.Select(i => i.Path).Where(p => !string.IsNullOrEmpty(p) && !p.StartsWith(Path.GetTempPath(), StringComparison.OrdinalIgnoreCase)).ToList();
        if (paths.Count == 0)
            return;
        var folder = ViewModel.CurrentPath.Length == 0 ? ViewModel.DisplayName : ViewModel.CurrentPath;
        var what = paths.Count == 1 ? $"“{Path.GetFileName(paths[0])}”" : $"{paths.Count} items";
        if (await _host.Dialogs.ConfirmAsync("Add to archive?", $"Add {what} to {folder}?", "Add"))
            await ViewModel.AddAsync(paths);
    }
}
