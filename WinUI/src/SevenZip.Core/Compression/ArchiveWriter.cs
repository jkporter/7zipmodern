using System.Runtime.InteropServices;
using SevenZip.Core.Archives;
using SevenZip.Core.Formats;
using SevenZip.Core.Interop;
using SevenZip.Core.Operations;

namespace SevenZip.Core.Compression;

public sealed class CompressResult
{
    public required IReadOnlyList<string> OutputFiles { get; init; }
    public ulong InputBytes { get; init; }
    public ulong OutputBytes { get; init; }
    public int Files { get; init; }
    public int Skipped { get; init; }
    public double Ratio => InputBytes == 0 ? 1 : (double)OutputBytes / InputBytes;
}

/// <summary>Changes to apply to an existing archive in one rewrite.</summary>
public sealed class UpdatePlan
{
    /// <summary>Files/folders on disk to add.</summary>
    public IReadOnlyList<string> Add { get; init; } = [];
    /// <summary>Folder inside the archive that receives <see cref="Add"/> ('' = root).</summary>
    public string TargetFolder { get; init; } = "";
    /// <summary>Items to remove; folders must already be expanded to their descendants.</summary>
    public IReadOnlyCollection<uint> Delete { get; init; } = [];
    /// <summary>Old archive path prefix → new prefix (renames a file or a whole folder).</summary>
    public IReadOnlyDictionary<string, string> Rename { get; init; } = new Dictionary<string, string>();
    /// <summary>Item index → file on disk whose content replaces it (edited temp files).</summary>
    public IReadOnlyDictionary<uint, string> Replace { get; init; } = new Dictionary<uint, string>();
    /// <summary>Empty folders to create.</summary>
    public IReadOnlyList<string> NewFolders { get; init; } = [];
}

public static class ArchiveWriter
{
    /// <summary>Creates a new archive (replacing any existing file only after success).</summary>
    public static CompressResult Create(string archivePath, IReadOnlyList<string> sources, CompressionSettings settings, OperationContext ctx)
    {
        archivePath = Path.GetFullPath(archivePath);
        var items = CollectSources(sources, "", settings, ctx, exclude: archivePath);
        return CreateFromItems(archivePath, items, settings, ctx);
    }

    internal static CompressResult CreateFromItems(string archivePath, List<UpdateItem> items, CompressionSettings settings, OperationContext ctx)
    {
        if (settings.IsTarball)
            return CreateTarball(archivePath, items, settings, ctx);

        var caps = FormatCapabilities.Get(settings.Format);
        if (caps.SingleFile)
        {
            var files = items.Where(i => !i.IsDirectory).ToList();
            if (files.Count != 1)
                throw new SevenZipException($"{caps.DisplayName} can only hold one file. Choose a TAR-based format for folders or several files.");
            items = [new UpdateItem
            {
                NewData = true, NewProps = true, ArchivePath = Path.GetFileName(files[0].ArchivePath), Size = files[0].Size,
                Attributes = files[0].Attributes, Modified = files[0].Modified, SourcePath = files[0].SourcePath, OpenSource = files[0].OpenSource,
            }];
        }

        ctx.TotalFiles = (ulong)items.Count(i => !i.IsDirectory);
        var format = FormatRegistry.Get(EngineFormatName(settings.Format));
        var outArchive = SevenZipEngine.CreateObject<IOutArchive>(format.ClassId);
        ApplyProperties(outArchive, BuildProperties(settings, format.Name, null));

        return WriteArchive(archivePath, settings.VolumeSize, outArchive, items, settings, ctx, source: null);
    }

    /// <summary>Applies <paramref name="plan"/> to an open archive, then disposes the session and swaps files atomically.</summary>
    public static CompressResult Update(ArchiveSession session, UpdatePlan plan, CompressionSettings? settings, OperationContext ctx)
    {
        if (!session.CanUpdate)
            throw new SevenZipException($"{session.Format.Name} archives can't be modified here.");

        // Keep the protection the archive already had.
        settings ??= new CompressionSettings { Format = session.Format.Name, Level = CompressionLevel.Normal };
        settings = settings with { Format = session.Format.Name };
        if (session.HasEncryptedItems && settings.Password is null)
            settings = settings with { Password = session.Password, EncryptFileNames = session.HeadersEncrypted };

        var deleted = new HashSet<uint>(plan.Delete);
        var added = CollectSources(plan.Add, plan.TargetFolder, settings, ctx, exclude: session.FilePath);
        foreach (var folder in plan.NewFolders)
        {
            added.Add(new UpdateItem
            {
                NewData = true, NewProps = true, ArchivePath = ArchiveSession.NormalizePath(folder), IsDirectory = true,
                Attributes = 0x10, Modified = DateTime.UtcNow, Created = DateTime.UtcNow,
            });
        }
        var addedByPath = new Dictionary<string, UpdateItem>(StringComparer.OrdinalIgnoreCase);
        foreach (var a in added)
            addedByPath[a.ArchivePath] = a;

        var items = new List<UpdateItem>(session.Entries.Count + added.Count);
        foreach (var entry in session.Entries)
        {
            if (deleted.Contains(entry.Index))
                continue;

            if (plan.Replace.TryGetValue(entry.Index, out var replacement))
            {
                var fi = new FileInfo(replacement);
                items.Add(UpdateItem.FromDisk(fi, entry.Path));
                continue;
            }

            var renamed = ApplyRename(entry.Path, plan.Rename);

            if (addedByPath.TryGetValue(renamed, out var incoming))
            {
                bool replace = settings.UpdateMode switch
                {
                    UpdateMode.UpdateAndAdd or UpdateMode.Freshen => incoming.Modified > entry.Modified,
                    _ => true,
                };
                if (incoming.IsDirectory && entry.IsDirectory)
                    replace = true;
                if (replace)
                    continue;
                addedByPath.Remove(renamed);
            }
            else if (settings.UpdateMode == UpdateMode.Synchronize && plan.Add.Count > 0 && IsUnderTarget(entry.Path, plan.TargetFolder))
            {
                continue;
            }

            items.Add(renamed == entry.Path ? UpdateItem.Keep(entry) : UpdateItem.Rename(entry, renamed));
        }

        if (settings.UpdateMode == UpdateMode.Freshen)
        {
            var existing = new HashSet<string>(session.Entries.Select(e => e.Path), StringComparer.OrdinalIgnoreCase);
            items.AddRange(addedByPath.Values.Where(a => existing.Contains(a.ArchivePath)));
        }
        else
        {
            items.AddRange(addedByPath.Values);
        }

        ctx.TotalFiles = (ulong)items.Count(i => i.NewData && !i.IsDirectory);

        if (session.Archive is not IOutArchive outArchive)
            throw new SevenZipException($"{session.Format.Name} handler can't write archives.");

        var props = BuildProperties(settings, session.Format.Name, session);
        string finalPath = session.FilePath;
        string tempPath = Path.Combine(Path.GetDirectoryName(finalPath)!, $".{Path.GetFileName(finalPath)}.{Environment.ProcessId:x}.tmp");

        CompressResult result;
        session.Gate.Wait();
        try
        {
            ApplyProperties(outArchive, props);
            result = WriteArchive(tempPath, null, outArchive, items, settings, ctx, session, atomicRename: false);
        }
        catch
        {
            TryDelete(tempPath);
            throw;
        }
        finally
        {
            session.Gate.Release();
        }

        // Release the original before swapping it out.
        session.Dispose();
        File.Move(tempPath, finalPath, overwrite: true);
        return new CompressResult
        {
            OutputFiles = [finalPath],
            InputBytes = result.InputBytes,
            OutputBytes = result.OutputBytes,
            Files = result.Files,
            Skipped = result.Skipped,
        };
    }

    private static bool IsUnderTarget(string path, string target)
        => target.Length == 0 || path.StartsWith(target.TrimEnd('\\') + "\\", StringComparison.OrdinalIgnoreCase);

    private static string ApplyRename(string path, IReadOnlyDictionary<string, string> renames)
    {
        foreach (var (from, to) in renames)
        {
            if (string.Equals(path, from, StringComparison.Ordinal))
                return ArchiveSession.NormalizePath(to);
            if (path.StartsWith(from + "\\", StringComparison.Ordinal))
                return ArchiveSession.NormalizePath(to) + path[from.Length..];
        }
        return path;
    }

    private static CompressResult WriteArchive(string path, ulong? volumeSize, IOutArchive outArchive, List<UpdateItem> items,
        CompressionSettings settings, OperationContext ctx, ArchiveSession? source, bool atomicRename = true)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var callback = new UpdateCallback(items, ctx, settings.Password, source, settings.StoreCreationTime, settings.StoreAccessTime);

        Stream target;
        string? tempPath = null;
        MultiVolumeStream? volumes = null;
        if (volumeSize is { } vs && vs > 0)
        {
            target = volumes = new MultiVolumeStream(path, vs);
        }
        else
        {
            tempPath = atomicRename ? Path.Combine(Path.GetDirectoryName(path)!, $".{Path.GetFileName(path)}.{Environment.ProcessId:x}.tmp") : path;
            target = new FileStream(tempPath, FileMode.Create, FileAccess.ReadWrite, FileShare.None, 1 << 16);
        }

        ulong written = 0;
        var outStream = new OutStreamWrapper(target, ownsStream: false, onWrite: n => { written += (ulong)n; ctx.OutputBytes = written; });
        int hr = HResults.E_FAIL;
        try
        {
            hr = outArchive.UpdateItems(outStream, (uint)items.Count, callback);
        }
        finally
        {
            callback.CloseAll();
            ulong finalLength = (ulong)target.Length;
            target.Dispose();
            if (hr != HResults.S_OK)
            {
                volumes?.DeleteAll();
                if (tempPath is not null)
                    TryDelete(tempPath);
            }
            written = finalLength;
        }
        Hr.Check(hr, "Compressing", ctx);

        IReadOnlyList<string> outputs;
        if (volumes is not null)
        {
            outputs = volumes.VolumePaths;
        }
        else
        {
            if (atomicRename)
                File.Move(tempPath!, path, overwrite: true);
            outputs = [path];
        }

        ctx.Report(force: true);
        return new CompressResult
        {
            OutputFiles = outputs,
            InputBytes = (ulong)items.Where(i => i.NewData).Sum(i => (decimal)i.Size),
            OutputBytes = written,
            Files = items.Count(i => !i.IsDirectory),
            Skipped = callback.FilesSkipped,
        };
    }

    /// <summary>tar.gz / tar.xz / tar.bz2: write the tar next to the target, then compress it as a single stream.</summary>
    private static CompressResult CreateTarball(string archivePath, List<UpdateItem> items, CompressionSettings settings, OperationContext ctx)
    {
        var dir = Path.GetDirectoryName(archivePath)!;
        var tarName = Path.GetFileNameWithoutExtension(archivePath);
        if (!tarName.EndsWith(".tar", StringComparison.OrdinalIgnoreCase))
            tarName += ".tar";
        var tempTar = Path.Combine(dir, $".{tarName}.{Environment.ProcessId:x}.tmp");

        try
        {
            var tarSettings = settings with { Format = "tar", VolumeSize = null, Method = null, DictionarySize = null, WordSize = null };
            var tarFormat = FormatRegistry.Get("tar");
            var tar = SevenZipEngine.CreateObject<IOutArchive>(tarFormat.ClassId);
            ctx.TotalFiles = (ulong)items.Count(i => !i.IsDirectory);
            var tarResult = WriteArchive(tempTar, null, tar, items, tarSettings, ctx, null, atomicRename: false);

            var streamFormat = settings.Format.ToLowerInvariant() switch
            {
                "tar.gz" => "gzip",
                "tar.xz" => "xz",
                "tar.bz2" => "bzip2",
                _ => throw new NotSupportedException(settings.Format),
            };
            var tarInfo = new FileInfo(tempTar);
            var single = new List<UpdateItem>
            {
                new()
                {
                    NewData = true, NewProps = true, ArchivePath = tarName, Size = (ulong)tarInfo.Length,
                    Attributes = 0x20, Modified = DateTime.UtcNow, SourcePath = tempTar,
                },
            };
            ctx.CompletedBytes = 0;
            ctx.OutputBytes = 0;
            var final = CreateFromItems(archivePath, single, settings with { Format = streamFormat }, ctx);
            return new CompressResult
            {
                OutputFiles = final.OutputFiles,
                InputBytes = tarResult.InputBytes,
                OutputBytes = final.OutputBytes,
                Files = tarResult.Files,
                Skipped = tarResult.Skipped,
            };
        }
        finally
        {
            TryDelete(tempTar);
        }
    }

    // ---- source enumeration ----

    internal static List<UpdateItem> CollectSources(IEnumerable<string> sources, string archivePrefix, CompressionSettings settings, OperationContext ctx, string? exclude)
    {
        var byPath = new Dictionary<string, UpdateItem>(StringComparer.OrdinalIgnoreCase);
        var order = new List<string>();
        archivePrefix = ArchiveSession.NormalizePath(archivePrefix);

        void Add(UpdateItem item)
        {
            if (!byPath.ContainsKey(item.ArchivePath))
                order.Add(item.ArchivePath);
            byPath[item.ArchivePath] = item;
        }

        string Join(string relative) => archivePrefix.Length == 0 ? relative : archivePrefix + "\\" + relative;

        foreach (var raw in sources)
        {
            ctx.Cancellation.ThrowIfCancellationRequested();
            var full = Path.GetFullPath(raw);
            var root = Path.GetPathRoot(full) ?? "";
            if (full.Length > root.Length)
                full = full.TrimEnd('\\');

            string name = settings.PathMode == CompressPathMode.Full
                ? full[root.Length..]
                : Path.GetFileName(full) is { Length: > 0 } n ? n : root.TrimEnd('\\', ':');

            if (File.Exists(full))
            {
                if (!IsExcluded(full, exclude))
                    Add(UpdateItem.FromDisk(new FileInfo(full), Join(name)));
                continue;
            }
            if (!Directory.Exists(full))
            {
                ctx.AddMessage(MessageSeverity.Warning, "The item no longer exists.", full);
                continue;
            }

            var stack = new Stack<(DirectoryInfo Dir, string ArchivePath)>();
            var rootDir = new DirectoryInfo(full);
            if (name.Length > 0)
            {
                // Full-path mode adds parent folders so the structure is explicit.
                if (settings.PathMode == CompressPathMode.Full)
                {
                    var parts = name.Split('\\');
                    for (int i = 1; i < parts.Length; i++)
                    {
                        var p = Join(string.Join('\\', parts[..i]));
                        var info = new DirectoryInfo(Path.Combine(root, string.Join('\\', parts[..i])));
                        if (!byPath.ContainsKey(p))
                            Add(UpdateItem.FromDisk(info, p));
                    }
                }
                Add(UpdateItem.FromDisk(rootDir, Join(name)));
            }
            stack.Push((rootDir, name.Length > 0 ? Join(name) : archivePrefix));

            var options = new EnumerationOptions
            {
                RecurseSubdirectories = false,
                AttributesToSkip = 0,
                IgnoreInaccessible = false,
                ReturnSpecialDirectories = false,
            };

            while (stack.Count > 0)
            {
                ctx.Cancellation.ThrowIfCancellationRequested();
                var (dir, prefix) = stack.Pop();
                IEnumerable<FileSystemInfo> children;
                try
                {
                    children = dir.EnumerateFileSystemInfos("*", options).ToList();
                }
                catch (Exception ex) when (ex is UnauthorizedAccessException or IOException)
                {
                    ctx.AddMessage(MessageSeverity.Warning, ex.Message, dir.FullName);
                    continue;
                }

                foreach (var child in children)
                {
                    var childPath = prefix.Length == 0 ? child.Name : prefix + "\\" + child.Name;
                    if (child is DirectoryInfo sub)
                    {
                        Add(UpdateItem.FromDisk(sub, childPath));
                        bool isLink = (sub.Attributes & FileAttributes.ReparsePoint) != 0 && sub.LinkTarget is not null;
                        if (!isLink || settings.FollowLinks)
                            stack.Push((sub, childPath));
                    }
                    else if (!IsExcluded(child.FullName, exclude))
                    {
                        Add(UpdateItem.FromDisk(child, childPath));
                    }
                }
            }
        }

        var list = new List<UpdateItem>(order.Count);
        foreach (var p in order)
            list.Add(byPath[p]);
        return list;

        static bool IsExcluded(string path, string? exclude)
            => exclude is not null && (string.Equals(path, exclude, StringComparison.OrdinalIgnoreCase)
                                        || path.StartsWith(exclude + ".", StringComparison.OrdinalIgnoreCase) && path.EndsWith(".tmp", StringComparison.OrdinalIgnoreCase));
    }

    // ---- engine properties ----

    internal static string EngineFormatName(string format) => format.ToLowerInvariant() switch
    {
        "gz" => "gzip",
        "bz2" => "bzip2",
        var f => f,
    };

    internal static List<(string Name, PropVariant Value)> BuildProperties(CompressionSettings s, string engineFormat, ArchiveSession? updating)
    {
        var props = new List<(string, PropVariant)>();
        var fmt = engineFormat.ToLowerInvariant();
        bool is7z = fmt == "7z";
        bool isZip = fmt == "zip";
        bool levelAware = fmt is "7z" or "zip" or "gzip" or "bzip2" or "xz";

        if (levelAware && (updating is null || s.Level != CompressionLevel.Normal || s.Method is not null))
            props.Add(("x", PropVariant.FromUInt32((uint)s.Level)));

        if (s.Level != CompressionLevel.Store || !is7z)
        {
            bool ppmd = string.Equals(s.Method, "PPMd", StringComparison.OrdinalIgnoreCase);
            string prefix = is7z ? "0" : "";
            if (s.Method is { } m && (is7z || isZip))
                props.Add((is7z ? "0" : "m", PropVariant.FromString(m)));
            if (s.DictionarySize is { } d && (is7z || isZip || fmt is "xz" or "bzip2"))
                props.Add((prefix + (ppmd ? "mem" : "d"), PropVariant.FromString($"{d}b")));
            if (s.WordSize is { } w && (is7z || isZip))
                props.Add((prefix + (ppmd ? "o" : "fb"), PropVariant.FromUInt32(w)));
        }

        if (is7z && s.SolidBlockSize is { } solid)
            props.Add(("s", PropVariant.FromString(solid switch { 0 => "off", ulong.MaxValue => "on", var b => $"{b}b" })));

        if (s.Threads is { } t && fmt is "7z" or "zip" or "xz" or "bzip2")
            props.Add(("mt", PropVariant.FromUInt32((uint)Math.Max(1, t))));

        if (s.MemoryLimitPercent is { } mem && levelAware)
            props.Add(("memuse", PropVariant.FromString($"{Math.Clamp(mem, 5, 100)}%")));

        if (!string.IsNullOrEmpty(s.Password))
        {
            if (is7z && s.EncryptFileNames)
                props.Add(("he", PropVariant.FromString("on")));
            if (isZip)
                props.Add(("em", PropVariant.FromString(s.ZipEncryption ?? "AES256")));
        }

        if (is7z)
        {
            if (s.StoreCreationTime) props.Add(("tc", PropVariant.FromString("on")));
            if (s.StoreAccessTime) props.Add(("ta", PropVariant.FromString("on")));
        }

        if (!string.IsNullOrWhiteSpace(s.ExtraParameters))
        {
            foreach (var token in s.ExtraParameters.Split([' ', ',', ';'], StringSplitOptions.RemoveEmptyEntries))
            {
                var eq = token.IndexOf('=');
                if (eq < 0)
                    props.Add((token, default));
                else
                    props.Add((token[..eq], PropVariant.FromString(token[(eq + 1)..])));
            }
        }
        return props;
    }

    internal static unsafe void ApplyProperties(object handler, List<(string Name, PropVariant Value)> props)
    {
        if (props.Count == 0)
            return;
        if (handler is not ISetProperties setter)
        {
            foreach (var (_, v) in props)
            {
                var copy = v;
                PropVariant.Clear(&copy);
            }
            return;
        }

        var names = new nint[props.Count];
        var values = new PropVariant[props.Count];
        try
        {
            for (int i = 0; i < props.Count; i++)
            {
                names[i] = Marshal.StringToHGlobalUni(props[i].Name);
                values[i] = props[i].Value;
            }
            int hr;
            fixed (nint* pNames = names)
            fixed (PropVariant* pValues = values)
                hr = setter.SetProperties((char**)pNames, pValues, (uint)props.Count);
            if (hr != 0)
                throw SevenZipException.FromHResult(hr, "Compression options");
        }
        finally
        {
            for (int i = 0; i < props.Count; i++)
            {
                if (names[i] != 0)
                    Marshal.FreeHGlobal(names[i]);
                fixed (PropVariant* p = &values[i])
                    PropVariant.Clear(p);
            }
        }
    }

    private static void TryDelete(string path)
    {
        try { File.Delete(path); } catch { /* best effort */ }
    }
}
