using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using SevenZip.App.Services;
using SevenZip.Core.Archives;
using SevenZip.Core.Compression;
using SevenZip.Core.Operations;

namespace SevenZip.App.ViewModels;

public sealed record ChoiceItem<T>(string Label, T Value)
{
    public override string ToString() => Label;
}

public sealed record SourceItem(string Path)
{
    public string Name => System.IO.Path.GetFileName(Path.TrimEnd('\\')) is { Length: > 0 } n ? n : Path;
    public string Folder => System.IO.Path.GetDirectoryName(Path) ?? "";
    public Microsoft.UI.Xaml.Media.ImageSource? Icon => Directory.Exists(Path) ? ShellIcons.GetFolderIcon() : ShellIcons.GetIcon(Path);
}

/// <summary>The "Create archive" page (successor of the classic Add to Archive dialog).</summary>
public sealed partial class CompressViewModel : ObservableObject
{
    private static readonly ChoiceItem<ulong?>[] s_volumeSizes =
    [
        new("Don't split", null),
        new("10 MB", 10UL << 20),
        new("100 MB", 100UL << 20),
        new("700 MB (CD)", 700UL << 20),
        new("1 GB", 1UL << 30),
        new("4 GB (FAT32 limit)", 4092UL << 20),
        new("4.7 GB (DVD)", 4480UL << 20),
        new("8.5 GB (DVD DL)", 8128UL << 20),
        new("25 GB (Blu-ray)", 23040UL << 20),
    ];

    private bool _nameTouched;
    private string _lastAutoName = "";

    public CompressViewModel(IReadOnlyList<string> sources, string? targetArchive = null)
    {
        var s = App.Current.Settings.Current;
        foreach (var p in sources)
            Sources.Add(new SourceItem(p));
        Formats = FormatCapabilities.All;
        ThreadChoices = [new("Automatic", null), .. Enumerable.Range(1, Environment.ProcessorCount * 2).Select(i => new ChoiceItem<int?>(i == 1 ? "1 thread" : $"{i} threads", i))];
        SelectedThreads = ThreadChoices[0];
        var format = FormatCapabilities.Get(targetArchive is not null ? FormatCapabilities.FromFileName(targetArchive)?.Id ?? s.DefaultFormat : s.DefaultFormat);
        Level = format.Levels.Contains(s.DefaultLevel) ? s.DefaultLevel : format.Levels[^1];
        SelectedFormat = format;
        EncryptFileNames = s.DefaultEncryptFileNames;
        VolumeChoices = s_volumeSizes;
        SelectedVolume = s_volumeSizes[0];
        UpdateModes =
        [
            new("Add and replace files", UpdateMode.AddAndReplace),
            new("Update and add files", UpdateMode.UpdateAndAdd),
            new("Freshen existing files", UpdateMode.Freshen),
            new("Synchronize files", UpdateMode.Synchronize),
        ];
        SelectedUpdateMode = UpdateModes[0];
        PathModes = [new("Relative paths", CompressPathMode.Relative), new("Full paths (no drive letter)", CompressPathMode.Full)];
        SelectedPathMode = PathModes[0];
        ZipEncryptionChoices = [new("AES-256 (recommended)", "AES256"), new("ZipCrypto (legacy, weak)", "ZipCrypto")];
        SelectedZipEncryption = ZipEncryptionChoices[0];

        if (targetArchive is not null)
        {
            ArchivePath = targetArchive;
            _nameTouched = true;
        }
        RefreshMethods();
        SuggestName();
        Sources.CollectionChanged += (_, _) =>
        {
            SuggestName();
            OnPropertyChanged(nameof(SourceSummary));
            OnPropertyChanged(nameof(HasSources));
            OnPropertyChanged(nameof(CanCreate));
            _ = MeasureAsync();
        };
        _ = MeasureAsync();
    }

    public ObservableCollection<SourceItem> Sources { get; } = [];
    public IReadOnlyList<FormatCapabilities> Formats { get; }
    public IReadOnlyList<ChoiceItem<int?>> ThreadChoices { get; }
    public IReadOnlyList<ChoiceItem<ulong?>> VolumeChoices { get; }
    public IReadOnlyList<ChoiceItem<UpdateMode>> UpdateModes { get; }
    public IReadOnlyList<ChoiceItem<CompressPathMode>> PathModes { get; }
    public IReadOnlyList<ChoiceItem<string>> ZipEncryptionChoices { get; }
    public ObservableCollection<ChoiceItem<CompressionLevel>> LevelChoices { get; } = [];
    public ObservableCollection<MethodOption> MethodChoices { get; } = [];
    public ObservableCollection<ChoiceItem<ulong?>> DictionaryChoices { get; } = [];
    public ObservableCollection<ChoiceItem<uint?>> WordSizeChoices { get; } = [];
    public ObservableCollection<ChoiceItem<ulong?>> SolidChoices { get; } = [];

    [ObservableProperty] public partial string ArchivePath { get; set; } = "";
    [ObservableProperty] public partial FormatCapabilities SelectedFormat { get; set; }
    [ObservableProperty] public partial CompressionLevel Level { get; set; }
    [ObservableProperty] public partial ChoiceItem<CompressionLevel>? SelectedLevel { get; set; }
    [ObservableProperty] public partial MethodOption? SelectedMethod { get; set; }
    [ObservableProperty] public partial ChoiceItem<ulong?>? SelectedDictionary { get; set; }
    [ObservableProperty] public partial ChoiceItem<uint?>? SelectedWordSize { get; set; }
    [ObservableProperty] public partial ChoiceItem<ulong?>? SelectedSolid { get; set; }
    [ObservableProperty] public partial ChoiceItem<int?> SelectedThreads { get; set; }
    [ObservableProperty] public partial ChoiceItem<ulong?> SelectedVolume { get; set; }
    [ObservableProperty] public partial string CustomVolume { get; set; } = "";
    [ObservableProperty] public partial ChoiceItem<UpdateMode> SelectedUpdateMode { get; set; }
    [ObservableProperty] public partial ChoiceItem<CompressPathMode> SelectedPathMode { get; set; }
    [ObservableProperty] public partial ChoiceItem<string> SelectedZipEncryption { get; set; }
    [ObservableProperty] public partial string Password { get; set; } = "";
    [ObservableProperty] public partial string ConfirmPassword { get; set; } = "";
    [ObservableProperty] public partial bool EncryptFileNames { get; set; }
    [ObservableProperty] public partial bool StoreCreationTime { get; set; }
    [ObservableProperty] public partial bool StoreAccessTime { get; set; }
    [ObservableProperty] public partial bool FollowLinks { get; set; }
    [ObservableProperty] public partial bool DeleteAfter { get; set; }
    [ObservableProperty] public partial bool ShareAfter { get; set; }
    [ObservableProperty] public partial string Parameters { get; set; } = "";
    [ObservableProperty] public partial string TotalSizeText { get; private set; } = "Calculating…";
    [ObservableProperty] public partial bool IsWorking { get; set; }

    public bool HasSources => Sources.Count > 0;
    public string SourceSummary => Sources.Count == 1 ? Sources[0].Name : $"{Sources.Count} items";
    public string FormatDescription => SelectedFormat.Description;
    public bool ShowMethodOptions => SelectedFormat.Methods.Count > 1 || DictionaryChoices.Count > 1;
    public bool ShowSolid => SelectedFormat.Solid && SelectedFormat.Id == "7z";
    public bool ShowThreads => SelectedFormat.MultiThread;
    public bool ShowEncryption => SelectedFormat.Encryption;
    public bool ShowEncryptNames => SelectedFormat.EncryptFileNames;
    public bool ShowZipEncryption => SelectedFormat.Id == "zip";
    public bool ShowVolumes => !SelectedFormat.IsTarballOrSingle();

    public string? PasswordError => Password.Length > 0 && ConfirmPassword.Length > 0 && Password != ConfirmPassword ? "Passwords don't match." : null;
    public string PasswordHint => Password.Length == 0 ? ""
        : Password.Length < 8 ? "Short passwords can be guessed quickly. Use 12+ characters."
        : SelectedFormat.Id == "zip" && SelectedZipEncryption.Value == "ZipCrypto" ? "ZipCrypto is easy to break. Prefer AES-256."
        : SelectedFormat.Id == "zip" ? "File names in ZIP archives stay visible. Use 7z to hide them."
        : "";

    public bool CanCreate => HasSources && ArchivePath.Length > 0 && PasswordError is null && !IsWorking
                             && (!SelectedFormat.SingleFile || Sources.Count == 1 && File.Exists(Sources[0].Path));

    public string MemoryText
    {
        get
        {
            if (Level == CompressionLevel.Store)
                return "No compression: files are stored as-is.";
            var method = SelectedMethod?.Name ?? SelectedFormat.Methods.FirstOrDefault()?.Name;
            var dict = SelectedDictionary?.Value ?? FormatCapabilities.GetDefaultDictionary(method, Level);
            var threads = SelectedThreads.Value ?? Environment.ProcessorCount;
            var (c, d) = FormatCapabilities.EstimateMemory(method, dict, threads);
            return $"Needs about {PropertyFormatter.FormatSize(c)} of memory to compress and {PropertyFormatter.FormatSize(d)} to extract.";
        }
    }

    // ---- reactions ----

    partial void OnSelectedFormatChanged(FormatCapabilities value)
    {
        LevelChoices.Clear();
        foreach (var l in value.Levels)
            LevelChoices.Add(new(LevelLabel(l), l));
        SelectedLevel = LevelChoices.FirstOrDefault(l => l.Value == Level) ?? LevelChoices.FirstOrDefault(l => l.Value == CompressionLevel.Normal) ?? LevelChoices[0];
        RefreshMethods();
        // Keep the user's chosen name but swap the extension.
        if (ArchivePath.Length > 0)
        {
            var stem = StripArchiveExtensions(ArchivePath);
            ArchivePath = stem + "." + value.Extension;
            _lastAutoName = _nameTouched ? _lastAutoName : ArchivePath;
        }
        OnPropertyChanged(nameof(FormatDescription));
        OnPropertyChanged(nameof(ShowSolid));
        OnPropertyChanged(nameof(ShowThreads));
        OnPropertyChanged(nameof(ShowEncryption));
        OnPropertyChanged(nameof(ShowEncryptNames));
        OnPropertyChanged(nameof(ShowZipEncryption));
        OnPropertyChanged(nameof(ShowVolumes));
        OnPropertyChanged(nameof(PasswordHint));
        OnPropertyChanged(nameof(CanCreate));
    }

    partial void OnSelectedLevelChanged(ChoiceItem<CompressionLevel>? value)
    {
        if (value is not null)
            Level = value.Value;
        RefreshDictionary();
        OnPropertyChanged(nameof(MemoryText));
    }

    partial void OnSelectedMethodChanged(MethodOption? value)
    {
        RefreshDictionary();
        OnPropertyChanged(nameof(MemoryText));
    }

    partial void OnSelectedDictionaryChanged(ChoiceItem<ulong?>? value) => OnPropertyChanged(nameof(MemoryText));
    partial void OnSelectedThreadsChanged(ChoiceItem<int?> value) => OnPropertyChanged(nameof(MemoryText));
    partial void OnPasswordChanged(string value) { OnPropertyChanged(nameof(PasswordError)); OnPropertyChanged(nameof(PasswordHint)); OnPropertyChanged(nameof(CanCreate)); }
    partial void OnConfirmPasswordChanged(string value) { OnPropertyChanged(nameof(PasswordError)); OnPropertyChanged(nameof(CanCreate)); }
    partial void OnSelectedZipEncryptionChanged(ChoiceItem<string> value) => OnPropertyChanged(nameof(PasswordHint));
    partial void OnArchivePathChanged(string value)
    {
        if (value != _lastAutoName)
            _nameTouched = true;
        OnPropertyChanged(nameof(CanCreate));
    }
    partial void OnIsWorkingChanged(bool value) => OnPropertyChanged(nameof(CanCreate));

    private void RefreshMethods()
    {
        MethodChoices.Clear();
        foreach (var m in SelectedFormat.Methods)
            MethodChoices.Add(m);
        SelectedMethod = MethodChoices.FirstOrDefault();

        SolidChoices.Clear();
        SolidChoices.Add(new("Automatic", null));
        SolidChoices.Add(new("Non-solid (each file separate)", 0));
        foreach (var (label, size) in new[] { ("16 MB", 16UL << 20), ("64 MB", 64UL << 20), ("256 MB", 256UL << 20), ("1 GB", 1UL << 30), ("4 GB", 4UL << 30), ("16 GB", 16UL << 30) })
            SolidChoices.Add(new(label, size));
        SolidChoices.Add(new("Solid (smallest)", ulong.MaxValue));
        SelectedSolid = SolidChoices[0];
        OnPropertyChanged(nameof(ShowMethodOptions));
    }

    private void RefreshDictionary()
    {
        var method = SelectedMethod?.Name;
        DictionaryChoices.Clear();
        var def = FormatCapabilities.GetDefaultDictionary(method, Level);
        DictionaryChoices.Add(new($"Automatic ({PropertyFormatter.FormatSize(def)})", null));
        foreach (var size in FormatCapabilities.GetDictionarySizes(method))
            DictionaryChoices.Add(new(PropertyFormatter.FormatSize(size), size));
        SelectedDictionary = DictionaryChoices[0];

        WordSizeChoices.Clear();
        WordSizeChoices.Add(new("Automatic", null));
        bool ppmd = string.Equals(method, "PPMd", StringComparison.OrdinalIgnoreCase);
        uint[] sizes = ppmd ? [2, 3, 4, 6, 8, 10, 12, 16, 20, 24, 32] : [8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 273];
        if (method?.ToUpperInvariant() is "LZMA" or "LZMA2" or "PPMD" or "DEFLATE" or "DEFLATE64")
            foreach (var w in sizes)
                WordSizeChoices.Add(new(w.ToString(), w));
        SelectedWordSize = WordSizeChoices[0];
        OnPropertyChanged(nameof(ShowMethodOptions));
    }

    private static string LevelLabel(CompressionLevel l) => l switch
    {
        CompressionLevel.Store => "Store (no compression)",
        CompressionLevel.Fastest => "Fastest",
        CompressionLevel.Fast => "Fast",
        CompressionLevel.Normal => "Normal",
        CompressionLevel.Maximum => "Maximum",
        _ => "Ultra (smallest, slowest)",
    };

    private void SuggestName()
    {
        if (_nameTouched || Sources.Count == 0)
            return;
        var first = Sources[0].Path.TrimEnd('\\');
        var folder = Path.GetDirectoryName(first) ?? first;
        string stem = Sources.Count == 1
            ? (Directory.Exists(first) ? Path.GetFileName(first) : Path.GetFileNameWithoutExtension(first))
            : Path.GetFileName(folder) is { Length: > 0 } parent ? parent : "Archive";
        if (string.IsNullOrEmpty(stem))
            stem = "Archive";
        _lastAutoName = Path.Combine(folder, stem + "." + SelectedFormat.Extension);
        ArchivePath = _lastAutoName;
    }

    private static string StripArchiveExtensions(string path)
    {
        var dir = Path.GetDirectoryName(path) ?? "";
        var name = Path.GetFileName(path);
        foreach (var f in FormatCapabilities.All.OrderByDescending(f => f.Extension.Length))
        {
            if (name.EndsWith("." + f.Extension, StringComparison.OrdinalIgnoreCase))
            {
                name = name[..^(f.Extension.Length + 1)];
                break;
            }
        }
        return Path.Combine(dir, name);
    }

    private async Task MeasureAsync()
    {
        var paths = Sources.Select(s => s.Path).ToList();
        TotalSizeText = "Calculating…";
        var (size, files) = await Task.Run(() =>
        {
            ulong total = 0;
            int count = 0;
            foreach (var p in paths)
            {
                try
                {
                    if (File.Exists(p))
                    {
                        total += (ulong)new FileInfo(p).Length;
                        count++;
                    }
                    else if (Directory.Exists(p))
                    {
                        foreach (var f in new DirectoryInfo(p).EnumerateFiles("*", new EnumerationOptions { RecurseSubdirectories = true, IgnoreInaccessible = true, AttributesToSkip = 0 }))
                        {
                            total += (ulong)f.Length;
                            if (++count > 2_000_000) break;
                        }
                    }
                }
                catch { }
            }
            return (total, count);
        });
        TotalSizeText = $"{PropertyFormatter.FormatCount(files, "file")} · {PropertyFormatter.FormatSize(size)}";
    }

    public CompressionSettings BuildSettings()
    {
        ulong? volume = SelectedVolume.Value;
        if (ShowVolumes && CustomVolume.Trim().Length > 0 && TryParseSize(CustomVolume, out var custom))
            volume = custom;
        return new CompressionSettings
        {
            Format = SelectedFormat.Id,
            Level = Level,
            Method = SelectedMethod?.Name == SelectedFormat.Methods.FirstOrDefault()?.Name ? null : SelectedMethod?.Name,
            DictionarySize = SelectedDictionary?.Value,
            WordSize = SelectedWordSize?.Value,
            SolidBlockSize = ShowSolid ? SelectedSolid?.Value : null,
            Threads = ShowThreads ? SelectedThreads.Value : null,
            Password = ShowEncryption && Password.Length > 0 ? Password : null,
            EncryptFileNames = ShowEncryptNames && EncryptFileNames,
            ZipEncryption = ShowZipEncryption ? SelectedZipEncryption.Value : null,
            VolumeSize = ShowVolumes ? volume : null,
            UpdateMode = SelectedUpdateMode.Value,
            PathMode = SelectedPathMode.Value,
            StoreCreationTime = StoreCreationTime,
            StoreAccessTime = StoreAccessTime,
            FollowLinks = FollowLinks,
            ExtraParameters = Parameters.Trim().Length > 0 ? Parameters.Trim() : null,
        };
    }

    /// <summary>"650m", "4.5g", "100000" (bytes) → bytes.</summary>
    public static bool TryParseSize(string text, out ulong bytes)
    {
        bytes = 0;
        text = text.Trim().ToLowerInvariant().Replace(" ", "").TrimEnd('b');
        if (text.Length == 0)
            return false;
        double mul = 1;
        switch (text[^1])
        {
            case 'k': mul = 1 << 10; text = text[..^1]; break;
            case 'm': mul = 1 << 20; text = text[..^1]; break;
            case 'g': mul = 1 << 30; text = text[..^1]; break;
            case 't': mul = 1UL << 40; text = text[..^1]; break;
        }
        if (!double.TryParse(text, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var v) || v <= 0)
            return false;
        bytes = (ulong)(v * mul);
        return bytes >= 1024;
    }

    /// <summary>Runs the job; returns the created files, or null when cancelled/failed.</summary>
    public async Task<CompressResult?> CreateAsync(IWindowHost host)
    {
        var settings = BuildSettings();
        var target = ArchivePath.Trim();
        var sources = Sources.Select(s => s.Path).ToList();
        var dir = Path.GetDirectoryName(target);
        if (dir is null || !Path.IsPathFullyQualified(target))
        {
            await host.Dialogs.ShowErrorAsync("Choose a location", "Enter a full path for the new archive, or use Browse.");
            return null;
        }

        // Adding to an existing archive of the same format updates it (classic 7-Zip behaviour).
        bool update = File.Exists(target) && settings.VolumeSize is null && !settings.IsTarball && !SelectedFormat.SingleFile;
        if (File.Exists(target))
        {
            var msg = update
                ? $"“{Path.GetFileName(target)}” already exists. Add the files to it, or replace it with a new archive?"
                : $"“{Path.GetFileName(target)}” already exists. Replace it?";
            var dialog = new Microsoft.UI.Xaml.Controls.ContentDialog
            {
                Title = "Archive already exists",
                Content = msg,
                PrimaryButtonText = update ? "Add to it" : "Replace",
                SecondaryButtonText = update ? "Replace" : null,
                CloseButtonText = "Cancel",
                DefaultButton = Microsoft.UI.Xaml.Controls.ContentDialogButton.Primary,
            };
            var answer = await host.Dialogs.ShowAsync(dialog);
            if (answer == Microsoft.UI.Xaml.Controls.ContentDialogResult.None)
                return null;
            update = update && answer == Microsoft.UI.Xaml.Controls.ContentDialogResult.Primary;
        }

        IsWorking = true;
        var op = new OperationViewModel(update ? OperationKind.Update : OperationKind.Compress, $"Creating {Path.GetFileName(target)}", new UiPrompts(host.Dialogs, target))
        {
            EfficiencyMode = App.Current.Settings.Current.EfficiencyModeForBackground,
        };
        try
        {
            var result = await host.Operations.RunAsync(op, ctx =>
            {
                if (!update)
                    return ArchiveWriter.Create(target, sources, settings, ctx);
                var session = ArchiveSession.Open(target, new ArchiveOpenOptions { Password = settings.Password }, ctx);
                try { return ArchiveWriter.Update(session, new UpdatePlan { Add = sources }, settings, ctx); }
                finally { session.Dispose(); }
            },
            r => $"{PropertyFormatter.FormatSize(r.OutputBytes)} · {r.Ratio:P0} of original",
            r => r.OutputFiles.FirstOrDefault());

            if (result is null)
                return null;
            if (op.Messages.Count > 0)
                await host.Dialogs.ShowMessagesAsync("Archive created with warnings", op.Messages);
            if (DeleteAfter && result.Skipped == 0 && !op.Context.HasErrors)
                ShellActions.Recycle(sources, host.Hwnd);
            return result;
        }
        catch (Exception ex)
        {
            await host.Dialogs.ShowErrorAsync("Couldn't create the archive", ex.Message);
            return null;
        }
        finally
        {
            IsWorking = false;
        }
    }
}

internal static class FormatCapabilitiesExtensions
{
    public static bool IsTarballOrSingle(this FormatCapabilities f) => f.SingleFile || f.Id.StartsWith("tar.", StringComparison.Ordinal);
}
