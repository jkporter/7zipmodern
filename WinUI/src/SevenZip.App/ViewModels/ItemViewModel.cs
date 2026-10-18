using System.Globalization;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using SevenZip.App.Services;
using SevenZip.Core.Archives;

namespace SevenZip.App.ViewModels;

/// <summary>A row in the archive browser: a file entry or a (possibly synthesised) folder.</summary>
public sealed class ItemViewModel
{
    private ItemViewModel(string name, string path, bool isFolder)
    {
        Name = name;
        Path = path;
        IsFolder = isFolder;
    }

    public string Name { get; }
    /// <summary>Path inside the archive.</summary>
    public string Path { get; }
    public bool IsFolder { get; }
    public ArchiveEntry? Entry { get; private init; }
    public ArchiveFolder? Folder { get; private init; }
    public ulong Size { get; private init; }
    public ulong? PackedSize { get; private init; }
    public DateTime? Modified { get; private init; }
    public string TypeName { get; private init; } = "";
    public ImageSource? Icon { get; private init; }
    public string? Method { get; private init; }
    public bool IsEncrypted { get; private init; }
    public uint? Crc { get; private init; }
    public int ChildCount { get; private init; }
    /// <summary>Folder part of <see cref="Path"/>, shown in search results.</summary>
    public string Location { get; private init; } = "";

    public string SizeText => IsFolder ? (ChildCount == 1 ? "1 item" : $"{ChildCount:N0} items") : PropertyFormatter.FormatSize(Size);
    public string PackedText => PackedSize is { } p && !IsFolder ? PropertyFormatter.FormatSize(p) : "";
    public string ModifiedText => Modified?.ToLocalTime().ToString("g", CultureInfo.CurrentCulture) ?? "";
    public string RatioText => !IsFolder && PackedSize is { } p && Size > 0 ? $"{Math.Min(999, 100.0 * p / Size):0}%" : "";
    public string CrcText => Crc is { } c ? c.ToString("X8", CultureInfo.InvariantCulture) : "";
    public Visibility LockVisibility => IsEncrypted ? Visibility.Visible : Visibility.Collapsed;
    public string AutomationName => IsFolder ? $"{Name}, folder" : $"{Name}, {TypeName}, {SizeText}";

    public static ItemViewModel FromFolder(ArchiveFolder folder, bool showLocation = false)
    {
        var m = folder.Measure();
        return new ItemViewModel(folder.Name, folder.Path, true)
        {
            Folder = folder,
            Entry = folder.Entry,
            Size = m.Size,
            PackedSize = m.Packed,
            Modified = folder.Entry?.Modified ?? folder.LatestModified(),
            TypeName = ShellIcons.GetTypeName(folder.Name, isFolder: true),
            Icon = ShellIcons.GetFolderIcon(),
            ChildCount = folder.Folders.Count + folder.Files.Count,
            Location = showLocation ? folder.Parent?.Path ?? "" : "",
        };
    }

    public static ItemViewModel FromEntry(ArchiveEntry e, bool showLocation = false)
    {
        int slash = e.Path.LastIndexOf('\\');
        return new ItemViewModel(e.Name, e.Path, false)
        {
            Entry = e,
            Size = e.Size,
            PackedSize = e.PackedSize,
            Modified = e.Modified,
            TypeName = ShellIcons.GetTypeName(e.Name, isFolder: false),
            Icon = ShellIcons.GetIcon(e.Name),
            Method = e.Method,
            IsEncrypted = e.IsEncrypted,
            Crc = e.Crc,
            Location = showLocation && slash > 0 ? e.Path[..slash] : "",
        };
    }
}

public sealed record BreadcrumbSegment(string Name, string Path)
{
    public override string ToString() => Name;
}

public sealed record PropertyRow(string Name, string Value);
