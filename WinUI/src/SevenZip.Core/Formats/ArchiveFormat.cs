using SevenZip.Core.Interop;

namespace SevenZip.Core.Formats;

/// <summary>A format handler registered inside 7z.dll (queried live, never hard-coded).</summary>
public sealed class ArchiveFormat
{
    public required uint Index { get; init; }
    public required string Name { get; init; }
    public required Guid ClassId { get; init; }
    /// <summary>Extensions without dots, lower case, in the order the handler lists them.</summary>
    public required IReadOnlyList<string> Extensions { get; init; }
    /// <summary>Parallel to <see cref="Extensions"/>; "*" means none (e.g. "tgz" → ".tar").</summary>
    public required IReadOnlyList<string> AddExtensions { get; init; }
    public required bool CanUpdate { get; init; }
    public required ArchiveFormatFlags Flags { get; init; }
    public required IReadOnlyList<byte[]> Signatures { get; init; }
    public required uint SignatureOffset { get; init; }

    public string DefaultExtension => Extensions.Count > 0 ? Extensions[0] : Name.ToLowerInvariant();

    public bool MatchesExtension(string extension)
    {
        extension = extension.TrimStart('.');
        foreach (var e in Extensions)
            if (string.Equals(e, extension, StringComparison.OrdinalIgnoreCase))
                return true;
        return false;
    }

    public bool MatchesSignature(ReadOnlySpan<byte> header)
    {
        foreach (var sig in Signatures)
        {
            if (sig.Length == 0 || SignatureOffset + sig.Length > header.Length)
                continue;
            if (header.Slice((int)SignatureOffset, sig.Length).SequenceEqual(sig))
                return true;
        }
        return false;
    }

    /// <summary>Name of the single item in a stream archive (e.g. "backup.tgz" → "backup.tar").</summary>
    public string GetDefaultItemName(string archiveFileName)
    {
        var ext = Path.GetExtension(archiveFileName).TrimStart('.');
        var stem = Path.GetFileNameWithoutExtension(archiveFileName);
        for (int i = 0; i < Extensions.Count; i++)
        {
            if (!string.Equals(Extensions[i], ext, StringComparison.OrdinalIgnoreCase))
                continue;
            var add = i < AddExtensions.Count ? AddExtensions[i] : "*";
            return add == "*" ? stem : stem + add;
        }
        return ext.Length == 0 ? archiveFileName + "~" : stem;
    }

    public override string ToString() => Name;
}

public static class FormatRegistry
{
    private static IReadOnlyList<ArchiveFormat>? s_formats;

    public static IReadOnlyList<ArchiveFormat> All => s_formats ??= Load();

    public static ArchiveFormat? Find(string name)
        => All.FirstOrDefault(f => string.Equals(f.Name, name, StringComparison.OrdinalIgnoreCase));

    public static ArchiveFormat Get(string name)
        => Find(name) ?? throw new NotSupportedException($"The engine has no '{name}' handler.");

    /// <summary>All extensions the engine can open; used for file pickers and file associations.</summary>
    public static IEnumerable<string> AllExtensions
        => All.SelectMany(f => f.Extensions).Distinct(StringComparer.OrdinalIgnoreCase).Order(StringComparer.OrdinalIgnoreCase);

    public static bool LooksLikeArchive(string path)
    {
        var ext = Path.GetExtension(path);
        if (ext.Length < 2)
            return false;
        // Numbered split volumes (.001) and multi-part rar are handled by Split/Rar.
        if (ext.Length == 4 && char.IsAsciiDigit(ext[1]) && char.IsAsciiDigit(ext[2]) && char.IsAsciiDigit(ext[3]))
            return true;
        return All.Any(f => (f.Flags & ArchiveFormatFlags.PreArc) == 0 && f.MatchesExtension(ext));
    }

    private static unsafe IReadOnlyList<ArchiveFormat> Load()
    {
        uint count = SevenZipEngine.GetNumberOfFormats();
        var list = new List<ArchiveFormat>((int)count);
        for (uint i = 0; i < count; i++)
        {
            string name = ReadString(i, HandlerPropId.Name) ?? $"#{i}";
            byte[]? clsid = ReadBinary(i, HandlerPropId.ClassId);
            if (clsid is null || clsid.Length < 16)
                continue;

            var exts = Split(ReadString(i, HandlerPropId.Extension));
            var adds = Split(ReadString(i, HandlerPropId.AddExtension));

            var sigs = new List<byte[]>();
            if (ReadBinary(i, HandlerPropId.Signature) is { Length: > 0 } single)
                sigs.Add(single);
            if (ReadBinary(i, HandlerPropId.MultiSignature) is { Length: > 0 } multi)
            {
                // Multi-signature blob: sequence of [len][bytes...].
                for (int pos = 0; pos < multi.Length;)
                {
                    int len = multi[pos++];
                    if (pos + len > multi.Length)
                        break;
                    sigs.Add(multi.AsSpan(pos, len).ToArray());
                    pos += len;
                }
            }

            list.Add(new ArchiveFormat
            {
                Index = i,
                Name = name,
                ClassId = new Guid(clsid.AsSpan(0, 16)),
                Extensions = exts,
                AddExtensions = adds,
                CanUpdate = ReadBool(i, HandlerPropId.Update),
                Flags = (ArchiveFormatFlags)(ReadUInt(i, HandlerPropId.Flags) ?? 0),
                Signatures = sigs,
                SignatureOffset = ReadUInt(i, HandlerPropId.SignatureOffset) ?? 0,
            });
        }
        return list;

        static string[] Split(string? s) => string.IsNullOrWhiteSpace(s)
            ? []
            : s.Split(' ', StringSplitOptions.RemoveEmptyEntries).Select(x => x.ToLowerInvariant()).ToArray();
    }

    private static unsafe string? ReadString(uint index, uint prop)
    {
        var v = SevenZipEngine.GetHandlerProperty(index, prop);
        try { return v.AsString(); } finally { PropVariant.Clear(&v); }
    }

    private static unsafe byte[]? ReadBinary(uint index, uint prop)
    {
        var v = SevenZipEngine.GetHandlerProperty(index, prop);
        try { return v.AsBinary(); } finally { PropVariant.Clear(&v); }
    }

    private static unsafe bool ReadBool(uint index, uint prop)
    {
        var v = SevenZipEngine.GetHandlerProperty(index, prop);
        try { return v.AsBool() ?? false; } finally { PropVariant.Clear(&v); }
    }

    private static unsafe uint? ReadUInt(uint index, uint prop)
    {
        var v = SevenZipEngine.GetHandlerProperty(index, prop);
        try { return v.AsUInt32(); } finally { PropVariant.Clear(&v); }
    }
}
