using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using SevenZip.App.Activation;
using SevenZip.App.Interop;
using SevenZip.App.Services;
using SevenZip.App.ViewModels;
using WinRT.Interop;

namespace SevenZip.App.Views;

/// <summary>Implemented by everything that can live in a tab.</summary>
public interface ITabPage
{
    string TabTitle { get; }
    IconSource TabIcon { get; }
    void OnTabClosed();
}

/// <summary>
/// The main window: tabs in the title bar (like File Explorer and Terminal on Windows 11) on Mica Alt.
/// Each tab is an archive, a "Create archive" job, checksums, benchmark, settings, or Home.
/// </summary>
public sealed partial class MainWindow : Window, IWindowHost
{
    public MainWindow()
    {
        InitializeComponent();
        Hwnd = WindowNative.GetWindowHandle(this);

        ExtendsContentIntoTitleBar = true;
        AppWindow.TitleBar.PreferredHeightOption = TitleBarHeightOption.Tall;
        SetTitleBar(DragRegion);
        AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "7zip.ico"));
        AppWindow.Title = "7-Zip";

        var scale = Win32.GetDpiForWindow(Hwnd) / 96.0;
        AppWindow.Resize(new Windows.Graphics.SizeInt32((int)(1180 * scale), (int)(760 * scale)));
        CenterOnScreen();

        Dialogs = new DialogService(() => Root.XamlRoot, DispatcherQueue, () => Hwnd);
        Operations = new OperationService(() => Hwnd, () => Win32.GetForegroundWindow() == Hwnd);
        ActivityList.ItemsSource = Operations.Operations;
        Operations.Operations.CollectionChanged += (_, _) => UpdateActivity();

        Root.ActualThemeChanged += (_, _) => ThemeHelper.UpdateCaptionButtons(this);
        AppWindow.Changed += (_, e) =>
        {
            if (e.DidSizeChange)
                UpdateCaptionSpacer();
        };
        Root.Loaded += (_, _) => UpdateCaptionSpacer();
        var settingsAccelerator = new KeyboardAccelerator { Key = (Windows.System.VirtualKey)0xBC, Modifiers = Windows.System.VirtualKeyModifiers.Control };
        settingsAccelerator.Invoked += (_, e) => { OpenSettings(); e.Handled = true; };
        Root.KeyboardAccelerators.Add(settingsAccelerator);

        AppWindow.Closing += OnClosing;
    }

    public nint Hwnd { get; }
    public WindowId WindowId => AppWindow.Id;
    public DialogService Dialogs { get; }
    public OperationService Operations { get; }

    private void CenterOnScreen()
    {
        var area = DisplayArea.GetFromWindowId(AppWindow.Id, DisplayAreaFallback.Nearest).WorkArea;
        var size = AppWindow.Size;
        AppWindow.Move(new Windows.Graphics.PointInt32(area.X + (area.Width - size.Width) / 2, area.Y + (area.Height - size.Height) / 2));
    }

    private void UpdateCaptionSpacer()
    {
        var scale = Root.XamlRoot?.RasterizationScale ?? 1;
        CaptionSpacer.Width = new GridLength(Math.Max(0, AppWindow.TitleBar.RightInset / scale));
    }

    private void UpdateActivity()
    {
        bool active = Operations.HasActive;
        ActivityRing.IsActive = active;
        NoActivityText.Visibility = Operations.Operations.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        foreach (var op in Operations.Operations)
        {
            op.PropertyChanged -= OnOperationChanged;
            op.PropertyChanged += OnOperationChanged;
        }
    }

    private void OnOperationChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(OperationViewModel.State))
            ActivityRing.IsActive = Operations.HasActive;
    }

    private async void OnClosing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        if (!Operations.HasActive)
        {
            CloseAllTabs();
            return;
        }
        args.Cancel = true;
        if (await Dialogs.ConfirmAsync("Operations are still running", "Closing 7-Zip will cancel them. Partially written archives are removed.", "Cancel them and close", destructive: true))
        {
            foreach (var op in Operations.Operations.Where(o => o.IsActive).ToList())
                op.CancelCommand.Execute(null);
            CloseAllTabs();
            Close();
        }
    }

    private void CloseAllTabs()
    {
        foreach (var tab in Tabs.TabItems.OfType<TabViewItem>())
            (tab.Content as ITabPage)?.OnTabClosed();
        Tabs.TabItems.Clear();
    }

    public void BringToFront()
    {
        if (AppWindow.Presenter is OverlappedPresenter { State: OverlappedPresenterState.Minimized } p)
            p.Restore();
        Activate();
        Win32.SetForegroundWindow(Hwnd);
    }

    // ---------------- activation ----------------

    public void Handle(LaunchRequest request)
    {
        switch (request.Verb)
        {
            case LaunchVerb.Open:
                foreach (var p in request.Paths)
                    OpenArchive(p, request.Format);
                break;
            case LaunchVerb.Compress when request.ShareOperation is { } share:
                _ = HandleShareAsync(share);
                break;
            case LaunchVerb.Compress:
                OpenCompress(request.Paths);
                break;
            case LaunchVerb.Hash:
                OpenHash(request.Paths, request.HashAlgorithm);
                break;
            case LaunchVerb.Benchmark:
                OpenBenchmark();
                break;
            case LaunchVerb.Settings:
                OpenSettings();
                break;
        }
        if (Tabs.TabItems.Count == 0)
            AddTab(new HomePage(this));
    }

    /// <summary>Files shared to 7-Zip from another app's Share sheet become a new archive.</summary>
    private async Task HandleShareAsync(Windows.ApplicationModel.DataTransfer.ShareTarget.ShareOperation share)
    {
        try
        {
            if (share.Data.Contains(Windows.ApplicationModel.DataTransfer.StandardDataFormats.StorageItems))
            {
                var items = await share.Data.GetStorageItemsAsync();
                share.ReportDataRetrieved();
                var paths = items.Select(i => i.Path).Where(p => !string.IsNullOrEmpty(p)).ToList();
                if (paths.Count > 0)
                    OpenCompress(paths);
            }
        }
        finally
        {
            share.ReportCompleted();
        }
    }

    // ---------------- IWindowHost ----------------

    public void OpenArchive(string path, string? format = null)
    {
        var existing = Tabs.TabItems.OfType<TabViewItem>().FirstOrDefault(t => t.Content is ArchivePage a && a.ViewModel.Parent is null
            && string.Equals(a.ViewModel.FilePath, path, StringComparison.OrdinalIgnoreCase));
        if (existing is not null)
        {
            Tabs.SelectedItem = existing;
            return;
        }
        var vm = new ArchiveViewModel(this, path, format: format);
        AddTab(new ArchivePage(this, vm), replaceHome: true);
        _ = vm.OpenAsync();
    }

    public void OpenNestedArchive(ArchiveViewModel parent, string extractedPath, string pathInParent)
    {
        var vm = new ArchiveViewModel(this, extractedPath, parent, pathInParent);
        AddTab(new ArchivePage(this, vm), insertAfterCurrent: true);
        _ = vm.OpenAsync();
    }

    public void OpenCompress(IReadOnlyList<string> paths, string? targetArchive = null)
        => AddTab(new CompressPage(this, new CompressViewModel(paths, targetArchive)), replaceHome: true);

    public void OpenHash(IReadOnlyList<string> paths, string? algorithm = null)
        => AddTab(new HashPage(this, new HashViewModel(paths, algorithm)), replaceHome: true);

    public void OpenBenchmark()
    {
        if (!SelectExisting<BenchmarkPage>())
            AddTab(new BenchmarkPage(new BenchmarkViewModel()), replaceHome: true);
    }

    public void OpenSettings()
    {
        if (!SelectExisting<SettingsPage>())
            AddTab(new SettingsPage(this));
    }

    public void UpdateTab(object page, string title)
    {
        var tab = Tabs.TabItems.OfType<TabViewItem>().FirstOrDefault(t => ReferenceEquals(t.Content, page));
        if (tab is null)
            return;
        tab.Header = title;
        ToolTipService.SetToolTip(tab, title);
        if (ReferenceEquals(Tabs.SelectedItem, tab))
            AppWindow.Title = $"{title} – 7-Zip";
    }

    public void CloseTab(object page)
    {
        var tab = Tabs.TabItems.OfType<TabViewItem>().FirstOrDefault(t => ReferenceEquals(t.Content, page));
        if (tab is not null)
            CloseTab(tab);
    }

    private bool SelectExisting<T>()
    {
        var tab = Tabs.TabItems.OfType<TabViewItem>().FirstOrDefault(t => t.Content is T);
        if (tab is null)
            return false;
        Tabs.SelectedItem = tab;
        return true;
    }

    private void AddTab(ITabPage page, bool replaceHome = false, bool insertAfterCurrent = false)
    {
        var tab = new TabViewItem
        {
            Header = page.TabTitle,
            IconSource = page.TabIcon,
            Content = page,
            HorizontalContentAlignment = HorizontalAlignment.Stretch,
            VerticalContentAlignment = VerticalAlignment.Stretch,
        };
        ToolTipService.SetToolTip(tab, page.TabTitle);

        // Opening something from a Home tab replaces it, like a browser's new-tab page.
        if (replaceHome && Tabs.SelectedItem is TabViewItem { Content: HomePage } home)
        {
            int index = Tabs.TabItems.IndexOf(home);
            Tabs.TabItems[index] = tab;
        }
        else if (insertAfterCurrent && Tabs.SelectedIndex >= 0)
        {
            Tabs.TabItems.Insert(Tabs.SelectedIndex + 1, tab);
        }
        else
        {
            Tabs.TabItems.Add(tab);
        }
        Tabs.SelectedItem = tab;
    }

    private void CloseTab(TabViewItem tab)
    {
        (tab.Content as ITabPage)?.OnTabClosed();
        Tabs.TabItems.Remove(tab);
        if (Tabs.TabItems.Count == 0)
        {
            if (Operations.HasActive)
                AddTab(new HomePage(this));
            else
                Close();
        }
    }

    // ---------------- events ----------------

    private void OnAddTabClick(TabView sender, object args) => AddTab(new HomePage(this));

    private void OnTabCloseRequested(TabView sender, TabViewTabCloseRequestedEventArgs args) => CloseTab(args.Tab);

    private void OnTabSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (Tabs.SelectedItem is TabViewItem { Content: ITabPage page } tab)
            AppWindow.Title = $"{tab.Header} – 7-Zip";
    }

    private void OnSettingsClick(object sender, RoutedEventArgs e) => OpenSettings();

    private void OnNewTabAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        AddTab(new HomePage(this));
        args.Handled = true;
    }

    private void OnCloseTabAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        if (Tabs.SelectedItem is TabViewItem tab)
            CloseTab(tab);
        args.Handled = true;
    }

    private void OnNextTabAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        if (Tabs.TabItems.Count > 0)
            Tabs.SelectedIndex = (Tabs.SelectedIndex + 1) % Tabs.TabItems.Count;
        args.Handled = true;
    }

    private void OnPreviousTabAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        if (Tabs.TabItems.Count > 0)
            Tabs.SelectedIndex = (Tabs.SelectedIndex - 1 + Tabs.TabItems.Count) % Tabs.TabItems.Count;
        args.Handled = true;
    }

    private async void OnOpenAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        args.Handled = true;
        if (await Pickers.PickArchiveAsync(WindowId) is { } path)
            OpenArchive(path);
    }

    private async void OnNewArchiveAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        args.Handled = true;
        var files = await Pickers.PickFilesAsync(WindowId, title: "Choose files to compress");
        if (files.Count > 0)
            OpenCompress(files);
    }
}
