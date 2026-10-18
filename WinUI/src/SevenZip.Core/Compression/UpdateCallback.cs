using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;
using SevenZip.Core.Archives;
using SevenZip.Core.Interop;
using SevenZip.Core.Operations;

namespace SevenZip.Core.Compression;

/// <summary>One item of the output archive: either copied from the old archive or new data from disk.</summary>
internal sealed class UpdateItem
{
    /// <summary>Index in the existing archive, or -1 for new items.</summary>
    public int ArchiveIndex { get; init; } = -1;
    public bool NewData { get; init; }
    public bool NewProps { get; init; }
    public required string ArchivePath { get; init; }
    public bool IsDirectory { get; init; }
    public ulong Size { get; init; }
    public uint Attributes { get; init; }
    public DateTime? Modified { get; init; }
    public DateTime? Created { get; init; }
    public DateTime? Accessed { get; init; }
    public string? SourcePath { get; init; }
    public Func<Stream>? OpenSource { get; init; }

    public static UpdateItem FromDisk(FileSystemInfo info, string archivePath) => new()
    {
        NewData = true,
        NewProps = true,
        ArchivePath = archivePath,
        IsDirectory = info is DirectoryInfo,
        Size = info is FileInfo fi ? (ulong)fi.Length : 0,
        Attributes = (uint)info.Attributes | (info is DirectoryInfo ? 0x10u : 0u),
        Modified = info.LastWriteTimeUtc,
        Created = info.CreationTimeUtc,
        Accessed = info.LastAccessTimeUtc,
        SourcePath = info.FullName,
    };

    public static UpdateItem Keep(ArchiveEntry e) => new()
    {
        ArchiveIndex = (int)e.Index,
        ArchivePath = e.Path,
        IsDirectory = e.IsDirectory,
        Size = e.Size,
    };

    public static UpdateItem Rename(ArchiveEntry e, string newPath) => new()
    {
        ArchiveIndex = (int)e.Index,
        NewProps = true,
        ArchivePath = newPath,
        IsDirectory = e.IsDirectory,
        Size = e.Size,
        Attributes = e.Attributes ?? (e.IsDirectory ? 0x10u : 0x20u),
        Modified = e.Modified,
        Created = e.Created,
        Accessed = e.Accessed,
    };
}

/// <summary>Sequential file reader that releases its handle at end-of-file (zip reads several files in parallel).</summary>
[GeneratedComClass]
internal sealed unsafe partial class SourceStream : ISequentialInStream, IStreamGetSize, IDisposable
{
    private Stream? _stream;
    private readonly ulong _size;
    private readonly Action<long>? _onRead;

    public SourceStream(Stream stream, ulong size, Action<long>? onRead = null)
    {
        _stream = stream;
        _size = size;
        _onRead = onRead;
    }

    public int Read(byte* data, uint size, uint* processedSize)
    {
        try
        {
            int n = 0;
            var s = _stream;
            if (s is not null && size > 0)
            {
                n = s.Read(new Span<byte>(data, (int)Math.Min(size, int.MaxValue)));
                if (n == 0)
                    Dispose();
            }
            if (processedSize != null)
                *processedSize = (uint)n;
            _onRead?.Invoke(n);
            return HResults.S_OK;
        }
        catch (Exception ex)
        {
            if (processedSize != null)
                *processedSize = 0;
            return HResults.FromException(ex);
        }
    }

    public int GetSize(ulong* size)
    {
        *size = _size;
        return HResults.S_OK;
    }

    public void Dispose()
    {
        Interlocked.Exchange(ref _stream, null)?.Dispose();
    }
}

[GeneratedComClass]
internal sealed unsafe partial class UpdateCallback : IArchiveUpdateCallback, ICryptoGetTextPassword2, ICryptoGetTextPassword
{
    private readonly IReadOnlyList<UpdateItem> _items;
    private readonly OperationContext _ctx;
    private readonly string? _newPassword;
    private readonly ArchiveSession? _source;
    private readonly bool _storeCTime;
    private readonly bool _storeATime;
    private readonly List<SourceStream> _open = [];

    public UpdateCallback(IReadOnlyList<UpdateItem> items, OperationContext ctx, string? newPassword, ArchiveSession? source, bool storeCTime, bool storeATime)
    {
        _items = items;
        _ctx = ctx;
        _newPassword = string.IsNullOrEmpty(newPassword) ? null : newPassword;
        _source = source;
        _storeCTime = storeCTime;
        _storeATime = storeATime;
    }

    public int FilesSkipped { get; private set; }

    public int SetTotal(ulong total)
    {
        _ctx.TotalBytes = total;
        return _ctx.Checkpoint();
    }

    public int SetCompleted(ulong* completeValue)
    {
        if (completeValue != null)
            _ctx.CompletedBytes = *completeValue;
        _ctx.Report();
        return _ctx.Checkpoint();
    }

    public int GetUpdateItemInfo(uint index, int* newData, int* newProps, uint* indexInArchive)
    {
        var item = _items[(int)index];
        if (newData != null) *newData = item.NewData ? 1 : 0;
        if (newProps != null) *newProps = item.NewProps ? 1 : 0;
        if (indexInArchive != null) *indexInArchive = item.ArchiveIndex < 0 ? uint.MaxValue : (uint)item.ArchiveIndex;
        return _ctx.Checkpoint();
    }

    public int GetProperty(uint index, uint propID, PropVariant* value)
    {
        try
        {
            var item = _items[(int)index];
            *value = propID switch
            {
                PropId.Path => PropVariant.FromString(item.ArchivePath),
                PropId.IsDir => PropVariant.FromBool(item.IsDirectory),
                PropId.Size => PropVariant.FromUInt64(item.Size),
                PropId.Attrib => PropVariant.FromUInt32(item.Attributes),
                PropId.MTime => PropVariant.FromDateTime(item.Modified),
                PropId.CTime when _storeCTime => PropVariant.FromDateTime(item.Created),
                PropId.ATime when _storeATime => PropVariant.FromDateTime(item.Accessed),
                PropId.IsAnti => PropVariant.FromBool(false),
                _ => default,
            };
            return HResults.S_OK;
        }
        catch (Exception ex)
        {
            return HResults.FromException(ex);
        }
    }

    public int GetStream(uint index, nint* inStream)
    {
        *inStream = 0;
        var item = _items[(int)index];
        try
        {
            int check = _ctx.Checkpoint();
            if (check != 0)
                return check;
            _ctx.CurrentItem = item.ArchivePath;
            _ctx.Report();
            if (item.IsDirectory)
                return HResults.S_OK;

            Stream stream = item.OpenSource is not null
                ? item.OpenSource()
                : new FileStream(item.SourcePath!, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 16, FileOptions.SequentialScan);
            var wrapper = new SourceStream(stream, item.Size);
            lock (_open)
                _open.Add(wrapper);
            *inStream = ComOut.ToNative<ISequentialInStream>(wrapper);
            return HResults.S_OK;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // S_FALSE tells the handler to skip this file (7-Zip's "cannot open file" warning).
            FilesSkipped++;
            _ctx.AddMessage(MessageSeverity.Warning, $"Cannot open file: {ex.Message}", item.SourcePath ?? item.ArchivePath);
            return HResults.S_FALSE;
        }
        catch (Exception ex)
        {
            return HResults.FromException(ex);
        }
    }

    public int SetOperationResult(int operationResult)
    {
        _ctx.CompletedFiles++;
        _ctx.Report();
        return _ctx.Checkpoint();
    }

    public int CryptoGetTextPassword2(int* passwordIsDefined, nint* password)
    {
        try
        {
            *passwordIsDefined = _newPassword is null ? 0 : 1;
            *password = Marshal.StringToBSTR(_newPassword ?? "");
            return HResults.S_OK;
        }
        catch (Exception ex)
        {
            return HResults.FromException(ex);
        }
    }

    /// <summary>Needed when re-packing encrypted solid blocks of the existing archive.</summary>
    public int CryptoGetTextPassword(nint* password)
    {
        try
        {
            var pwd = _source?.Password ?? _newPassword;
            if (pwd is null && _ctx.Prompts is not null && _source is not null)
                pwd = OperationContext.Wait(_ctx.Prompts.RequestPasswordAsync(_source.DisplayName, retry: false));
            if (pwd is null)
            {
                *password = 0;
                return HResults.E_ABORT;
            }
            if (_source is not null)
                _source.Password = pwd;
            *password = Marshal.StringToBSTR(pwd);
            return HResults.S_OK;
        }
        catch (Exception ex)
        {
            return HResults.FromException(ex);
        }
    }

    public void CloseAll()
    {
        lock (_open)
        {
            foreach (var s in _open)
                s.Dispose();
            _open.Clear();
        }
    }
}
