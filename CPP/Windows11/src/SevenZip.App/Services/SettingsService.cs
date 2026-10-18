using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using SevenZip.Core.Archives;
using SevenZip.Core.Compression;

namespace SevenZip.App.Services;

public enum AppTheme { System, Light, Dark }
public enum BackdropKind { Mica, MicaAlt, Acrylic }

public sealed class RecentArchive
{
    public required string Path { get; set; }
    public DateTime LastOpened { get; set; }
}

/// <summary>User preferences. Replaces HKCU\Software\7-Zip with a per-user JSON document.</summary>
public sealed class AppSettings
{
    public AppTheme Theme { get; set; } = AppTheme.System;
    public BackdropKind Backdrop { get; set; } = BackdropKind.MicaAlt;

    // Extraction
    public bool SmartExtractFolder { get; set; } = true;
    public bool OpenFolderAfterExtract { get; set; } = true;
    public OverwriteMode OverwriteMode { get; set; } = OverwriteMode.Ask;
    public ZonePropagation ZonePropagation { get; set; } = ZonePropagation.All;
    public bool RestoreSymbolicLinks { get; set; } = true;

    // Compression
    public string DefaultFormat { get; set; } = "7z";
    public CompressionLevel DefaultLevel { get; set; } = CompressionLevel.Normal;
    public bool DefaultEncryptFileNames { get; set; } = true;

    // Browsing
    public bool ShowDetailsPane { get; set; } = true;
    public bool ConfirmDelete { get; set; } = true;
    public bool AutoOpenTarballs { get; set; } = true;

    // Explorer context menu (read by SevenZip.ShellExtension.dll)
    public bool MenuCascaded { get; set; } = true;
    public bool MenuOpen { get; set; } = true;
    public bool MenuExtract { get; set; } = true;
    public bool MenuExtractHere { get; set; } = true;
    public bool MenuExtractSmart { get; set; } = true;
    public bool MenuTest { get; set; } = true;
    public bool MenuCompress { get; set; } = true;
    public bool MenuCompress7z { get; set; } = true;
    public bool MenuCompressZip { get; set; } = true;
    public bool MenuCompressShare { get; set; } = true;
    public bool MenuHash { get; set; } = true;

    // Background work
    public bool NotifyOnCompletion { get; set; } = true;
    public bool EfficiencyModeForBackground { get; set; }
    public bool OfferSavedPasswords { get; set; } = true;

    public List<RecentArchive> Recent { get; set; } = [];
}

[JsonSourceGenerationOptions(WriteIndented = true, UseStringEnumConverter = true)]
[JsonSerializable(typeof(AppSettings))]
internal partial class SettingsJsonContext : JsonSerializerContext;

public sealed class SettingsService
{
    private readonly string _file;
    private readonly Lock _lock = new();

    public SettingsService()
    {
        Folder = GetSettingsFolder();
        Directory.CreateDirectory(Folder);
        _file = Path.Combine(Folder, "settings.json");
        Current = Load();
        WriteShellConfig();
    }

    public string Folder { get; }
    public AppSettings Current { get; }
    public event EventHandler? Changed;

    /// <summary>Packaged: the package's LocalState (cleaned on uninstall). Unpackaged: %LOCALAPPDATA%\7-Zip.</summary>
    private static string GetSettingsFolder()
    {
        if (ShellActions.IsPackaged)
        {
            try { return Windows.Storage.ApplicationData.Current.LocalFolder.Path; }
            catch { /* fall through */ }
        }
        return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "7-Zip WinUI");
    }

    private AppSettings Load()
    {
        try
        {
            if (File.Exists(_file))
                return JsonSerializer.Deserialize(File.ReadAllText(_file), SettingsJsonContext.Default.AppSettings) ?? new AppSettings();
        }
        catch
        {
            // Corrupt settings: start fresh rather than refusing to launch.
        }
        return new AppSettings();
    }

    public void Save()
    {
        lock (_lock)
        {
            var tmp = _file + ".tmp";
            File.WriteAllText(tmp, JsonSerializer.Serialize(Current, SettingsJsonContext.Default.AppSettings));
            File.Move(tmp, _file, overwrite: true);
            WriteShellConfig();
        }
        Changed?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>Tiny key=value file the Explorer extension reads (it must not parse JSON or load .NET).</summary>
    private void WriteShellConfig()
    {
        var s = Current;
        var sb = new StringBuilder();
        void W(string k, bool v) => sb.Append(k).Append('=').Append(v ? '1' : '0').Append("\r\n");
        W("Cascaded", s.MenuCascaded);
        W("Open", s.MenuOpen);
        W("Extract", s.MenuExtract);
        W("ExtractHere", s.MenuExtractHere);
        W("ExtractSmart", s.MenuExtractSmart);
        W("Test", s.MenuTest);
        W("Compress", s.MenuCompress);
        W("Compress7z", s.MenuCompress7z);
        W("CompressZip", s.MenuCompressZip);
        W("CompressShare", s.MenuCompressShare);
        W("Hash", s.MenuHash);
        try { File.WriteAllText(Path.Combine(Folder, "contextmenu.ini"), sb.ToString(), Encoding.Unicode); }
        catch (IOException) { }
    }

    public void AddRecent(string path)
    {
        var list = Current.Recent;
        list.RemoveAll(r => string.Equals(r.Path, path, StringComparison.OrdinalIgnoreCase));
        list.Insert(0, new RecentArchive { Path = path, LastOpened = DateTime.Now });
        if (list.Count > 30)
            list.RemoveRange(30, list.Count - 30);
        Save();
        ShellActions.AddToRecentDocuments(path);
    }

    public void RemoveRecent(string path)
    {
        Current.Recent.RemoveAll(r => string.Equals(r.Path, path, StringComparison.OrdinalIgnoreCase));
        Save();
    }
}
