namespace SevenZip.Core.Archives;

public enum ExtractPathMode
{
    /// <summary>Recreate the folder structure stored in the archive (relative to <see cref="ExtractOptions.BasePath"/>).</summary>
    FullPaths,
    /// <summary>Flatten everything into the destination folder.</summary>
    NoPaths,
}

public enum OverwriteMode { Ask, Overwrite, Skip, RenameNew, RenameExisting }

/// <summary>Mark-of-the-Web propagation (the "Zone.Id" option of 7-Zip, on by default here).</summary>
public enum ZonePropagation { Off, All, OfficeFilesOnly }

public sealed class ExtractOptions
{
    public required string Destination { get; init; }
    public ExtractPathMode PathMode { get; init; } = ExtractPathMode.FullPaths;
    /// <summary>Folder inside the archive to treat as the root (the folder the user is browsing).</summary>
    public string? BasePath { get; init; }
    public OverwriteMode Overwrite { get; init; } = OverwriteMode.Ask;
    public bool RestoreTimestamps { get; init; } = true;
    public bool RestoreAttributes { get; init; } = true;
    public bool ExtractAlternateStreams { get; init; }
    public bool RestoreSymbolicLinks { get; init; } = true;
    public ZonePropagation ZonePropagation { get; init; } = ZonePropagation.All;
    public string? Password { get; init; }
}

public sealed class ExtractResult
{
    public required string Destination { get; init; }
    public int FilesProcessed { get; init; }
    public int Folders { get; init; }
    public int Skipped { get; init; }
    public int Errors { get; init; }
    public ulong Bytes { get; init; }
    public bool WrongPassword { get; init; }
    /// <summary>Distinct top-level items created in the destination (for "show in folder", drag-out).</summary>
    public required IReadOnlyList<string> TopLevelPaths { get; init; }
}

public static class ExtractPlanner
{
    /// <summary>
    /// "Smart" destination: if everything in the archive already lives under one top-level folder (or the
    /// archive holds a single file) extract straight into <paramref name="parentFolder"/>; otherwise
    /// create a folder named after the archive. Avoids both "foo\foo\…" and loose files spilling out.
    /// </summary>
    public static string GetSmartDestination(ArchiveSession session, string parentFolder)
    {
        var root = session.Root;
        int topLevel = root.Folders.Count + root.Files.Count;
        if (topLevel == 1)
            return parentFolder;
        return Path.Combine(parentFolder, GetFolderNameFor(session.DisplayName));
    }

    /// <summary>"photos.tar.gz" → "photos", "data.7z.001" → "data".</summary>
    public static string GetFolderNameFor(string archiveName)
    {
        var name = Path.GetFileName(archiveName);
        for (int i = 0; i < 3; i++)
        {
            var ext = Path.GetExtension(name);
            if (ext.Length == 0)
                break;
            bool known = ext.Length == 4 && ext.Skip(1).All(char.IsAsciiDigit)
                         || ext.Equals(".tar", StringComparison.OrdinalIgnoreCase)
                         || Formats.FormatRegistry.LooksLikeArchive(name);
            if (!known)
                break;
            name = Path.GetFileNameWithoutExtension(name);
        }
        if (name.EndsWith(".part1", StringComparison.OrdinalIgnoreCase) || name.EndsWith(".part01", StringComparison.OrdinalIgnoreCase))
            name = name[..name.LastIndexOf(".part", StringComparison.OrdinalIgnoreCase)];
        return name.Length == 0 ? "Extracted" : name;
    }
}
