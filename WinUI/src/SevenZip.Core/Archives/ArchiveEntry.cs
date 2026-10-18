namespace SevenZip.Core.Archives;

/// <summary>One item as reported by the archive handler.</summary>
public sealed class ArchiveEntry
{
    public required uint Index { get; init; }
    /// <summary>Normalised path using '\' separators, never rooted.</summary>
    public required string Path { get; init; }
    public required bool IsDirectory { get; init; }
    public ulong Size { get; init; }
    public ulong? PackedSize { get; init; }
    public DateTime? Modified { get; init; }
    public DateTime? Created { get; init; }
    public DateTime? Accessed { get; init; }
    public uint? Attributes { get; init; }
    public uint? Crc { get; init; }
    public string? Method { get; init; }
    public bool IsEncrypted { get; init; }
    public bool IsAltStream { get; init; }
    public string? Comment { get; init; }
    public string? SymbolicLink { get; init; }

    public string Name
    {
        get
        {
            int i = Path.LastIndexOf('\\');
            return i < 0 ? Path : Path[(i + 1)..];
        }
    }
}

/// <summary>Folder view over the flat item list. Folders that have no explicit entry are synthesised.</summary>
public sealed class ArchiveFolder
{
    private readonly Dictionary<string, ArchiveFolder> _folders = new(StringComparer.Ordinal);
    private readonly List<ArchiveEntry> _files = [];

    internal ArchiveFolder(string name, string path, ArchiveFolder? parent)
    {
        Name = name;
        Path = path;
        Parent = parent;
    }

    public string Name { get; }
    /// <summary>Path inside the archive, '' for the root.</summary>
    public string Path { get; }
    public ArchiveFolder? Parent { get; }
    /// <summary>The explicit directory entry, when the archive stores one.</summary>
    public ArchiveEntry? Entry { get; internal set; }

    public IReadOnlyCollection<ArchiveFolder> Folders => _folders.Values;
    public IReadOnlyList<ArchiveEntry> Files => _files;

    public ArchiveFolder? GetFolder(string name) => _folders.GetValueOrDefault(name);

    internal ArchiveFolder GetOrAddFolder(string name)
    {
        if (!_folders.TryGetValue(name, out var folder))
        {
            folder = new ArchiveFolder(name, Path.Length == 0 ? name : Path + "\\" + name, this);
            _folders.Add(name, folder);
        }
        return folder;
    }

    internal void AddFile(ArchiveEntry entry) => _files.Add(entry);

    /// <summary>Resolves "a\b\c" relative to this folder.</summary>
    public ArchiveFolder? Navigate(string relativePath)
    {
        var current = this;
        foreach (var part in relativePath.Split('\\', StringSplitOptions.RemoveEmptyEntries))
        {
            current = current.GetFolder(part);
            if (current is null)
                return null;
        }
        return current;
    }

    public IEnumerable<ArchiveEntry> EnumerateEntriesRecursive()
    {
        if (Entry is not null)
            yield return Entry;
        foreach (var f in _files)
            yield return f;
        foreach (var sub in _folders.Values)
            foreach (var e in sub.EnumerateEntriesRecursive())
                yield return e;
    }

    public (ulong Size, ulong Packed, int Files, int Folders) Measure()
    {
        ulong size = 0, packed = 0;
        int files = 0, folders = 0;
        foreach (var f in _files)
        {
            size += f.Size;
            packed += f.PackedSize ?? 0;
            files++;
        }
        foreach (var sub in _folders.Values)
        {
            var m = sub.Measure();
            size += m.Size;
            packed += m.Packed;
            files += m.Files;
            folders += m.Folders + 1;
        }
        return (size, packed, files, folders);
    }

    public DateTime? LatestModified()
    {
        DateTime? latest = Entry?.Modified;
        foreach (var f in _files)
            if (f.Modified > latest || latest is null)
                latest = f.Modified;
        return latest;
    }

    internal static ArchiveFolder Build(IReadOnlyList<ArchiveEntry> entries)
    {
        var root = new ArchiveFolder("", "", null);
        foreach (var entry in entries)
        {
            var parts = entry.Path.Split('\\', StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length == 0)
                continue;
            var folder = root;
            for (int i = 0; i < parts.Length - 1; i++)
                folder = folder.GetOrAddFolder(parts[i]);
            if (entry.IsDirectory)
                folder.GetOrAddFolder(parts[^1]).Entry = entry;
            else
                folder.AddFile(entry);
        }
        return root;
    }
}

/// <summary>Archive-level information (format, sizes, integrity flags).</summary>
public sealed class ArchiveInfo
{
    public required string FormatName { get; init; }
    public ulong? PhysicalSize { get; init; }
    public ulong? HeadersSize { get; init; }
    public string? Method { get; init; }
    public bool? IsSolid { get; init; }
    public uint? Blocks { get; init; }
    public uint? Volumes { get; init; }
    public long? Offset { get; init; }
    public string? Comment { get; init; }
    public bool HeadersEncrypted { get; init; }
    public uint ErrorFlags { get; init; }
    public uint WarningFlags { get; init; }
    public string? Error { get; init; }
    public string? Warning { get; init; }
    /// <summary>Every archive property the handler reports, for the Properties pane.</summary>
    public required IReadOnlyList<KeyValuePair<string, string>> AllProperties { get; init; }

    public bool HasErrors => ErrorFlags != 0 || !string.IsNullOrEmpty(Error);
    public bool HasWarnings => WarningFlags != 0 || !string.IsNullOrEmpty(Warning);

    public static IEnumerable<string> DescribeErrorFlags(uint flags)
    {
        string[] names =
        [
            "Not an archive", "Headers error", "Encrypted headers error", "Unavailable start of archive",
            "Unconfirmed start of archive", "Unexpected end of archive", "There is data after the end of archive",
            "Unsupported method", "Unsupported feature", "Data error", "CRC error",
        ];
        for (int i = 0; i < names.Length; i++)
            if ((flags & (1u << i)) != 0)
                yield return names[i];
    }
}
