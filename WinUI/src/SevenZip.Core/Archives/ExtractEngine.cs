using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;
using SevenZip.Core.Interop;
using SevenZip.Core.Operations;

namespace SevenZip.Core.Archives;

public static class ExtractEngine
{
    /// <summary>Extracts the given items (null = everything) and returns a summary.</summary>
    public static ExtractResult Extract(ArchiveSession session, IReadOnlyCollection<uint>? indices, ExtractOptions options, OperationContext ctx)
        => Run(session, indices, options, ctx, test: false);

    /// <summary>Decodes and verifies items without writing anything.</summary>
    public static ExtractResult Test(ArchiveSession session, IReadOnlyCollection<uint>? indices, OperationContext ctx)
        => Run(session, indices, new ExtractOptions { Destination = Path.GetTempPath() }, ctx, test: true);

    /// <summary>Expands folder selections into item indices (folders include all descendants).</summary>
    public static IReadOnlyCollection<uint> CollectIndices(IEnumerable<ArchiveEntry> files, IEnumerable<ArchiveFolder> folders)
    {
        var set = new SortedSet<uint>();
        foreach (var f in files)
            set.Add(f.Index);
        foreach (var folder in folders)
            foreach (var e in folder.EnumerateEntriesRecursive())
                set.Add(e.Index);
        return set;
    }

    private static unsafe ExtractResult Run(ArchiveSession session, IReadOnlyCollection<uint>? indices, ExtractOptions options, OperationContext ctx, bool test)
    {
        if (!test)
            Directory.CreateDirectory(options.Destination);

        var callback = new ExtractCallback(session, options, ctx, test);
        uint[] sorted = indices is null ? [] : indices.Order().ToArray();

        if (indices is null)
        {
            ctx.TotalFiles = (ulong)session.Entries.Count(e => !e.IsDirectory);
        }
        else
        {
            var byIndex = session.Entries;
            ctx.TotalFiles = (ulong)sorted.Count(i => i < byIndex.Count && !byIndex[(int)i].IsDirectory);
        }

        int hr;
        session.Gate.Wait();
        try
        {
            fixed (uint* p = sorted)
                hr = session.Archive.Extract(indices is null ? null : p, indices is null ? uint.MaxValue : (uint)sorted.Length, test ? 1 : 0, callback);
        }
        finally
        {
            callback.CloseCurrent(aborted: true);
            session.Gate.Release();
        }

        if (!test)
            callback.FinishDeferredWork();

        if (hr == HResults.E_ABORT && callback.PasswordCancelled)
            throw new OperationCanceledException();
        Hr.Check(hr, test ? "Testing" : "Extracting", ctx);

        if (callback.WrongPassword)
            session.Password = null;

        ctx.Report(force: true);
        return callback.ToResult();
    }
}

[GeneratedComClass]
internal sealed unsafe partial class ExtractCallback : IArchiveExtractCallback, ICryptoGetTextPassword
{
    private static readonly HashSet<string> s_officeExtensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".doc", ".docx", ".docm", ".dot", ".dotx", ".dotm", ".xls", ".xlsx", ".xlsm", ".xlsb", ".xlt", ".xltx",
        ".xltm", ".xlam", ".ppt", ".pptx", ".pptm", ".pot", ".potx", ".potm", ".ppam", ".pps", ".ppsx", ".ppsm",
        ".sldx", ".sldm", ".rtf", ".pub", ".vsd", ".vsdx", ".vsdm", ".one", ".mdb", ".accdb",
    };

    private readonly ArchiveSession _session;
    private readonly ExtractOptions _options;
    private readonly OperationContext _ctx;
    private readonly bool _test;
    private readonly string _root;
    private readonly string[] _baseComponents;
    private readonly List<(string Path, DateTime? MTime, DateTime? CTime, uint? Attrib)> _directories = [];
    private readonly List<(string Path, string Target)> _symlinks = [];
    private readonly HashSet<string> _topLevel = new(StringComparer.OrdinalIgnoreCase);

    private OverwriteMode _overwrite;
    private FileStream? _file;
    private OutStreamWrapper? _wrapper;
    private string? _currentPath;
    private ArchiveEntry? _currentEntry;
    private int _askMode;
    private int _filesDone, _folders, _skipped, _errors;
    private ulong _bytes;

    public ExtractCallback(ArchiveSession session, ExtractOptions options, OperationContext ctx, bool test)
    {
        _session = session;
        _options = options;
        _ctx = ctx;
        _test = test;
        _overwrite = options.Overwrite;
        _root = Path.GetFullPath(options.Destination);
        _baseComponents = string.IsNullOrEmpty(options.BasePath)
            ? []
            : options.BasePath.Split('\\', StringSplitOptions.RemoveEmptyEntries);
    }

    public bool WrongPassword { get; private set; }
    public bool PasswordCancelled { get; private set; }

    // ---- IProgress ----

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

    // ---- IArchiveExtractCallback ----

    public int GetStream(uint index, nint* outStream, int askExtractMode)
    {
        *outStream = 0;
        try
        {
            int check = _ctx.Checkpoint();
            if (check != 0)
                return check;

            _currentEntry = index < _session.Entries.Count ? _session.Entries[(int)index] : null;
            _currentPath = null;
            _ctx.CurrentItem = _currentEntry?.Path;

            if (askExtractMode != AskMode.Extract || _test || _currentEntry is null)
                return HResults.S_OK;

            var entry = _currentEntry;
            if (entry.IsAltStream && !_options.ExtractAlternateStreams)
            {
                _skipped++;
                return HResults.S_OK;
            }

            var target = ResolveTarget(entry);
            if (target is null)
            {
                _skipped++;
                return HResults.S_OK;
            }

            if (entry.IsDirectory)
            {
                Directory.CreateDirectory(target);
                _directories.Add((target, entry.Modified, entry.Created, entry.Attributes));
                _folders++;
                return HResults.S_OK;
            }

            Directory.CreateDirectory(Path.GetDirectoryName(target)!);

            if (File.Exists(target) || Directory.Exists(target))
            {
                target = ResolveConflict(target, entry);
                if (target is null)
                {
                    _skipped++;
                    return _ctx.Cancellation.IsCancellationRequested ? HResults.E_ABORT : HResults.S_OK;
                }
            }

            var fsOptions = new FileStreamOptions
            {
                Mode = FileMode.Create,
                Access = FileAccess.Write,
                Share = FileShare.Read,
                BufferSize = 1 << 16,
                PreallocationSize = entry.Size > 0 && entry.Size < (1UL << 40) ? (long)entry.Size : 0,
            };
            _file = new FileStream(target, fsOptions);
            _wrapper = new OutStreamWrapper(_file, ownsStream: false);
            _currentPath = target;
            *outStream = ComOut.ToNative<ISequentialOutStream>(_wrapper);
            return HResults.S_OK;
        }
        catch (OperationCanceledException)
        {
            return HResults.E_ABORT;
        }
        catch (Exception ex)
        {
            _errors++;
            _ctx.AddMessage(MessageSeverity.Error, ex.Message, _currentEntry?.Path);
            // Skip this item but keep going, like 7-Zip does for per-file write errors.
            return HResults.S_OK;
        }
    }

    public int PrepareOperation(int askExtractMode)
    {
        _askMode = askExtractMode;
        return HResults.S_OK;
    }

    public int SetOperationResult(int opRes)
    {
        try
        {
            var entry = _currentEntry;
            var result = (ExtractResultCode)opRes;
            CloseCurrent(aborted: false);

            if (result != ExtractResultCode.Ok)
            {
                _errors++;
                bool encrypted = entry?.IsEncrypted == true || _session.HeadersEncrypted;
                if (result == ExtractResultCode.WrongPassword || (encrypted && result is ExtractResultCode.CrcError or ExtractResultCode.DataError))
                    WrongPassword = true;
                _ctx.AddMessage(MessageSeverity.Error, Describe(result, encrypted), entry?.Path);
            }
            else if (entry is not null && !entry.IsDirectory)
            {
                _bytes += entry.Size;
            }

            if (entry is not null && !entry.IsDirectory)
            {
                _filesDone++;
                _ctx.CompletedFiles = (ulong)_filesDone;
            }
            _ctx.Report();
            return _ctx.Checkpoint();
        }
        catch (Exception ex)
        {
            return HResults.FromException(ex);
        }
    }

    // ---- ICryptoGetTextPassword ----

    public int CryptoGetTextPassword(nint* password)
    {
        try
        {
            var pwd = _options.Password ?? _session.Password;
            if (pwd is null && _ctx.Prompts is not null)
            {
                pwd = OperationContext.Wait(_ctx.Prompts.RequestPasswordAsync(_session.DisplayName, retry: false));
                if (pwd is null)
                {
                    PasswordCancelled = true;
                    *password = 0;
                    return HResults.E_ABORT;
                }
            }
            if (pwd is null)
            {
                PasswordCancelled = true;
                *password = 0;
                return HResults.E_ABORT;
            }
            _session.Password = pwd;
            *password = Marshal.StringToBSTR(pwd);
            return HResults.S_OK;
        }
        catch (Exception ex)
        {
            return HResults.FromException(ex);
        }
    }

    // ---- helpers ----

    private string? ResolveTarget(ArchiveEntry entry)
    {
        var components = PathSanitizer.GetSafeComponents(entry.Path, allowAltStream: entry.IsAltStream);

        // Strip the folder being browsed so "Extract" from inside docs\2024 produces 2024's contents.
        int skip = 0;
        if (_baseComponents.Length > 0)
        {
            while (skip < _baseComponents.Length && skip < components.Length
                   && string.Equals(components[skip], PathSanitizer.CleanComponent(_baseComponents[skip]), StringComparison.OrdinalIgnoreCase))
                skip++;
            if (skip < _baseComponents.Length)
                skip = 0;
        }
        var relative = components.AsSpan(skip).ToArray();

        if (_options.PathMode == ExtractPathMode.NoPaths)
        {
            if (entry.IsDirectory)
                return null;
            relative = relative.Length == 0 ? [] : [relative[^1]];
        }
        if (relative.Length == 0)
            return null;

        var full = PathSanitizer.CombineSafe(_root, relative);
        if (full is null)
        {
            _ctx.AddMessage(MessageSeverity.Warning, "Skipped an item whose path points outside the destination folder.", entry.Path);
            return null;
        }
        _topLevel.Add(Path.Combine(_root, relative[0].Split(':')[0]));
        return full;
    }

    private string? ResolveConflict(string target, ArchiveEntry entry)
    {
        var mode = _overwrite;
        if (mode == OverwriteMode.Ask)
        {
            if (_ctx.Prompts is null)
            {
                mode = OverwriteMode.Overwrite;
            }
            else
            {
                var info = new FileInfo(target);
                var choice = OperationContext.Wait(_ctx.Prompts.ResolveOverwriteAsync(new OverwriteRequest(
                    target, info.Exists ? info.Length : 0, info.Exists ? info.LastWriteTimeUtc : null, entry.Size, entry.Modified)));
                switch (choice)
                {
                    case OverwriteChoice.Replace: mode = OverwriteMode.Overwrite; break;
                    case OverwriteChoice.ReplaceAll: mode = _overwrite = OverwriteMode.Overwrite; break;
                    case OverwriteChoice.Skip: mode = OverwriteMode.Skip; break;
                    case OverwriteChoice.SkipAll: mode = _overwrite = OverwriteMode.Skip; break;
                    case OverwriteChoice.KeepBoth: mode = OverwriteMode.RenameNew; break;
                    case OverwriteChoice.KeepBothAll: mode = _overwrite = OverwriteMode.RenameNew; break;
                    default:
                        _ctx.AddMessage(MessageSeverity.Info, "Cancelled.");
                        throw new OperationCanceledException();
                }
            }
        }

        switch (mode)
        {
            case OverwriteMode.Skip:
                return null;
            case OverwriteMode.RenameNew:
                return PathSanitizer.MakeUnique(target);
            case OverwriteMode.RenameExisting:
                File.Move(target, PathSanitizer.MakeUnique(target));
                return target;
            default:
                if (Directory.Exists(target))
                {
                    _ctx.AddMessage(MessageSeverity.Error, "A folder with the same name already exists.", entry.Path);
                    return null;
                }
                var attrs = File.GetAttributes(target);
                if ((attrs & FileAttributes.ReadOnly) != 0)
                    File.SetAttributes(target, attrs & ~FileAttributes.ReadOnly);
                return target;
        }
    }

    internal void CloseCurrent(bool aborted)
    {
        var file = _file;
        var path = _currentPath;
        var entry = _currentEntry;
        _file = null;
        _wrapper = null;
        _currentPath = null;
        if (file is null || path is null)
            return;

        try
        {
            if (!aborted && entry is not null)
            {
                // Engine might have reported a smaller size than preallocated (e.g. on data errors).
                if (file.Position != file.Length)
                    file.SetLength(file.Position);
                if (_options.RestoreTimestamps)
                {
                    if (entry.Modified is { } m) File.SetLastWriteTimeUtc(file.SafeFileHandle, m);
                    if (entry.Created is { } c) File.SetCreationTimeUtc(file.SafeFileHandle, c);
                }
            }
        }
        catch (Exception ex)
        {
            _ctx.AddMessage(MessageSeverity.Warning, ex.Message, entry?.Path);
        }
        finally
        {
            file.Dispose();
        }

        if (aborted || entry is null)
            return;

        try
        {
            WriteZoneIdentifier(path);
            if (_options.RestoreAttributes && entry.Attributes is { } a)
            {
                var keep = (FileAttributes)(a & 0xFFFF) & (FileAttributes.ReadOnly | FileAttributes.Hidden | FileAttributes.System | FileAttributes.Archive);
                if (keep != 0)
                    File.SetAttributes(path, keep);
            }
            if (_options.RestoreSymbolicLinks && !string.IsNullOrEmpty(entry.SymbolicLink))
                _symlinks.Add((path, entry.SymbolicLink));
        }
        catch (Exception ex)
        {
            _ctx.AddMessage(MessageSeverity.Warning, ex.Message, entry.Path);
        }
    }

    private void WriteZoneIdentifier(string path)
    {
        var zone = _session.ZoneIdentifier;
        if (zone is null || _options.ZonePropagation == ZonePropagation.Off || path.IndexOf(':', 2) >= 0)
            return;
        if (_options.ZonePropagation == ZonePropagation.OfficeFilesOnly && !s_officeExtensions.Contains(Path.GetExtension(path)))
            return;
        File.WriteAllText(path + ":Zone.Identifier", zone);
    }

    /// <summary>Runs after all data is written: links last (so they cannot redirect later writes), then folder times.</summary>
    internal void FinishDeferredWork()
    {
        foreach (var (path, target) in _symlinks)
        {
            try
            {
                var linkDir = Path.GetDirectoryName(path)!;
                var resolved = Path.GetFullPath(Path.Combine(linkDir, target.Replace('/', '\\')));
                if (Path.IsPathRooted(target) || !resolved.StartsWith(_root, StringComparison.OrdinalIgnoreCase))
                {
                    _ctx.AddMessage(MessageSeverity.Warning, $"Symbolic link target '{target}' is outside the destination; kept as a regular file.", path);
                    continue;
                }
                File.Delete(path);
                File.CreateSymbolicLink(path, target.Replace('/', '\\'));
            }
            catch (Exception ex)
            {
                _ctx.AddMessage(MessageSeverity.Warning, $"Could not create symbolic link: {ex.Message}", path);
            }
        }

        if (!_options.RestoreTimestamps)
            return;
        foreach (var (path, mtime, ctime, attrib) in _directories.OrderByDescending(d => d.Path.Length))
        {
            try
            {
                if (mtime is { } m) Directory.SetLastWriteTimeUtc(path, m);
                if (ctime is { } c) Directory.SetCreationTimeUtc(path, c);
                if (_options.RestoreAttributes && attrib is { } a)
                {
                    var keep = (FileAttributes)(a & 0xFFFF) & (FileAttributes.Hidden | FileAttributes.System);
                    if (keep != 0)
                        File.SetAttributes(path, FileAttributes.Directory | keep);
                }
            }
            catch
            {
                // Folder metadata is best-effort.
            }
        }
    }

    private static string Describe(ExtractResultCode code, bool encrypted) => code switch
    {
        ExtractResultCode.UnsupportedMethod => "Unsupported compression method.",
        ExtractResultCode.DataError => encrypted ? "Data error in encrypted file. Wrong password?" : "Data error.",
        ExtractResultCode.CrcError => encrypted ? "CRC failed in encrypted file. Wrong password?" : "CRC failed.",
        ExtractResultCode.Unavailable => "Unavailable data.",
        ExtractResultCode.UnexpectedEnd => "Unexpected end of data.",
        ExtractResultCode.DataAfterEnd => "There is some data after the end of the payload data.",
        ExtractResultCode.IsNotArc => "Is not an archive.",
        ExtractResultCode.HeadersError => "Headers error.",
        ExtractResultCode.WrongPassword => "Wrong password.",
        _ => $"Error {(int)code}.",
    };

    public ExtractResult ToResult() => new()
    {
        Destination = _root,
        FilesProcessed = _filesDone,
        Folders = _folders,
        Skipped = _skipped,
        Errors = _errors,
        Bytes = _bytes,
        WrongPassword = WrongPassword,
        TopLevelPaths = _topLevel.ToArray(),
    };
}
