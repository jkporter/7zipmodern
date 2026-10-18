using SevenZip.Core.Interop;
using SevenZip.Core.Operations;

namespace SevenZip.Core.Hashing;

public sealed record HashAlgorithmInfo(uint Index, string Name, uint DigestSize);

public sealed class FileHashResult
{
    public required string Path { get; init; }
    public required string RelativePath { get; init; }
    public required ulong Size { get; init; }
    /// <summary>Algorithm name → hex digest (upper case, 7-Zip's byte order).</summary>
    public required IReadOnlyDictionary<string, string> Digests { get; init; }
    public string? Error { get; init; }
}

/// <summary>Checksums through the engine's own hashers (CRC32, CRC64, SHA-1/256/384/512, SHA3, BLAKE2sp, XXH64, MD5…).</summary>
public static unsafe class HashEngine
{
    private static IReadOnlyList<HashAlgorithmInfo>? s_algorithms;

    public static IReadOnlyList<HashAlgorithmInfo> Algorithms => s_algorithms ??= LoadAlgorithms();

    /// <summary>Order shown in the UI; anything else the engine offers follows.</summary>
    private static readonly string[] s_preferred = ["CRC32", "CRC64", "SHA256", "SHA1", "SHA512", "SHA384", "SHA3-256", "BLAKE2sp", "XXH64", "MD5"];

    private static IReadOnlyList<HashAlgorithmInfo> LoadAlgorithms()
    {
        var hashers = SevenZipEngine.GetHashers();
        uint n = hashers.GetNumHashers();
        var list = new List<HashAlgorithmInfo>();
        for (uint i = 0; i < n; i++)
        {
            PropVariant v = default;
            hashers.GetHasherProp(i, MethodPropId.Name, &v);
            var name = v.AsString();
            PropVariant.Clear(&v);
            hashers.GetHasherProp(i, MethodPropId.DigestSize, &v);
            uint size = v.AsUInt32() ?? 0;
            PropVariant.Clear(&v);
            if (!string.IsNullOrEmpty(name) && size > 0)
                list.Add(new HashAlgorithmInfo(i, name, size));
        }
        return list
            .OrderBy(a => Array.FindIndex(s_preferred, p => string.Equals(p, a.Name, StringComparison.OrdinalIgnoreCase)) is var k && k >= 0 ? k : 100)
            .ThenBy(a => a.Name, StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    public static HashAlgorithmInfo? Find(string name)
        => Algorithms.FirstOrDefault(a => string.Equals(a.Name, name, StringComparison.OrdinalIgnoreCase)
                                          || string.Equals(a.Name.Replace("-", ""), name.Replace("-", ""), StringComparison.OrdinalIgnoreCase));

    /// <summary>Expands folders and hashes every file.</summary>
    public static IReadOnlyList<FileHashResult> ComputeFiles(IEnumerable<string> paths, IReadOnlyList<HashAlgorithmInfo> algorithms, OperationContext ctx,
        Action<FileHashResult>? onFile = null)
    {
        var files = new List<(string Full, string Relative)>();
        foreach (var p in paths)
        {
            var full = Path.GetFullPath(p);
            if (File.Exists(full))
            {
                files.Add((full, Path.GetFileName(full)));
            }
            else if (Directory.Exists(full))
            {
                var baseDir = Path.GetDirectoryName(full.TrimEnd('\\')) ?? full;
                foreach (var f in Directory.EnumerateFiles(full, "*", new EnumerationOptions { RecurseSubdirectories = true, IgnoreInaccessible = true, AttributesToSkip = 0 }))
                    files.Add((f, Path.GetRelativePath(baseDir, f)));
            }
        }
        files.Sort((a, b) => string.Compare(a.Relative, b.Relative, StringComparison.OrdinalIgnoreCase));

        ctx.TotalFiles = (ulong)files.Count;
        ctx.TotalBytes = 0;
        foreach (var (full, _) in files)
        {
            try { ctx.TotalBytes += (ulong)new FileInfo(full).Length; } catch (IOException) { }
        }

        var hashers = SevenZipEngine.GetHashers();
        var instances = new List<(HashAlgorithmInfo Info, IHasher Hasher)>();
        foreach (var alg in algorithms)
        {
            if (hashers.CreateHasher(alg.Index, out var h) == 0 && h is not null)
                instances.Add((alg, h));
        }

        var results = new List<FileHashResult>(files.Count);
        var buffer = GC.AllocateUninitializedArray<byte>(1 << 20, pinned: true);
        foreach (var (full, relative) in files)
        {
            ctx.Cancellation.ThrowIfCancellationRequested();
            ctx.CurrentItem = relative;
            FileHashResult result;
            try
            {
                foreach (var (_, h) in instances)
                    h.Init();
                ulong size = 0;
                using (var fs = new FileStream(full, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1, FileOptions.SequentialScan))
                {
                    int read;
                    fixed (byte* p = buffer)
                    {
                        while ((read = fs.Read(buffer, 0, buffer.Length)) > 0)
                        {
                            if (ctx.Checkpoint() != 0)
                                throw new OperationCanceledException();
                            foreach (var (_, h) in instances)
                                h.Update(p, (uint)read);
                            size += (uint)read;
                            ctx.CompletedBytes += (uint)read;
                            ctx.Report();
                        }
                    }
                }
                result = new FileHashResult
                {
                    Path = full,
                    RelativePath = relative,
                    Size = size,
                    Digests = instances.ToDictionary(x => x.Info.Name, x => FinalHex(x.Hasher, x.Info.DigestSize)),
                };
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                result = new FileHashResult { Path = full, RelativePath = relative, Size = 0, Digests = new Dictionary<string, string>(), Error = ex.Message };
                ctx.AddMessage(MessageSeverity.Error, ex.Message, full);
            }
            ctx.CompletedFiles++;
            results.Add(result);
            onFile?.Invoke(result);
        }
        ctx.Report(force: true);
        return results;
    }

    public static string ComputeBytes(ReadOnlySpan<byte> data, HashAlgorithmInfo algorithm)
    {
        var hashers = SevenZipEngine.GetHashers();
        if (hashers.CreateHasher(algorithm.Index, out var h) != 0 || h is null)
            throw new NotSupportedException(algorithm.Name);
        h.Init();
        fixed (byte* p = data)
            h.Update(p, (uint)data.Length);
        return FinalHex(h, algorithm.DigestSize);
    }

    private static string FinalHex(IHasher hasher, uint digestSize)
    {
        Span<byte> digest = stackalloc byte[(int)Math.Max(digestSize, 64)];
        fixed (byte* p = digest)
            hasher.Final(p);
        var d = digest[..(int)digestSize];
        // CRC/XXH style digests (<= 8 bytes) are printed as little-endian numbers, like 7-Zip does.
        if (digestSize <= 8)
        {
            var copy = d.ToArray();
            Array.Reverse(copy);
            return Convert.ToHexString(copy);
        }
        return Convert.ToHexString(d);
    }

    /// <summary>Writes a checksum file in the common "hash *name" format (sha256sum compatible).</summary>
    public static string FormatChecksumFile(IEnumerable<FileHashResult> results, string algorithm)
    {
        var sb = new System.Text.StringBuilder();
        foreach (var r in results)
            if (r.Digests.TryGetValue(algorithm, out var hex))
                sb.Append(hex.ToLowerInvariant()).Append(" *").Append(r.RelativePath.Replace('\\', '/')).Append('\n');
        return sb.ToString();
    }
}
