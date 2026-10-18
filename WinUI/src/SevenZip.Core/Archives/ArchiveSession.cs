using System.Runtime.InteropServices;
using SevenZip.Core.Formats;
using SevenZip.Core.Interop;
using SevenZip.Core.Operations;

namespace SevenZip.Core.Archives;

public sealed class ArchiveOpenOptions
{
    /// <summary>Force a handler ("Open as…"). Null = detect.</summary>
    public ArchiveFormat? Format { get; init; }
    public string? Password { get; init; }
    /// <summary>Display name when the file is a temporary copy (nested archives).</summary>
    public string? DisplayName { get; init; }
    public ArchiveSession? Parent { get; init; }
    public string? PathInParent { get; init; }
}

/// <summary>
/// An open archive. All engine calls on one handler instance are serialised through <see cref="Gate"/>
/// because handlers are not thread-safe.
/// </summary>
public sealed unsafe class ArchiveSession : IDisposable
{
    private const ulong MaxStartSearch = 1 << 23;

    private readonly List<IDisposable> _resources = [];
    private readonly List<IInArchive> _outerArchives = [];
    private bool _disposed;

    private ArchiveSession(string path, ArchiveFormat format, IInArchive archive)
    {
        FilePath = path;
        Format = format;
        Archive = archive;
    }

    public string FilePath { get; }
    public string DisplayName { get; private init; } = "";
    public ArchiveFormat Format { get; }
    internal IInArchive Archive { get; }
    internal SemaphoreSlim Gate { get; } = new(1, 1);

    public IReadOnlyList<ArchiveEntry> Entries { get; private set; } = [];
    public ArchiveFolder Root { get; private set; } = null!;
    public ArchiveInfo Info { get; private set; } = null!;
    public IReadOnlyList<string> VolumePaths { get; private set; } = [];
    public string? Password { get; internal set; }
    public bool HeadersEncrypted { get; private set; }
    public bool HasEncryptedItems { get; private set; }

    /// <summary>The archive this one was opened from (nested archives), or null.</summary>
    public ArchiveSession? Parent { get; private init; }
    public string? PathInParent { get; private init; }
    /// <summary>Format of an outer container ("Split") when the archive was opened through one.</summary>
    public string? ContainerFormat { get; private set; }

    public bool CanUpdate => Format.CanUpdate && Parent is null && ContainerFormat is null && VolumePaths.Count <= 1
                             && !Info.HasErrors;

    /// <summary>Zone.Identifier (Mark of the Web) of the archive file, propagated to extracted files.</summary>
    public string? ZoneIdentifier { get; private set; }

    public static ArchiveSession Open(string path, ArchiveOpenOptions? options = null, OperationContext? ctx = null)
    {
        options ??= new ArchiveOpenOptions();
        ctx ??= new OperationContext();
        path = Path.GetFullPath(path);

        var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 16, FileOptions.RandomAccess);
        var header = new byte[1 << 16];
        int headerLength = fs.ReadAtLeast(header, header.Length, throwOnEndOfStream: false);
        fs.Position = 0;

        var stream = new InStreamWrapper(fs);
        var callback = new OpenCallback(path, ctx, options.Password);
        callback.VolumePaths.Add(path);

        string displayName = options.DisplayName ?? Path.GetFileName(path);
        Exception? lastError = null;

        foreach (var format in GetCandidates(displayName, header.AsSpan(0, headerLength), options.Format))
        {
            ctx.Cancellation.ThrowIfCancellationRequested();
            IInArchive archive;
            try { archive = SevenZipEngine.CreateObject<IInArchive>(format.ClassId); }
            catch (Exception ex) { lastError = ex; continue; }

            fs.Position = 0;
            ulong maxCheck = (format.Flags & ArchiveFormatFlags.FindSignature) != 0 || options.Format is not null ? MaxStartSearch : 0;
            int hr = archive.Open(stream, &maxCheck, callback);

            if (hr == HResults.E_ABORT || callback.PasswordCancelled)
            {
                archive.Close();
                stream.Dispose();
                throw new OperationCanceledException();
            }

            if (hr == HResults.S_OK && !IsNotArchive(archive))
            {
                var session = new ArchiveSession(path, format, archive)
                {
                    DisplayName = displayName,
                    Parent = options.Parent,
                    PathInParent = options.PathInParent,
                };
                session._resources.Add(stream);
                session._resources.AddRange(callback.VolumeStreams);
                session.VolumePaths = callback.VolumePaths.ToArray();
                session.Password = callback.Password;
                session.HeadersEncrypted = callback.PasswordRequested;
                session.ZoneIdentifier = ReadZoneIdentifier(path) ?? options.Parent?.ZoneIdentifier;

                try
                {
                    session = session.UnwrapContainer(ctx);
                    session.Load();
                }
                catch
                {
                    session.Dispose();
                    throw;
                }
                return session;
            }

            archive.Close();
            if (callback.PasswordRequested)
            {
                stream.Dispose();
                foreach (var v in callback.VolumeStreams)
                    v.Dispose();
                throw new WrongPasswordException(displayName);
            }
            if (hr != HResults.S_OK && hr != HResults.S_FALSE)
                lastError = SevenZipException.FromHResult(hr, format.Name);
        }

        stream.Dispose();
        foreach (var v in callback.VolumeStreams)
            v.Dispose();
        throw lastError as SevenZipException
              ?? new SevenZipException($"Cannot open '{displayName}' as an archive.", HResults.S_FALSE);
    }

    /// <summary>Candidate handlers, most likely first (extension, signature, signature search, executables last).</summary>
    internal static IEnumerable<ArchiveFormat> GetCandidates(string fileName, ReadOnlySpan<byte> header, ArchiveFormat? forced)
    {
        if (forced is not null)
            return [forced];

        var all = FormatRegistry.All;
        var ext = Path.GetExtension(fileName);
        var result = new List<ArchiveFormat>();
        var preArc = new List<ArchiveFormat>();

        void Add(ArchiveFormat f)
        {
            if (result.Contains(f) || preArc.Contains(f))
                return;
            if ((f.Flags & ArchiveFormatFlags.PreArc) != 0)
                preArc.Add(f);
            else
                result.Add(f);
        }

        bool numbered = ext.Length == 4 && ext.Skip(1).All(char.IsAsciiDigit);
        if (numbered && FormatRegistry.Find("Split") is { } split)
            result.Add(split);

        foreach (var f in all)
            if (ext.Length > 1 && f.MatchesExtension(ext))
                Add(f);

        foreach (var f in all)
        {
            if ((f.Flags & ArchiveFormatFlags.ByExtOnlyOpen) != 0)
                continue;
            if (f.MatchesSignature(header) || SevenZipEngine.ProbeIsArc(f.Index, header) == 1)
                Add(f);
        }

        foreach (var f in all)
            if ((f.Flags & ArchiveFormatFlags.FindSignature) != 0 && (f.Flags & ArchiveFormatFlags.ByExtOnlyOpen) == 0)
                Add(f);

        result.AddRange(preArc);
        return result;
    }

    private static bool IsNotArchive(IInArchive archive)
    {
        PropVariant v = default;
        if (archive.GetArchiveProperty(PropId.ErrorFlags, &v) != 0)
            return false;
        uint flags = v.AsUInt32() ?? 0;
        PropVariant.Clear(&v);
        return (flags & 1) != 0;
    }

    /// <summary>Split volumes (.001, .002…) present one combined stream; open the real archive inside it.</summary>
    private ArchiveSession UnwrapContainer(OperationContext ctx)
    {
        if (!string.Equals(Format.Name, "Split", StringComparison.OrdinalIgnoreCase))
            return this;
        if (Archive is not IInArchiveGetStream getStream)
            return this;

        PropVariant name = default;
        Archive.GetProperty(0, PropId.Path, &name);
        string innerName = name.AsString() ?? Path.GetFileNameWithoutExtension(FilePath);
        PropVariant.Clear(&name);

        if (getStream.GetStream(0, out var seq) != 0 || seq is not IInStream inner)
            return this;

        Span<byte> header = new byte[1 << 16];
        int headerLength = ReadHeader(inner, header);

        var callback = new OpenCallback(FilePath, ctx, Password) { DisableVolumes = true };
        foreach (var format in GetCandidates(innerName, header[..headerLength], null))
        {
            if (string.Equals(format.Name, "Split", StringComparison.OrdinalIgnoreCase))
                continue;
            var archive = SevenZipEngine.CreateObject<IInArchive>(format.ClassId);
            inner.Seek(0, 0, null);
            ulong maxCheck = (format.Flags & ArchiveFormatFlags.FindSignature) != 0 ? MaxStartSearch : 0;
            int hr = archive.Open(inner, &maxCheck, callback);
            if (hr == HResults.E_ABORT || callback.PasswordCancelled)
                throw new OperationCanceledException();
            if (hr == HResults.S_OK && !IsNotArchive(archive))
            {
                var session = new ArchiveSession(FilePath, format, archive)
                {
                    DisplayName = DisplayName,
                    Parent = Parent,
                    PathInParent = PathInParent,
                };
                session.ContainerFormat = Format.Name;
                session._outerArchives.Add(Archive);
                session._resources.AddRange(_resources);
                _resources.Clear();
                session.VolumePaths = VolumePaths;
                session.Password = callback.Password;
                session.HeadersEncrypted = callback.PasswordRequested;
                session.ZoneIdentifier = ZoneIdentifier;
                return session;
            }
            archive.Close();
            if (callback.PasswordRequested)
                throw new WrongPasswordException(DisplayName);
        }
        return this;

        static int ReadHeader(IInStream s, Span<byte> buffer)
        {
            int total = 0;
            fixed (byte* p = buffer)
            {
                while (total < buffer.Length)
                {
                    uint read;
                    if (s.Read(p + total, (uint)(buffer.Length - total), &read) != 0 || read == 0)
                        break;
                    total += (int)read;
                }
            }
            return total;
        }
    }

    private void Load()
    {
        uint count;
        Hr.Check(Archive.GetNumberOfItems(&count), "Reading archive");

        var entries = new List<ArchiveEntry>((int)count);
        bool anyEncrypted = false;
        for (uint i = 0; i < count; i++)
        {
            string? path = GetString(i, PropId.Path);
            if (string.IsNullOrEmpty(path))
                path = count == 1 ? Format.GetDefaultItemName(DisplayName) : $"[{i}]";
            path = NormalizePath(path);

            bool encrypted = GetBool(i, PropId.Encrypted) ?? false;
            anyEncrypted |= encrypted;
            entries.Add(new ArchiveEntry
            {
                Index = i,
                Path = path,
                IsDirectory = GetBool(i, PropId.IsDir) ?? false,
                Size = GetUInt64(i, PropId.Size) ?? 0,
                PackedSize = GetUInt64(i, PropId.PackSize),
                Modified = GetTime(i, PropId.MTime),
                Created = GetTime(i, PropId.CTime),
                Accessed = GetTime(i, PropId.ATime),
                Attributes = (uint?)GetUInt64(i, PropId.Attrib),
                Crc = (uint?)GetUInt64(i, PropId.Crc),
                Method = GetString(i, PropId.Method),
                IsEncrypted = encrypted,
                IsAltStream = GetBool(i, PropId.IsAltStream) ?? false,
                Comment = GetString(i, PropId.Comment),
                SymbolicLink = GetString(i, PropId.SymLink),
            });
        }
        Entries = entries;
        HasEncryptedItems = anyEncrypted || HeadersEncrypted;
        Root = ArchiveFolder.Build(entries.Where(e => !e.IsAltStream).ToList());
        Info = ReadInfo();
    }

    internal static string NormalizePath(string path)
    {
        path = path.Replace('/', '\\');
        while (path.StartsWith('\\'))
            path = path[1..];
        return path.TrimEnd('\\');
    }

    private ArchiveInfo ReadInfo()
    {
        var all = new List<KeyValuePair<string, string>>
        {
            new("Path", FilePath),
            new("Type", ContainerFormat is null ? Format.Name : $"{Format.Name} (inside {ContainerFormat})"),
        };
        if (VolumePaths.Count > 1)
            all.Add(new("Volumes", VolumePaths.Count.ToString()));

        uint n;
        if (Archive.GetNumberOfArchiveProperties(&n) == 0)
        {
            for (uint i = 0; i < n; i++)
            {
                nint bstr = 0;
                uint propId;
                ushort vt;
                if (Archive.GetArchivePropertyInfo(i, &bstr, &propId, &vt) != 0)
                    continue;
                string? engineName = bstr != 0 ? Marshal.PtrToStringBSTR(bstr) : null;
                if (bstr != 0)
                    NativeMethods.SysFreeString(bstr);
                PropVariant v = default;
                if (Archive.GetArchiveProperty(propId, &v) == 0 && !v.IsEmpty)
                    all.Add(new(PropId.GetDisplayName(propId, engineName), PropertyFormatter.FormatValue(propId, v)));
                PropVariant.Clear(&v);
            }
        }

        return new ArchiveInfo
        {
            FormatName = Format.Name,
            PhysicalSize = GetArcUInt64(PropId.PhySize) ?? (ulong)new FileInfo(FilePath).Length,
            HeadersSize = GetArcUInt64(PropId.HeadersSize),
            Method = GetArcString(PropId.Method),
            IsSolid = GetArcBool(PropId.Solid),
            Blocks = (uint?)GetArcUInt64(PropId.NumBlocks),
            Volumes = (uint?)GetArcUInt64(PropId.NumVolumes),
            Offset = (long?)GetArcUInt64(PropId.Offset),
            Comment = GetArcString(PropId.Comment),
            HeadersEncrypted = HeadersEncrypted,
            ErrorFlags = (uint)(GetArcUInt64(PropId.ErrorFlags) ?? 0),
            WarningFlags = (uint)(GetArcUInt64(PropId.WarningFlags) ?? 0),
            Error = GetArcString(PropId.Error),
            Warning = GetArcString(PropId.Warning),
            AllProperties = all,
        };
    }

    /// <summary>Every property the handler reports for one item (for the details pane).</summary>
    public IReadOnlyList<KeyValuePair<string, string>> GetItemProperties(uint index)
    {
        var list = new List<KeyValuePair<string, string>>();
        Gate.Wait();
        try
        {
            uint n;
            if (Archive.GetNumberOfProperties(&n) != 0)
                return list;
            for (uint i = 0; i < n; i++)
            {
                nint bstr = 0;
                uint propId;
                ushort vt;
                if (Archive.GetPropertyInfo(i, &bstr, &propId, &vt) != 0)
                    continue;
                string? engineName = bstr != 0 ? Marshal.PtrToStringBSTR(bstr) : null;
                if (bstr != 0)
                    NativeMethods.SysFreeString(bstr);
                PropVariant v = default;
                if (Archive.GetProperty(index, propId, &v) == 0 && !v.IsEmpty)
                    list.Add(new(PropId.GetDisplayName(propId, engineName), PropertyFormatter.FormatValue(propId, v)));
                PropVariant.Clear(&v);
            }
        }
        finally
        {
            Gate.Release();
        }
        return list;
    }

    private static string? ReadZoneIdentifier(string path)
    {
        try
        {
            var ads = path + ":Zone.Identifier";
            return File.Exists(ads) ? File.ReadAllText(ads) : null;
        }
        catch
        {
            return null;
        }
    }

    // ---- property helpers ----

    internal string? GetString(uint index, uint propId)
    {
        PropVariant v = default;
        if (Archive.GetProperty(index, propId, &v) != 0)
            return null;
        try { return v.AsString(); } finally { PropVariant.Clear(&v); }
    }

    internal bool? GetBool(uint index, uint propId)
    {
        PropVariant v = default;
        if (Archive.GetProperty(index, propId, &v) != 0)
            return null;
        return v.AsBool();
    }

    internal ulong? GetUInt64(uint index, uint propId)
    {
        PropVariant v = default;
        if (Archive.GetProperty(index, propId, &v) != 0)
            return null;
        return v.AsUInt64();
    }

    internal DateTime? GetTime(uint index, uint propId)
    {
        PropVariant v = default;
        if (Archive.GetProperty(index, propId, &v) != 0)
            return null;
        return v.AsDateTime();
    }

    private ulong? GetArcUInt64(uint propId)
    {
        PropVariant v = default;
        if (Archive.GetArchiveProperty(propId, &v) != 0)
            return null;
        return v.AsUInt64();
    }

    private bool? GetArcBool(uint propId)
    {
        PropVariant v = default;
        if (Archive.GetArchiveProperty(propId, &v) != 0)
            return null;
        return v.AsBool();
    }

    private string? GetArcString(uint propId)
    {
        PropVariant v = default;
        if (Archive.GetArchiveProperty(propId, &v) != 0)
            return null;
        try { return v.AsString(); } finally { PropVariant.Clear(&v); }
    }

    public void Dispose()
    {
        if (_disposed)
            return;
        _disposed = true;
        Gate.Wait();
        try
        {
            Archive.Close();
            foreach (var outer in _outerArchives)
                outer.Close();
            foreach (var r in _resources)
                r.Dispose();
        }
        finally
        {
            Gate.Release();
        }
    }
}
