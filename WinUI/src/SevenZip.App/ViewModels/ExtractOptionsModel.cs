using CommunityToolkit.Mvvm.ComponentModel;
using SevenZip.Core.Archives;

namespace SevenZip.App.ViewModels;

/// <summary>State of the "Extract" options (dialog in the main window, or the compact shell window).</summary>
public sealed partial class ExtractOptionsModel : ObservableObject
{
    private readonly string _parentFolder;
    private readonly string _folderName;

    public ExtractOptionsModel(string parentFolder, string folderName, bool suggestSubfolder, bool needsPassword)
    {
        var s = App.Current.Settings.Current;
        _parentFolder = parentFolder;
        _folderName = folderName;
        CreateSubfolder = suggestSubfolder;
        Destination = suggestSubfolder ? Path.Combine(parentFolder, folderName) : parentFolder;
        NeedsPassword = needsPassword;
        OverwriteIndex = (int)s.OverwriteMode;
        OpenFolderWhenDone = s.OpenFolderAfterExtract;
        ZoneIndex = (int)s.ZonePropagation;
        RestoreSymbolicLinks = s.RestoreSymbolicLinks;
    }

    public string FolderName => _folderName;
    public string SubfolderLabel => $"Extract into a new folder named \u201C{_folderName}\u201D";

    [ObservableProperty] public partial string Destination { get; set; }
    [ObservableProperty] public partial bool CreateSubfolder { get; set; }
    /// <summary>Index into: Ask, Replace, Skip, Keep both, Rename existing (matches <see cref="OverwriteMode"/>).</summary>
    [ObservableProperty] public partial int OverwriteIndex { get; set; }
    /// <summary>0 = keep folders, 1 = flatten.</summary>
    [ObservableProperty] public partial int PathModeIndex { get; set; }
    [ObservableProperty] public partial bool OpenFolderWhenDone { get; set; }
    /// <summary>Index into <see cref="ZonePropagation"/>.</summary>
    [ObservableProperty] public partial int ZoneIndex { get; set; }
    [ObservableProperty] public partial bool RestoreTimestamps { get; set; } = true;
    [ObservableProperty] public partial bool RestoreSymbolicLinks { get; set; }
    [ObservableProperty] public partial bool NeedsPassword { get; set; }
    [ObservableProperty] public partial string Password { get; set; } = "";

    partial void OnCreateSubfolderChanged(bool value)
    {
        // Keep the destination in sync while the user hasn't typed a custom location.
        var withFolder = Path.Combine(_parentFolder, _folderName);
        if (value && string.Equals(Destination, _parentFolder, StringComparison.OrdinalIgnoreCase))
            Destination = withFolder;
        else if (!value && string.Equals(Destination, withFolder, StringComparison.OrdinalIgnoreCase))
            Destination = _parentFolder;
    }

    public ExtractOptions ToOptions(string? basePath) => new()
    {
        Destination = Destination,
        BasePath = basePath,
        Overwrite = (OverwriteMode)OverwriteIndex,
        PathMode = PathModeIndex == 1 ? ExtractPathMode.NoPaths : ExtractPathMode.FullPaths,
        ZonePropagation = (ZonePropagation)ZoneIndex,
        RestoreTimestamps = RestoreTimestamps,
        RestoreSymbolicLinks = RestoreSymbolicLinks,
        Password = NeedsPassword && Password.Length > 0 ? Password : null,
    };

    public string? Validate()
    {
        if (string.IsNullOrWhiteSpace(Destination))
            return "Choose where to extract the files.";
        try
        {
            if (!Path.IsPathFullyQualified(Destination))
                return "Enter a full folder path, for example C:\\Users\\you\\Downloads.";
        }
        catch
        {
            return "That folder path isn't valid.";
        }
        return null;
    }
}
