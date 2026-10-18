using System.Diagnostics;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using SevenZip.App.Activation;
using SevenZip.App.Interop;
using SevenZip.App.Services;
using SevenZip.App.ViewModels;
using SevenZip.Core.Archives;
using SevenZip.Core.Compression;
using SevenZip.Core.Operations;
using WinRT.Interop;

namespace SevenZip.App.Views;

/// <summary>
/// Small window for verbs started from File Explorer (successor of 7zG's progress dialog): optional
/// options step, live progress with pause/cancel/efficiency mode, then a result summary.
/// </summary>
public sealed partial class OperationWindow : Window, IWindowHost
{
    private readonly LaunchRequest _request;
    private OperationViewModel? _current;
    private TaskCompletionSource<bool>? _optionsDecision;
    private string? _revealPath;
    private bool _finished;
    private readonly List<OperationMessage> _messages = [];

    public OperationWindow(LaunchRequest request)
    {
        _request = request;
        InitializeComponent();
        Hwnd = WindowNative.GetWindowHandle(this);

        ExtendsContentIntoTitleBar = true;
        SetTitleBar(TitleArea);
        AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "7zip.ico"));
        if (AppWindow.Presenter is OverlappedPresenter p)
        {
            p.IsMaximizable = false;
            p.IsResizable = false;
        }
        Resize(520, 340);

        Dialogs = new DialogService(() => Root.XamlRoot, DispatcherQueue, () => Hwnd);
        Operations = new OperationService(() => Hwnd, () => Win32.GetForegroundWindow() == Hwnd);
        Root.ActualThemeChanged += (_, _) => ThemeHelper.UpdateCaptionButtons(this);
        // Title bar content fades when the window is inactive, matching the system caption buttons.
        Activated += (_, e) =>
        {
            double opacity = e.WindowActivationState == WindowActivationState.Deactivated ? 0.5 : 1;
            TitleIcon.Opacity = opacity;
            TitleBarText.Opacity = opacity;
        };
        EfficiencyToggle.IsChecked = App.Current.Settings.Current.EfficiencyModeForBackground;
        AppWindow.Closing += OnClosing;
        Root.Loaded += async (_, _) => await RunAsync();
    }

    public nint Hwnd { get; }
    public WindowId WindowId => AppWindow.Id;
    public DialogService Dialogs { get; }
    public OperationService Operations { get; }

    private void Resize(int width, int height)
    {
        var scale = Win32.GetDpiForWindow(Hwnd) / 96.0;
        AppWindow.Resize(new Windows.Graphics.SizeInt32((int)(width * scale), (int)(height * scale)));
        var area = DisplayArea.GetFromWindowId(AppWindow.Id, DisplayAreaFallback.Nearest).WorkArea;
        AppWindow.Move(new Windows.Graphics.PointInt32(area.X + (area.Width - AppWindow.Size.Width) / 2, area.Y + (area.Height - AppWindow.Size.Height) / 2));
    }

    // ---------------- flow ----------------

    private async Task RunAsync()
    {
        var paths = _request.Paths;
        if (paths.Count == 0)
        {
            Close();
            return;
        }
        try
        {
            switch (_request.Verb)
            {
                case LaunchVerb.Extract:
                    await ExtractWithOptionsAsync(paths);
                    break;
                case LaunchVerb.ExtractHere:
                    foreach (var p in paths)
                        if (!await ExtractOneAsync(p, _ => Path.GetDirectoryName(p)!, null)) break;
                    break;
                case LaunchVerb.ExtractSmart:
                    foreach (var p in paths)
                        if (!await ExtractOneAsync(p, s => ExtractPlanner.GetSmartDestination(s, Path.GetDirectoryName(p)!), null)) break;
                    break;
                case LaunchVerb.Test:
                    foreach (var p in paths)
                        if (!await TestOneAsync(p)) break;
                    break;
                case LaunchVerb.Compress7z:
                    await CompressAsync(paths, "7z", share: false);
                    break;
                case LaunchVerb.CompressZip:
                    await CompressAsync(paths, "zip", share: false);
                    break;
                case LaunchVerb.CompressShare:
                    await CompressAsync(paths, "zip", share: true);
                    break;
            }
        }
        catch (Exception ex)
        {
            _messages.Add(new OperationMessage(MessageSeverity.Error, ex.Message));
        }
        Finish();
    }

    private async Task ExtractWithOptionsAsync(IReadOnlyList<string> archives)
    {
        var first = archives[0];
        var folderName = ExtractPlanner.GetFolderNameFor(first);
        var model = new ExtractOptionsModel(Path.GetDirectoryName(first)!, folderName, App.Current.Settings.Current.SmartExtractFolder, needsPassword: false);
        var panel = new ExtractOptionsPanel(model, WindowId);
        TitleText.Text = archives.Count == 1 ? $"Extract {Path.GetFileName(first)}" : $"Extract {archives.Count} archives";
        OptionsHost.Content = panel;
        OptionsHost.Visibility = Visibility.Visible;
        EfficiencyToggle.Visibility = Visibility.Collapsed;
        PrimaryButton.Content = "Extract";
        SecondaryButton.Content = "Cancel";
        Resize(620, 600);

        _optionsDecision = new TaskCompletionSource<bool>();
        while (true)
        {
            if (!await _optionsDecision.Task)
            {
                Close();
                return;
            }
            if (model.Validate() is { } error)
            {
                panel.ShowError(error);
                _optionsDecision = new TaskCompletionSource<bool>();
                continue;
            }
            break;
        }
        _optionsDecision = null;
        ArchivePage.SaveExtractDefaults(model);
        OptionsHost.Visibility = Visibility.Collapsed;
        EfficiencyToggle.Visibility = Visibility.Visible;
        Resize(520, 340);

        var options = model.ToOptions(null);
        foreach (var archive in archives)
        {
            // Several archives with "new folder" each get their own folder under the chosen parent.
            Func<ArchiveSession, string> dest = model.CreateSubfolder && archives.Count > 1
                ? _ => Path.Combine(Path.GetDirectoryName(model.Destination.TrimEnd('\\'))!, ExtractPlanner.GetFolderNameFor(archive))
                : _ => model.Destination;
            if (!await ExtractOneAsync(archive, dest, options, model.OpenFolderWhenDone))
                break;
        }
    }

    private async Task<bool> ExtractOneAsync(string archive, Func<ArchiveSession, string> destination, ExtractOptions? template, bool? openFolder = null)
    {
        var s = App.Current.Settings.Current;
        var op = Begin(OperationKind.Extract, $"Extracting {Path.GetFileName(archive)}", archive);
        var result = await Operations.RunAsync(op, ctx =>
        {
            using var session = ArchiveSession.Open(archive, new ArchiveOpenOptions { Password = template?.Password }, ctx);
            var options = new ExtractOptions
            {
                Destination = destination(session),
                Overwrite = template?.Overwrite ?? s.OverwriteMode,
                PathMode = template?.PathMode ?? ExtractPathMode.FullPaths,
                ZonePropagation = template?.ZonePropagation ?? s.ZonePropagation,
                RestoreTimestamps = template?.RestoreTimestamps ?? true,
                RestoreSymbolicLinks = template?.RestoreSymbolicLinks ?? s.RestoreSymbolicLinks,
                Password = template?.Password,
            };
            return ExtractEngine.Extract(session, null, options, ctx);
        },
        r => r.Errors == 0 ? $"Extracted {r.FilesProcessed:N0} files" : $"{r.Errors} errors",
        r => r.Destination);
        End(op);
        if (result is null)
            return false;
        _revealPath = result.TopLevelPaths.Count == 1 ? result.TopLevelPaths[0] : result.Destination;
        if ((openFolder ?? s.OpenFolderAfterExtract) && result.Errors == 0)
        {
            if (result.TopLevelPaths.Count is > 0 and <= 16)
                ShellActions.RevealInExplorer(result.TopLevelPaths);
            else
                ShellActions.OpenFolder(result.Destination);
        }
        return true;
    }

    private async Task<bool> TestOneAsync(string archive)
    {
        var op = Begin(OperationKind.Test, $"Testing {Path.GetFileName(archive)}", archive);
        var result = await Operations.RunAsync(op, ctx =>
        {
            using var session = ArchiveSession.Open(archive, null, ctx);
            return ExtractEngine.Test(session, null, ctx);
        }, r => r.Errors == 0 ? $"No errors · {r.FilesProcessed:N0} files" : $"{r.Errors} errors");
        End(op);
        return result is not null;
    }

    private async Task CompressAsync(IReadOnlyList<string> sources, string format, bool share)
    {
        var first = sources[0].TrimEnd('\\');
        var folder = Path.GetDirectoryName(first) ?? first;
        var stem = sources.Count == 1
            ? (Directory.Exists(first) ? Path.GetFileName(first) : Path.GetFileNameWithoutExtension(first))
            : Path.GetFileName(folder) is { Length: > 0 } parent ? parent : "Archive";
        if (string.IsNullOrEmpty(stem))
            stem = "Archive";
        var target = share
            ? Path.Combine(TempFiles.NewFolder(), stem + "." + format)
            : Path.Combine(folder, stem + "." + format);

        var s = App.Current.Settings.Current;
        var settings = new CompressionSettings { Format = format, Level = s.DefaultLevel };
        bool update = !share && File.Exists(target);
        var op = Begin(update ? OperationKind.Update : OperationKind.Compress, (update ? "Adding to " : "Creating ") + Path.GetFileName(target), target);
        var result = await Operations.RunAsync(op, ctx =>
        {
            if (!update)
                return ArchiveWriter.Create(target, sources, settings, ctx);
            var session = ArchiveSession.Open(target, null, ctx);
            try { return ArchiveWriter.Update(session, new UpdatePlan { Add = sources }, settings, ctx); }
            finally { session.Dispose(); }
        },
        r => $"{PropertyFormatter.FormatSize(r.OutputBytes)} · {r.Ratio:P0} of original",
        r => r.OutputFiles.FirstOrDefault());
        End(op);
        if (result is null)
            return;
        _revealPath = result.OutputFiles.FirstOrDefault();
        if (share)
        {
            ShareService.ShareFiles(Hwnd, result.OutputFiles, Path.GetFileName(target));
            _revealPath = null;
        }
    }

    // ---------------- progress UI ----------------

    private OperationViewModel Begin(OperationKind kind, string title, string archivePath)
    {
        var op = new OperationViewModel(kind, title, new UiPrompts(Dialogs, kind is OperationKind.Compress ? null : archivePath))
        {
            EfficiencyMode = EfficiencyToggle.IsChecked == true,
        };
        _current = op;
        TitleText.Text = title;
        ProgressPanel.Visibility = Visibility.Visible;
        Progress.IsIndeterminate = true;
        Progress.Value = 0;
        DetailText.Text = "Preparing…";
        StatsText.Text = "";
        PrimaryButton.Content = "Cancel";
        SecondaryButton.Content = "Pause";
        SecondaryButton.Visibility = Visibility.Visible;
        op.ProgressChanged += OnProgress;
        AppWindow.Title = title;
        return op;
    }

    private void OnProgress(object? sender, EventArgs e)
    {
        if (sender is not OperationViewModel op)
            return;
        Progress.IsIndeterminate = op.IsIndeterminate;
        Progress.Value = op.Percent;
        DetailText.Text = op.Detail;
        StatsText.Text = op.Stats;
        AppWindow.Title = op.IsIndeterminate ? op.Title : $"{op.Percent:0}% · {op.Title}";
    }

    private void End(OperationViewModel op)
    {
        op.ProgressChanged -= OnProgress;
        _messages.AddRange(op.Messages);
        if (op.State == OperationState.Failed)
            _messages.Add(new OperationMessage(MessageSeverity.Error, op.Summary, op.Title));
        if (op.State == OperationState.Cancelled)
            _messages.Add(new OperationMessage(MessageSeverity.Info, "Cancelled.", op.Title));
        StatsText.Text = op.Summary;
        _current = null;
    }

    private async void Finish()
    {
        _finished = true;
        ProgressPanel.Visibility = Visibility.Collapsed;
        EfficiencyToggle.Visibility = Visibility.Collapsed;
        var errors = _messages.Count(m => m.Severity == MessageSeverity.Error);
        var warnings = _messages.Count(m => m.Severity == MessageSeverity.Warning);
        bool cancelled = _messages.Any(m => m.Text == "Cancelled.");

        ResultBar.Severity = errors > 0 ? InfoBarSeverity.Error : warnings > 0 ? InfoBarSeverity.Warning : cancelled ? InfoBarSeverity.Informational : InfoBarSeverity.Success;
        ResultBar.Title = errors > 0 ? $"Finished with {errors} error{(errors == 1 ? "" : "s")}"
            : cancelled ? "Cancelled"
            : _request.Verb == LaunchVerb.Test ? "No errors found"
            : _request.Verb == LaunchVerb.CompressShare ? "Ready to share"
            : "Done";
        ResultBar.Message = StatsText.Text;
        ResultBar.IsOpen = true;
        AppWindow.Title = $"{ResultBar.Title} – 7-Zip";

        if (_messages.Count > 0 && !(cancelled && _messages.Count == 1))
        {
            foreach (var m in _messages.Take(300))
                MessageList.Items.Add(new TextBlock { Text = m.ItemPath is null ? m.Text : $"{m.ItemPath}: {m.Text}", TextWrapping = TextWrapping.Wrap, IsTextSelectionEnabled = true });
            MessageList.Visibility = Visibility.Visible;
            Resize(560, 460);
        }

        PrimaryButton.Content = "Close";
        SecondaryButton.Content = "Show in File Explorer";
        SecondaryButton.Visibility = _revealPath is not null ? Visibility.Visible : Visibility.Collapsed;

        // Like the classic progress dialog: a clean, successful job just disappears.
        bool autoClose = errors == 0 && warnings == 0 && !cancelled && _request.Verb is not (LaunchVerb.Test or LaunchVerb.CompressShare);
        if (autoClose)
        {
            await Task.Delay(1500);
            Close();
        }
    }

    // ---------------- buttons ----------------

    private void OnPrimaryClick(object sender, RoutedEventArgs e)
    {
        if (_optionsDecision is not null)
            _optionsDecision.TrySetResult(true);
        else if (_current is { IsActive: true } op)
            op.CancelCommand.Execute(null);
        else if (_finished)
            Close();
    }

    private void OnSecondaryClick(object sender, RoutedEventArgs e)
    {
        if (_optionsDecision is not null)
        {
            _optionsDecision.TrySetResult(false);
        }
        else if (_current is { IsActive: true } op)
        {
            op.TogglePauseCommand.Execute(null);
            SecondaryButton.Content = op.IsPaused ? "Resume" : "Pause";
        }
        else if (_finished && _revealPath is not null)
        {
            if (Directory.Exists(_revealPath) && _request.Verb is not (LaunchVerb.Compress7z or LaunchVerb.CompressZip))
                ShellActions.OpenFolder(_revealPath);
            else
                ShellActions.RevealInExplorer([_revealPath]);
            Close();
        }
    }

    private void OnEfficiencyClick(object sender, RoutedEventArgs e)
    {
        if (_current is not null)
            _current.EfficiencyMode = EfficiencyToggle.IsChecked == true;
    }

    private void OnClosing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        _optionsDecision?.TrySetResult(false);
        if (_current is { IsActive: true } op)
            op.CancelCommand.Execute(null);
    }

    // ---------------- IWindowHost (hand interactive work to the main window) ----------------

    private static void LaunchMain(string verb, IEnumerable<string> paths)
    {
        var exe = Environment.ProcessPath;
        if (exe is null)
            return;
        var args = verb + " " + string.Join(' ', paths.Select(p => $"\"{p}\""));
        Process.Start(new ProcessStartInfo(exe, args) { UseShellExecute = false });
    }

    public void OpenArchive(string path, string? format = null) => LaunchMain("open", [path]);
    public void OpenNestedArchive(ArchiveViewModel parent, string extractedPath, string pathInParent) => LaunchMain("open", [extractedPath]);
    public void OpenCompress(IReadOnlyList<string> paths, string? targetArchive = null) => LaunchMain("compress", paths);
    public void OpenHash(IReadOnlyList<string> paths, string? algorithm = null) => LaunchMain("hash", paths);
    public void OpenBenchmark() => LaunchMain("benchmark", []);
    public void OpenSettings() => LaunchMain("settings", []);
    public void UpdateTab(object page, string title) { }
}
