using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using SevenZip.App.Services;
using SevenZip.Core.Archives;
using SevenZip.Core.Compression;
using SevenZip.Core.Formats;
using SevenZip.Core.Operations;

namespace SevenZip.App.ViewModels;

public enum SortColumn { Name, Modified, Type, Size, Packed }

public sealed record PendingEdit(uint Index, string ArchivePath, string TempPath);

/// <summary>An open archive tab: navigation, search, sorting and every archive command.</summary>
public sealed partial class ArchiveViewModel : ObservableObject, IDisposable
{
    private readonly IWindowHost _host;
    private ArchiveSession? _session;
    private ArchiveSession? _outer;       // .tar.gz → the gzip layer kept alive under the tar
    private readonly List<FileSystemWatcher> _watchers = [];
    private CancellationTokenSource? _searchCts;

    public ArchiveViewModel(IWindowHost host, string path, ArchiveViewModel? parent = null, string? pathInParent = null, string? format = null)
    {
        _host = host;
        FilePath = path;
        Parent = parent;
        PathInParent = pathInParent;
        Format = format;
        DisplayName = parent is null ? Path.GetFileName(path) : $"{parent.DisplayName} › {Path.GetFileName(pathInParent ?? path)}";
    }

    public string FilePath { get; }
    public string? Format { get; }
    public ArchiveViewModel? Parent { get; }
    public string? PathInParent { get; }
    public ArchiveSession? Session => _session;
    public string DisplayName { get; private set; }

    public ObservableCollection<BreadcrumbSegment> Breadcrumbs { get; } = [];
    public ObservableCollection<PendingEdit> PendingEdits { get; } = [];
    public ObservableCollection<PropertyRow> Details { get; } = [];

    [ObservableProperty] public partial IReadOnlyList<ItemViewModel> Items { get; private set; } = [];
    [ObservableProperty] public partial string CurrentPath { get; private set; } = "";
    [ObservableProperty] public partial bool IsLoading { get; private set; } = true;
    [ObservableProperty] public partial string? LoadError { get; private set; }
    [ObservableProperty] public partial string StatusText { get; private set; } = "";
    [ObservableProperty] public partial string SelectionText { get; private set; } = "";
    [ObservableProperty] public partial string SearchText { get; set; } = "";
    [ObservableProperty] public partial bool IsSearching { get; private set; }
    [ObservableProperty] public partial SortColumn Sort { get; private set; } = SortColumn.Name;
    [ObservableProperty] public partial bool SortDescending { get; private set; }
    [ObservableProperty] public partial string? Warning { get; private set; }
    [ObservableProperty] public partial string DetailsTitle { get; private set; } = "";
    [ObservableProperty] public partial Microsoft.UI.Xaml.Media.ImageSource? DetailsIcon { get; private set; }

    public bool CanModify => _session?.CanUpdate == true;
    public string ReadOnlyReason => _session is null ? ""
        : Parent is not null || _outer is not null ? "Nested archives are read-only. Extract the archive to change it."
        : !_session.Format.CanUpdate ? $"{_session.Format.Name} archives can be opened and extracted but not modified."
        : _session.VolumePaths.Count > 1 || _session.ContainerFormat is not null ? "Multi-volume archives are read-only."
        : _session.Info.HasErrors ? "This archive has errors, so it is opened read-only."
        : "";

    public string FolderForArchive => Path.GetDirectoryName(Parent?.RootFilePath ?? FilePath)!;
    private string RootFilePath => Parent?.RootFilePath ?? FilePath;

    // ---------------- open ----------------

    public async Task OpenAsync(string? password = null)
    {
        IsLoading = true;
        LoadError = null;
        var op = new OperationViewModel(OperationKind.Open, $"Opening {Path.GetFileName(FilePath)}", new UiPrompts(_host.Dialogs, Parent is null ? FilePath : null));
        try
        {
            var opened = await _host.Operations.RunAsync(op, ctx =>
            {
                var options = new ArchiveOpenOptions
                {
                    Password = password,
                    Format = Format is null ? null : FormatRegistry.Find(Format),
                    DisplayName = Parent is null ? null : Path.GetFileName(PathInParent),
                    Parent = Parent?._session,
                    PathInParent = PathInParent,
                };
                ArchiveSession? session = null;
                for (int attempt = 0; ; attempt++)
                {
                    try
                    {
                        session = ArchiveSession.Open(FilePath, options, ctx);
                        break;
                    }
                    catch (WrongPasswordException) when (attempt < 4)
                    {
                        // UiPrompts shows "wrong password, try again" on the next request.
                    }
                }
                return new OpenResult(session, TryUnwrapTarball(session, ctx));
            }, r => PropertyFormatter.FormatCount(r.Session.Entries.Count, "item"));

            if (opened is null)
            {
                LoadError = "Opening was cancelled.";
                return;
            }

            if (opened.Inner is not null)
            {
                _outer = opened.Session;
                _session = opened.Inner;
            }
            else
            {
                _session = opened.Session;
            }

            if (Parent is null)
                App.Current.Settings.AddRecent(FilePath);

            var info = _session.Info;
            if (info.HasErrors)
                Warning = "This archive has problems: " + string.Join("; ", ArchiveInfo.DescribeErrorFlags(info.ErrorFlags).Append(info.Error ?? "").Where(s => s.Length > 0));
            else if (info.HasWarnings)
                Warning = string.Join("; ", ArchiveInfo.DescribeErrorFlags(info.WarningFlags).Append(info.Warning ?? "").Where(s => s.Length > 0));

            OnPropertyChanged(nameof(CanModify));
            OnPropertyChanged(nameof(ReadOnlyReason));
            Navigate("");
        }
        catch (Exception ex)
        {
            LoadError = ex.Message;
        }
        finally
        {
            IsLoading = false;
        }
    }

    private sealed record OpenResult(ArchiveSession Session, ArchiveSession? Inner);

    /// <summary>"backup.tar.gz" opens straight into the tar (one stream layer is just noise).</summary>
    private static ArchiveSession? TryUnwrapTarball(ArchiveSession session, OperationContext ctx)
    {
        if (!App.Current.Settings.Current.AutoOpenTarballs || session.Entries.Count != 1)
            return null;
        if (session.Format.Name.ToLowerInvariant() is not ("gzip" or "bzip2" or "xz" or "zstd" or "lzma" or "z" or "lz" or "lz4"))
            return null;
        var entry = session.Entries[0];
        if (!entry.Name.EndsWith(".tar", StringComparison.OrdinalIgnoreCase))
            return null;
        var temp = TempFiles.NewFolder();
        ExtractEngine.Extract(session, [entry.Index], new ExtractOptions { Destination = temp, ZonePropagation = ZonePropagation.Off }, ctx);
        var tarPath = Path.Combine(temp, PathSanitizer.GetSafeComponents(entry.Path).Last());
        return ArchiveSession.Open(tarPath, new ArchiveOpenOptions { DisplayName = Path.GetFileName(session.FilePath), Parent = session, PathInParent = entry.Path }, ctx);
    }

    // ---------------- navigation ----------------

    public void Navigate(string path)
    {
        if (_session is null)
            return;
        var folder = _session.Root.Navigate(path) ?? _session.Root;
        CurrentPath = folder.Path;
        if (SearchText.Length > 0)
            SearchText = "";

        Breadcrumbs.Clear();
        Breadcrumbs.Add(new BreadcrumbSegment(DisplayName, ""));
        var acc = "";
        foreach (var part in folder.Path.Split('\\', StringSplitOptions.RemoveEmptyEntries))
        {
            acc = acc.Length == 0 ? part : acc + "\\" + part;
            Breadcrumbs.Add(new BreadcrumbSegment(part, acc));
        }
        Populate(folder);
    }

    public bool CanGoUp => CurrentPath.Length > 0;

    public void GoUp()
    {
        if (!CanGoUp)
            return;
        int i = CurrentPath.LastIndexOf('\\');
        Navigate(i < 0 ? "" : CurrentPath[..i]);
    }

    private ArchiveFolder CurrentFolder => _session?.Root.Navigate(CurrentPath) ?? _session!.Root;

    private void Populate(ArchiveFolder folder)
    {
        var list = new List<ItemViewModel>(folder.Folders.Count + folder.Files.Count);
        foreach (var f in folder.Folders)
            list.Add(ItemViewModel.FromFolder(f));
        foreach (var e in folder.Files)
            list.Add(ItemViewModel.FromEntry(e));
        Items = SortItems(list);
        IsSearching = false;
        UpdateStatus();
        ShowDetails([]);
        OnPropertyChanged(nameof(CanGoUp));
    }

    public void SetSort(SortColumn column)
    {
        if (Sort == column)
            SortDescending = !SortDescending;
        else
        {
            Sort = column;
            SortDescending = column is SortColumn.Modified or SortColumn.Size or SortColumn.Packed;
        }
        Items = SortItems(Items.ToList());
    }

    private List<ItemViewModel> SortItems(List<ItemViewModel> items)
    {
        Comparison<ItemViewModel> cmp = Sort switch
        {
            SortColumn.Modified => (a, b) => Nullable.Compare(a.Modified, b.Modified),
            SortColumn.Type => (a, b) => string.Compare(a.TypeName, b.TypeName, StringComparison.CurrentCultureIgnoreCase),
            SortColumn.Size => (a, b) => a.Size.CompareTo(b.Size),
            SortColumn.Packed => (a, b) => Nullable.Compare(a.PackedSize, b.PackedSize),
            _ => (a, b) => NaturalCompare(a.Name, b.Name),
        };
        int sign = SortDescending ? -1 : 1;
        items.Sort((a, b) =>
        {
            if (a.IsFolder != b.IsFolder)
                return a.IsFolder ? -1 : 1;
            int r = cmp(a, b) * sign;
            return r != 0 ? r : NaturalCompare(a.Name, b.Name);
        });
        return items;
    }

    /// <summary>Explorer-style "file2" before "file10" ordering.</summary>
    private static int NaturalCompare(string a, string b)
        => System.Globalization.CompareInfo.GetCompareInfo("").Compare(a, b, System.Globalization.CompareOptions.IgnoreCase | System.Globalization.CompareOptions.NumericOrdering);

    partial void OnSearchTextChanged(string value) => _ = SearchAsync(value);

    /// <summary>Searches the whole archive (classic 7-Zip only filtered the current folder).</summary>
    private async Task SearchAsync(string query)
    {
        _searchCts?.Cancel();
        if (_session is null)
            return;
        if (string.IsNullOrWhiteSpace(query))
        {
            if (IsSearching)
                Populate(CurrentFolder);
            return;
        }
        var cts = _searchCts = new CancellationTokenSource();
        var session = _session;
        await Task.Delay(150);
        if (cts.IsCancellationRequested)
            return;

        var matches = await Task.Run(() =>
        {
            var hits = new List<object>();
            var stack = new Stack<ArchiveFolder>([session.Root]);
            while (stack.Count > 0 && hits.Count < 5000 && !cts.IsCancellationRequested)
            {
                var f = stack.Pop();
                foreach (var sub in f.Folders)
                {
                    if (sub.Name.Contains(query, StringComparison.CurrentCultureIgnoreCase))
                        hits.Add(sub);
                    stack.Push(sub);
                }
                foreach (var e in f.Files)
                    if (e.Name.Contains(query, StringComparison.CurrentCultureIgnoreCase))
                        hits.Add(e);
            }
            return hits;
        });
        if (cts.IsCancellationRequested)
            return;

        var list = matches.Select(m => m is ArchiveFolder f ? ItemViewModel.FromFolder(f, showLocation: true) : ItemViewModel.FromEntry((ArchiveEntry)m, showLocation: true)).ToList();
        Items = SortItems(list);
        IsSearching = true;
        StatusText = list.Count >= 5000 ? "More than 5,000 results — refine your search" : $"{list.Count:N0} results for “{query}”";
    }

    private void UpdateStatus()
    {
        if (_session is null)
            return;
        var folders = Items.Count(i => i.IsFolder);
        var files = Items.Count - folders;
        var size = Items.Aggregate(0UL, (s, i) => s + i.Size);
        StatusText = $"{PropertyFormatter.FormatCount(Items.Count, "item")} ({PropertyFormatter.FormatCount(folders, "folder")}, {PropertyFormatter.FormatCount(files, "file")}) · {PropertyFormatter.FormatSize(size)}";
    }

    public void ShowDetails(IReadOnlyList<ItemViewModel> selection)
    {
        Details.Clear();
        if (_session is null)
            return;

        if (selection.Count == 0)
        {
            var info = _session.Info;
            DetailsTitle = DisplayName;
            DetailsIcon = ShellIcons.GetIconForPath(FilePath);
            var (size, packed, files, folders) = _session.Root.Measure();
            Details.Add(new("Contents", $"{PropertyFormatter.FormatCount(files, "file")}, {PropertyFormatter.FormatCount(folders, "folder")}"));
            Details.Add(new("Total size", PropertyFormatter.FormatSizeExact(size)));
            if (info.PhysicalSize is { } phys)
            {
                Details.Add(new("Archive size", PropertyFormatter.FormatSizeExact(phys)));
                if (size > 0)
                    Details.Add(new("Compression", $"{100.0 * phys / size:0.#}% of original (saved {PropertyFormatter.FormatSize(size > phys ? size - phys : 0)})"));
            }
            Details.Add(new("Encrypted", _session.HeadersEncrypted ? "Yes, including file names" : _session.HasEncryptedItems ? "Yes" : "No"));
            foreach (var (k, v) in info.AllProperties)
                if (k is not ("Path" or "Physical size"))
                    Details.Add(new(k, v));
            SelectionText = "";
            return;
        }

        if (selection.Count == 1)
        {
            var item = selection[0];
            DetailsTitle = item.Name;
            DetailsIcon = item.Icon;
            if (item.IsFolder)
            {
                var m = item.Folder!.Measure();
                Details.Add(new("Type", item.TypeName));
                Details.Add(new("Contains", $"{PropertyFormatter.FormatCount(m.Files, "file")}, {PropertyFormatter.FormatCount(m.Folders, "folder")}"));
                Details.Add(new("Size", PropertyFormatter.FormatSizeExact(m.Size)));
                if (m.Packed > 0)
                    Details.Add(new("Packed size", PropertyFormatter.FormatSizeExact(m.Packed)));
                if (item.Modified is { } mod)
                    Details.Add(new("Modified", mod.ToLocalTime().ToString("G")));
            }
            if (item.Entry is { } entry)
            {
                Details.Add(new("Type", item.TypeName));
                foreach (var (k, v) in _session.GetItemProperties(entry.Index))
                    if (k != "Folder" && !(item.IsFolder && k is "Size" or "Packed size"))
                        Details.Add(new(k, v));
            }
            SelectionText = item.IsFolder ? "1 folder selected" : $"1 item selected · {item.SizeText}";
            return;
        }

        DetailsTitle = $"{selection.Count:N0} items";
        DetailsIcon = null;
        var totalSize = selection.Aggregate(0UL, (s, i) => s + i.Size);
        var totalPacked = selection.Aggregate(0UL, (s, i) => s + (i.PackedSize ?? 0));
        Details.Add(new("Size", PropertyFormatter.FormatSizeExact(totalSize)));
        if (totalPacked > 0)
            Details.Add(new("Packed size", PropertyFormatter.FormatSizeExact(totalPacked)));
        SelectionText = $"{selection.Count:N0} items selected · {PropertyFormatter.FormatSize(totalSize)}";
    }

    // ---------------- extraction ----------------

    private (IReadOnlyCollection<uint>? Indices, string? BasePath) Resolve(IReadOnlyList<ItemViewModel>? items)
    {
        if (items is null || items.Count == 0)
            return (CurrentPath.Length == 0 && !IsSearching ? null : ExtractEngine.CollectIndices(CurrentFolder.Files, CurrentFolder.Folders), CurrentPath);
        var indices = ExtractEngine.CollectIndices(items.Where(i => !i.IsFolder).Select(i => i.Entry!), items.Where(i => i.IsFolder).Select(i => i.Folder!));
        // Search results come from many folders; keep their structure relative to the archive root.
        return (indices, IsSearching ? "" : CurrentPath);
    }

    public async Task<ExtractResult?> ExtractAsync(IReadOnlyList<ItemViewModel>? items, ExtractOptions options, bool openFolder)
    {
        if (_session is null)
            return null;
        var (indices, basePath) = Resolve(items);
        options = new ExtractOptions
        {
            Destination = options.Destination,
            BasePath = basePath,
            Overwrite = options.Overwrite,
            PathMode = options.PathMode,
            ZonePropagation = options.ZonePropagation,
            RestoreTimestamps = options.RestoreTimestamps,
            RestoreSymbolicLinks = options.RestoreSymbolicLinks,
            RestoreAttributes = options.RestoreAttributes,
            Password = options.Password,
        };
        var session = _session;
        var op = new OperationViewModel(OperationKind.Extract, $"Extracting {DisplayName}", new UiPrompts(_host.Dialogs, Parent is null ? FilePath : null));
        try
        {
            var result = await _host.Operations.RunAsync(op, ctx => ExtractEngine.Extract(session, indices, options, ctx),
                r => r.Errors == 0 ? $"Extracted {PropertyFormatter.FormatCount(r.FilesProcessed, "file")} to {r.Destination}" : $"{PropertyFormatter.FormatCount(r.Errors, "error")} · {PropertyFormatter.FormatCount(r.FilesProcessed, "file")}",
                r => r.Destination);
            if (result is null)
                return null;
            if (result.WrongPassword)
            {
                await _host.Dialogs.ShowErrorAsync("Wrong password", "Some files couldn't be decrypted. Check the password and try again.");
                return result;
            }
            if (op.Messages.Count > 0)
                await _host.Dialogs.ShowMessagesAsync(result.Errors > 0 ? "Extraction finished with errors" : "Extraction finished with warnings", op.Messages);
            if (openFolder && result.Errors == 0)
            {
                if (result.TopLevelPaths.Count is > 0 and <= 16)
                    ShellActions.RevealInExplorer(result.TopLevelPaths);
                else
                    ShellActions.OpenFolder(result.Destination);
            }
            return result;
        }
        catch (Exception ex)
        {
            await _host.Dialogs.ShowErrorAsync("Couldn't extract", ex.Message);
            return null;
        }
    }

    public Task ExtractHereAsync(IReadOnlyList<ItemViewModel>? items)
        => ExtractAsync(items, DefaultOptions(FolderForArchive), App.Current.Settings.Current.OpenFolderAfterExtract);

    public Task ExtractSmartAsync(IReadOnlyList<ItemViewModel>? items)
    {
        var dest = _session is null ? FolderForArchive
            : items is { Count: > 0 } ? Path.Combine(FolderForArchive, ExtractPlanner.GetFolderNameFor(DisplayName))
            : ExtractPlanner.GetSmartDestination(_session, FolderForArchive);
        return ExtractAsync(items, DefaultOptions(dest), App.Current.Settings.Current.OpenFolderAfterExtract);
    }

    private static ExtractOptions DefaultOptions(string destination)
    {
        var s = App.Current.Settings.Current;
        return new ExtractOptions
        {
            Destination = destination,
            Overwrite = s.OverwriteMode,
            ZonePropagation = s.ZonePropagation,
            RestoreSymbolicLinks = s.RestoreSymbolicLinks,
        };
    }

    public ExtractOptionsModel CreateExtractOptions(IReadOnlyList<ItemViewModel>? items)
    {
        var folderName = ExtractPlanner.GetFolderNameFor(DisplayName.Split(" › ").Last());
        bool suggest = App.Current.Settings.Current.SmartExtractFolder;
        if (suggest && _session is not null && (items is null || items.Count == 0) && CurrentPath.Length == 0)
            suggest = ExtractPlanner.GetSmartDestination(_session, FolderForArchive) != FolderForArchive;
        bool needsPassword = _session?.HasEncryptedItems == true && _session.Password is null;
        return new ExtractOptionsModel(FolderForArchive, folderName, suggest, needsPassword);
    }

    /// <summary>Extracts to a private temp folder; used by open, drag-out, copy and share.</summary>
    public async Task<IReadOnlyList<string>> ExtractToTempAsync(IReadOnlyList<ItemViewModel> items, bool quiet = false)
    {
        if (_session is null || items.Count == 0)
            return [];
        var temp = TempFiles.NewFolder();
        var (indices, _) = Resolve(items);
        var basePath = items.Count == 1 || !IsSearching ? ParentPathOf(items[0].Path) : "";
        var session = _session;
        var op = new OperationViewModel(OperationKind.Extract, items.Count == 1 ? $"Preparing {items[0].Name}" : $"Preparing {items.Count} items",
            new UiPrompts(_host.Dialogs, Parent is null ? FilePath : null));
        var result = await _host.Operations.RunAsync(op,
            ctx => ExtractEngine.Extract(session, indices, new ExtractOptions { Destination = temp, BasePath = basePath, Overwrite = OverwriteMode.Overwrite, ZonePropagation = ZonePropagation.All }, ctx),
            r => PropertyFormatter.FormatCount(r.FilesProcessed, "file"));
        if (result is null || (result.Errors > 0 && !quiet))
        {
            if (result is not null)
                await _host.Dialogs.ShowMessagesAsync("Some files couldn't be extracted", op.Messages);
            return [];
        }
        return items.Select(i => Path.Combine(temp, PathSanitizer.GetSafeComponents(i.Name).FirstOrDefault() ?? i.Name)).Where(p => File.Exists(p) || Directory.Exists(p)).ToList();
    }

    private static string ParentPathOf(string path)
    {
        int i = path.LastIndexOf('\\');
        return i < 0 ? "" : path[..i];
    }

    /// <summary>Double-click: folders navigate, nested archives open in a new tab, files open with their app.</summary>
    public async Task OpenItemAsync(ItemViewModel item, bool forceOutside = false, bool openWith = false)
    {
        if (item.IsFolder)
        {
            Navigate(item.Path);
            return;
        }
        var paths = await ExtractToTempAsync([item]);
        if (paths.Count == 0)
            return;
        var path = paths[0];
        if (!forceOutside && !openWith && FormatRegistry.LooksLikeArchive(path) && !path.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
        {
            _host.OpenNestedArchive(this, path, item.Path);
            return;
        }
        if (openWith)
            ShellActions.OpenWith(_host.Hwnd, path);
        else
            ShellActions.Launch(path);
        if (CanModify && item.Entry is { } entry)
            WatchForEdits(entry, path);
    }

    /// <summary>Opens a nested archive explicitly ("Open inside").</summary>
    public async Task OpenInsideAsync(ItemViewModel item)
    {
        var paths = await ExtractToTempAsync([item]);
        if (paths.Count == 1)
            _host.OpenNestedArchive(this, paths[0], item.Path);
    }

    private void WatchForEdits(ArchiveEntry entry, string tempPath)
    {
        var watcher = new FileSystemWatcher(Path.GetDirectoryName(tempPath)!, Path.GetFileName(tempPath))
        {
            NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.Size,
            EnableRaisingEvents = true,
        };
        var dispatcher = _host.Dialogs.Dispatcher;
        watcher.Changed += (_, _) => dispatcher.TryEnqueue(() =>
        {
            if (!PendingEdits.Any(p => p.TempPath == tempPath))
                PendingEdits.Add(new PendingEdit(entry.Index, entry.Path, tempPath));
        });
        _watchers.Add(watcher);
    }

    public async Task ApplyEditAsync(PendingEdit edit)
    {
        PendingEdits.Remove(edit);
        var current = _session?.Entries.FirstOrDefault(e => e.Path == edit.ArchivePath);
        if (current is null)
            return;
        await UpdateAsync(new UpdatePlan { Replace = new Dictionary<uint, string> { [current.Index] = edit.TempPath } }, $"Updating {Path.GetFileName(edit.ArchivePath)}");
    }

    public void DiscardEdit(PendingEdit edit) => PendingEdits.Remove(edit);

    public async Task TestAsync()
    {
        if (_session is null)
            return;
        var session = _session;
        var op = new OperationViewModel(OperationKind.Test, $"Testing {DisplayName}", new UiPrompts(_host.Dialogs, Parent is null ? FilePath : null));
        try
        {
            var result = await _host.Operations.RunAsync(op, ctx => ExtractEngine.Test(session, null, ctx),
                r => r.Errors == 0 ? $"No errors · {PropertyFormatter.FormatCount(r.FilesProcessed, "file")} tested" : $"{PropertyFormatter.FormatCount(r.Errors, "error")} found");
            if (result is null)
                return;
            if (result.Errors == 0)
                await _host.Dialogs.ShowErrorAsync("Test passed", $"There are no errors in {DisplayName}.\n\n{PropertyFormatter.FormatCount(result.FilesProcessed, "file")}, {PropertyFormatter.FormatSize(result.Bytes)} verified.");
            else
                await _host.Dialogs.ShowMessagesAsync($"{PropertyFormatter.FormatCount(result.Errors, "error")} in {DisplayName}", op.Messages);
        }
        catch (Exception ex)
        {
            await _host.Dialogs.ShowErrorAsync("Test failed", ex.Message);
        }
    }

    // ---------------- modification ----------------

    public Task AddAsync(IReadOnlyList<string> paths)
        => paths.Count == 0 ? Task.CompletedTask
            : UpdateAsync(new UpdatePlan { Add = paths, TargetFolder = CurrentPath }, paths.Count == 1 ? $"Adding {Path.GetFileName(paths[0])}" : $"Adding {paths.Count} items");

    public async Task DeleteAsync(IReadOnlyList<ItemViewModel> items)
    {
        if (items.Count == 0 || _session is null)
            return;
        if (App.Current.Settings.Current.ConfirmDelete)
        {
            var what = items.Count == 1 ? $"“{items[0].Name}”" : $"these {items.Count} items";
            if (!await _host.Dialogs.ConfirmAsync("Delete from archive?", $"Delete {what} from {DisplayName}? This can't be undone.", "Delete", destructive: true))
                return;
        }
        var (indices, _) = Resolve(items);
        await UpdateAsync(new UpdatePlan { Delete = indices ?? [] }, items.Count == 1 ? $"Deleting {items[0].Name}" : $"Deleting {items.Count} items");
    }

    public async Task RenameAsync(ItemViewModel item)
    {
        var siblings = CurrentFolder.Folders.Select(f => f.Name).Concat(CurrentFolder.Files.Select(f => f.Name)).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var name = await _host.Dialogs.PromptTextAsync("Rename", "New name", item.Name, "Rename", n =>
            n.IndexOfAny(['\\', '/', ':', '*', '?', '"', '<', '>', '|']) >= 0 ? "A name can't contain \\ / : * ? \" < > |"
            : !string.Equals(n, item.Name, StringComparison.OrdinalIgnoreCase) && siblings.Contains(n) ? "There's already an item with that name."
            : null);
        if (name is null || name == item.Name)
            return;
        var parent = ParentPathOf(item.Path);
        var newPath = parent.Length == 0 ? name : parent + "\\" + name;
        await UpdateAsync(new UpdatePlan { Rename = new Dictionary<string, string> { [item.Path] = newPath } }, $"Renaming {item.Name}");
    }

    public async Task NewFolderAsync()
    {
        var name = await _host.Dialogs.PromptTextAsync("New folder", "Folder name", "New folder", "Create", n =>
            n.IndexOfAny(['\\', '/', ':', '*', '?', '"', '<', '>', '|']) >= 0 ? "A name can't contain \\ / : * ? \" < > |"
            : CurrentFolder.GetFolder(n) is not null ? "That folder already exists." : null);
        if (name is null)
            return;
        await UpdateAsync(new UpdatePlan { NewFolders = [CurrentPath.Length == 0 ? name : CurrentPath + "\\" + name] }, $"Creating {name}");
    }

    private async Task UpdateAsync(UpdatePlan plan, string title)
    {
        if (_session is null || !CanModify)
        {
            await _host.Dialogs.ShowErrorAsync("Read-only archive", ReadOnlyReason);
            return;
        }
        var session = _session;
        var keepPath = CurrentPath;
        var password = session.Password;
        var op = new OperationViewModel(OperationKind.Update, title, new UiPrompts(_host.Dialogs, FilePath));
        try
        {
            var result = await _host.Operations.RunAsync(op, ctx => ArchiveWriter.Update(session, plan, null, ctx), r => $"{DisplayName} updated", _ => FilePath);
            if (op.Messages.Count > 0)
                await _host.Dialogs.ShowMessagesAsync("Updated with warnings", op.Messages);
        }
        catch (Exception ex)
        {
            await _host.Dialogs.ShowErrorAsync("Couldn't update the archive", ex.Message);
        }
        // Update() closes the session on success; reopen either way so the view reflects the file on disk.
        session.Dispose();
        _session = null;
        await OpenAsync(password);
        Navigate(keepPath);
    }

    public void Dispose()
    {
        foreach (var w in _watchers)
            w.Dispose();
        _searchCts?.Cancel();
        _session?.Dispose();
        _outer?.Dispose();
    }
}
