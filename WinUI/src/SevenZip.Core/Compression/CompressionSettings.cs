namespace SevenZip.Core.Compression;

public enum CompressionLevel
{
    Store = 0,
    Fastest = 1,
    Fast = 3,
    Normal = 5,
    Maximum = 7,
    Ultra = 9,
}

public enum UpdateMode
{
    /// <summary>Add new files, replace existing ones with the same name.</summary>
    AddAndReplace,
    /// <summary>Add new files, replace existing ones only when the file on disk is newer.</summary>
    UpdateAndAdd,
    /// <summary>Only refresh files that already exist in the archive.</summary>
    Freshen,
    /// <summary>Make the archive mirror the source (removes items that no longer exist on disk).</summary>
    Synchronize,
}

public enum CompressPathMode
{
    /// <summary>Paths relative to the folder that contains each selected item.</summary>
    Relative,
    /// <summary>Full paths without the drive letter.</summary>
    Full,
}

/// <summary>Everything the "Create archive" page can configure. Null means "engine default".</summary>
public sealed record CompressionSettings
{
    /// <summary>Engine format name ("7z", "zip", "tar", "gzip", "bzip2", "xz", "wim") or a tarball combo ("tar.gz", "tar.xz", "tar.bz2").</summary>
    public string Format { get; init; } = "7z";
    public CompressionLevel Level { get; init; } = CompressionLevel.Normal;
    public string? Method { get; init; }
    public ulong? DictionarySize { get; init; }
    public uint? WordSize { get; init; }
    /// <summary>null = format default, 0 = non-solid, ulong.MaxValue = fully solid, otherwise block size in bytes.</summary>
    public ulong? SolidBlockSize { get; init; }
    public int? Threads { get; init; }
    /// <summary>Limit engine memory to a percentage of RAM.</summary>
    public int? MemoryLimitPercent { get; init; }

    public string? Password { get; init; }
    public bool EncryptFileNames { get; init; }
    /// <summary>"AES256" (default) or "ZipCrypto" for zip.</summary>
    public string? ZipEncryption { get; init; }

    /// <summary>Split into volumes of this many bytes.</summary>
    public ulong? VolumeSize { get; init; }
    public UpdateMode UpdateMode { get; init; } = UpdateMode.AddAndReplace;
    public CompressPathMode PathMode { get; init; } = CompressPathMode.Relative;
    public bool StoreCreationTime { get; init; }
    public bool StoreAccessTime { get; init; }
    /// <summary>Follow directory symbolic links/junctions instead of storing them as empty folders.</summary>
    public bool FollowLinks { get; init; }
    /// <summary>Free-form engine switches, e.g. "qs=on f=off" (the "Parameters" box of classic 7-Zip).</summary>
    public string? ExtraParameters { get; init; }

    public bool IsTarball => Format.StartsWith("tar.", StringComparison.OrdinalIgnoreCase);

    public static CompressionSettings QuickZip => new() { Format = "zip", Level = CompressionLevel.Normal };
    public static CompressionSettings Quick7z => new() { Format = "7z", Level = CompressionLevel.Normal };
}

public sealed record MethodOption(string Name, string DisplayName);

/// <summary>What each creatable format supports (mirrors the classic CompressDialog g_Formats table).</summary>
public sealed record FormatCapabilities(
    string Id,
    string DisplayName,
    string Extension,
    IReadOnlyList<CompressionLevel> Levels,
    IReadOnlyList<MethodOption> Methods,
    bool Solid,
    bool MultiThread,
    bool Encryption,
    bool EncryptFileNames,
    bool SingleFile,
    string Description)
{
    private static readonly CompressionLevel[] s_allLevels =
        [CompressionLevel.Store, CompressionLevel.Fastest, CompressionLevel.Fast, CompressionLevel.Normal, CompressionLevel.Maximum, CompressionLevel.Ultra];
    private static readonly CompressionLevel[] s_noStore = s_allLevels[1..];

    public static IReadOnlyList<FormatCapabilities> All { get; } =
    [
        new("7z", "7z", "7z", s_allLevels,
            [new("LZMA2", "LZMA2"), new("LZMA", "LZMA"), new("PPMd", "PPMd"), new("BZip2", "BZip2"), new("Deflate", "Deflate"), new("Deflate64", "Deflate64"), new("Copy", "Store (no compression)")],
            Solid: true, MultiThread: true, Encryption: true, EncryptFileNames: true, SingleFile: false,
            "Best compression. AES-256 encryption with optional encrypted file names."),
        new("zip", "ZIP", "zip", [CompressionLevel.Store, CompressionLevel.Fastest, CompressionLevel.Fast, CompressionLevel.Normal, CompressionLevel.Maximum, CompressionLevel.Ultra],
            [new("Deflate", "Deflate"), new("Deflate64", "Deflate64"), new("BZip2", "BZip2"), new("LZMA", "LZMA"), new("PPMd", "PPMd")],
            Solid: false, MultiThread: true, Encryption: true, EncryptFileNames: false, SingleFile: false,
            "Opens everywhere, including Windows' built-in compressed folders."),
        new("tar.gz", "TAR.GZ", "tar.gz", [CompressionLevel.Fastest, CompressionLevel.Normal, CompressionLevel.Maximum, CompressionLevel.Ultra],
            [new("Deflate", "Deflate")], Solid: false, MultiThread: false, Encryption: false, EncryptFileNames: false, SingleFile: false,
            "Tarball compressed with gzip. Common on Linux and macOS."),
        new("tar.xz", "TAR.XZ", "tar.xz", s_noStore,
            [new("LZMA2", "LZMA2")], Solid: true, MultiThread: true, Encryption: false, EncryptFileNames: false, SingleFile: false,
            "Tarball compressed with xz (LZMA2). Small and widely supported on Linux."),
        new("tar.bz2", "TAR.BZ2", "tar.bz2", [CompressionLevel.Fastest, CompressionLevel.Fast, CompressionLevel.Normal, CompressionLevel.Maximum, CompressionLevel.Ultra],
            [new("BZip2", "BZip2")], Solid: false, MultiThread: true, Encryption: false, EncryptFileNames: false, SingleFile: false,
            "Tarball compressed with bzip2."),
        new("tar", "TAR", "tar", [CompressionLevel.Store],
            [new("GNU", "GNU"), new("POSIX", "POSIX")], Solid: false, MultiThread: false, Encryption: false, EncryptFileNames: false, SingleFile: false,
            "Uncompressed tape archive; keeps POSIX metadata."),
        new("wim", "WIM", "wim", [CompressionLevel.Store],
            [], Solid: false, MultiThread: false, Encryption: false, EncryptFileNames: false, SingleFile: false,
            "Windows Imaging Format."),
        new("xz", "XZ", "xz", s_noStore,
            [new("LZMA2", "LZMA2")], Solid: true, MultiThread: true, Encryption: false, EncryptFileNames: false, SingleFile: true,
            "Compresses a single file with LZMA2."),
        new("gzip", "GZIP", "gz", [CompressionLevel.Fastest, CompressionLevel.Normal, CompressionLevel.Maximum, CompressionLevel.Ultra],
            [new("Deflate", "Deflate")], Solid: false, MultiThread: false, Encryption: false, EncryptFileNames: false, SingleFile: true,
            "Compresses a single file with Deflate."),
        new("bzip2", "BZIP2", "bz2", [CompressionLevel.Fastest, CompressionLevel.Fast, CompressionLevel.Normal, CompressionLevel.Maximum, CompressionLevel.Ultra],
            [new("BZip2", "BZip2")], Solid: false, MultiThread: true, Encryption: false, EncryptFileNames: false, SingleFile: true,
            "Compresses a single file with BZip2."),
    ];

    public static FormatCapabilities Get(string id)
        => All.FirstOrDefault(f => string.Equals(f.Id, id, StringComparison.OrdinalIgnoreCase)) ?? All[0];

    public static FormatCapabilities? FromFileName(string fileName)
    {
        var lower = fileName.ToLowerInvariant();
        foreach (var f in All.OrderByDescending(f => f.Extension.Length))
            if (lower.EndsWith("." + f.Extension, StringComparison.Ordinal))
                return f;
        if (lower.EndsWith(".tgz")) return Get("tar.gz");
        if (lower.EndsWith(".txz")) return Get("tar.xz");
        if (lower.EndsWith(".tbz2") || lower.EndsWith(".tbz")) return Get("tar.bz2");
        return null;
    }

    /// <summary>Dictionary choices offered for a method (bytes).</summary>
    public static IReadOnlyList<ulong> GetDictionarySizes(string? method)
    {
        const ulong KB = 1024, MB = 1024 * KB;
        return method?.ToUpperInvariant() switch
        {
            "LZMA" or "LZMA2" or null => [64 * KB, 256 * KB, 1 * MB, 2 * MB, 4 * MB, 8 * MB, 16 * MB, 32 * MB, 64 * MB, 128 * MB, 256 * MB, 512 * MB, 1024 * MB, 1536 * MB],
            "PPMD" => [1 * MB, 2 * MB, 4 * MB, 8 * MB, 16 * MB, 32 * MB, 64 * MB, 128 * MB, 256 * MB, 512 * MB, 1024 * MB],
            "BZIP2" => [100 * KB, 200 * KB, 300 * KB, 400 * KB, 500 * KB, 600 * KB, 700 * KB, 800 * KB, 900 * KB],
            "DEFLATE" => [32 * KB],
            "DEFLATE64" => [64 * KB],
            _ => [],
        };
    }

    /// <summary>Default dictionary per level, matching the engine's LZMA2 defaults.</summary>
    public static ulong GetDefaultDictionary(string? method, CompressionLevel level)
    {
        const ulong MB = 1024 * 1024;
        return method?.ToUpperInvariant() switch
        {
            "PPMD" => level switch { >= CompressionLevel.Ultra => 192 * MB, >= CompressionLevel.Maximum => 64 * MB, >= CompressionLevel.Normal => 16 * MB, _ => 4 * MB },
            "BZIP2" => level >= CompressionLevel.Fast ? 900UL * 1024 : 100UL * 1024,
            "DEFLATE" => 32 * 1024,
            "DEFLATE64" => 64 * 1024,
            _ => level switch
            {
                CompressionLevel.Store => 0,
                CompressionLevel.Fastest => 256 * 1024,
                CompressionLevel.Fast => 4 * MB,
                CompressionLevel.Normal => 16 * MB,
                CompressionLevel.Maximum => 64 * MB,
                _ => 64 * MB,
            },
        };
    }

    /// <summary>Rough memory estimate (bytes) for compressing and decompressing, mirroring 7-Zip's dialog hints.</summary>
    public static (ulong Compress, ulong Decompress) EstimateMemory(string? method, ulong dictionary, int threads)
    {
        const ulong MB = 1024 * 1024;
        threads = Math.Max(1, threads);
        switch (method?.ToUpperInvariant())
        {
            case "PPMD":
                return (dictionary + 2 * MB, dictionary + 2 * MB);
            case "BZIP2":
                return ((ulong)threads * 10 * MB, 7 * MB);
            case "DEFLATE" or "DEFLATE64":
                return ((ulong)threads * 4 * MB, 2 * MB);
            case "COPY":
                return (MB, MB);
            default:
            {
                // LZMA2: bt4 match finder ≈ 11.5 × dict per encoder, two threads per encoder.
                ulong perEncoder = dictionary * 23 / 2 + 6 * MB;
                ulong encoders = (ulong)Math.Max(1, threads / 2);
                if (string.Equals(method, "LZMA", StringComparison.OrdinalIgnoreCase))
                    encoders = 1;
                return (perEncoder * encoders + 2 * MB, dictionary + 2 * MB);
            }
        }
    }
}
