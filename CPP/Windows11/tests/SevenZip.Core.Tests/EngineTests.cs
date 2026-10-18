using System.Text;
using SevenZip.Core.Archives;
using SevenZip.Core.Compression;
using SevenZip.Core.Formats;
using SevenZip.Core.Hashing;
using SevenZip.Core.Interop;
using SevenZip.Core.Operations;
using Xunit;

namespace SevenZip.Core.Tests;

public sealed class TempDir : IDisposable
{
    public string Path { get; } = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "7zw-tests", Guid.NewGuid().ToString("N"));
    public TempDir() => Directory.CreateDirectory(Path);
    public string this[string relative] => System.IO.Path.Combine(Path, relative);
    public void Dispose()
    {
        try { Directory.Delete(Path, recursive: true); } catch { }
    }
}

internal sealed class ScriptedPrompts(string? password, OverwriteChoice overwrite = OverwriteChoice.Replace) : IOperationPrompts
{
    public int PasswordRequests;
    public int OverwriteRequests;
    public Task<string?> RequestPasswordAsync(string archiveName, bool retry)
    {
        Interlocked.Increment(ref PasswordRequests);
        return Task.FromResult(password);
    }
    public Task<OverwriteChoice> ResolveOverwriteAsync(OverwriteRequest request)
    {
        Interlocked.Increment(ref OverwriteRequests);
        return Task.FromResult(overwrite);
    }
}

public class EngineTests
{
    private static string MakeSourceTree(TempDir t)
    {
        var src = t["src"];
        Directory.CreateDirectory(System.IO.Path.Combine(src, "docs", "empty"));
        File.WriteAllText(System.IO.Path.Combine(src, "readme.txt"), "Hello from 7-Zip on Windows 11!");
        File.WriteAllText(System.IO.Path.Combine(src, "docs", "notes.md"), string.Concat(Enumerable.Repeat("compressible line\n", 5000)));
        var rnd = new byte[300_000];
        new Random(1).NextBytes(rnd);
        File.WriteAllBytes(System.IO.Path.Combine(src, "docs", "random.bin"), rnd);
        return src;
    }

    [Fact]
    public void EngineLoadsAndListsFormats()
    {
        var formats = FormatRegistry.All;
        Assert.Contains(formats, f => f.Name == "7z" && f.CanUpdate);
        Assert.Contains(formats, f => f.Name.Equals("zip", StringComparison.OrdinalIgnoreCase) && f.CanUpdate);
        Assert.Contains(formats, f => f.Name.Equals("Rar5", StringComparison.OrdinalIgnoreCase));
        Assert.True(formats.Count > 40);
        Assert.NotNull(SevenZipEngine.GetEngineVersion());
    }

    [Theory]
    [InlineData("7z")]
    [InlineData("zip")]
    [InlineData("tar")]
    [InlineData("tar.gz")]
    [InlineData("tar.xz")]
    [InlineData("tar.bz2")]
    [InlineData("wim")]
    public void RoundTripsAFolder(string format)
    {
        using var t = new TempDir();
        var src = MakeSourceTree(t);
        var caps = FormatCapabilities.Get(format);
        var archive = t["out." + caps.Extension];

        var ctx = new OperationContext();
        var result = ArchiveWriter.Create(archive, [src], new CompressionSettings { Format = format, Level = caps.Levels[^1] }, ctx);
        Assert.True(File.Exists(archive));
        Assert.Equal(3, result.Files);

        using var session = ArchiveSession.Open(archive);
        ArchiveSession target = session;
        ArchiveFolder root = session.Root;
        if (format.StartsWith("tar."))
            root = OpenInnerTar(session, t, out target);

        var srcFolder = root.GetFolder("src")!;
        Assert.NotNull(srcFolder);
        Assert.Contains(srcFolder.Files, f => f.Name == "readme.txt");
        Assert.NotNull(srcFolder.Navigate("docs\\empty"));

        var dest = t["dest"];
        var extract = ExtractEngine.Extract(target, null, new ExtractOptions { Destination = dest }, new OperationContext());
        Assert.Equal(0, extract.Errors);
        Assert.Equal(File.ReadAllBytes(System.IO.Path.Combine(src, "docs", "random.bin")), File.ReadAllBytes(System.IO.Path.Combine(dest, "src", "docs", "random.bin")));
        Assert.Equal(File.ReadAllText(System.IO.Path.Combine(src, "docs", "notes.md")), File.ReadAllText(System.IO.Path.Combine(dest, "src", "docs", "notes.md")));
        Assert.True(Directory.Exists(System.IO.Path.Combine(dest, "src", "docs", "empty")));
        if (!ReferenceEquals(target, session))
            target.Dispose();
    }

    private static ArchiveFolder OpenInnerTar(ArchiveSession outer, TempDir t, out ArchiveSession inner)
    {
        Assert.Single(outer.Entries);
        var tmp = t["inner"];
        ExtractEngine.Extract(outer, null, new ExtractOptions { Destination = tmp }, new OperationContext());
        var tarPath = Directory.GetFiles(tmp).Single();
        Assert.EndsWith(".tar", tarPath);
        inner = ArchiveSession.Open(tarPath, new ArchiveOpenOptions { Parent = outer });
        return inner.Root;
    }

    [Fact]
    public void EncryptedHeadersRequirePassword()
    {
        using var t = new TempDir();
        var src = MakeSourceTree(t);
        var archive = t["secret.7z"];
        ArchiveWriter.Create(archive, [src], new CompressionSettings { Password = "p@ss", EncryptFileNames = true }, new OperationContext());

        Assert.Throws<OperationCanceledException>(() => ArchiveSession.Open(archive, null, new OperationContext(prompts: new ScriptedPrompts(null))));
        Assert.Throws<WrongPasswordException>(() => ArchiveSession.Open(archive, null, new OperationContext(prompts: new ScriptedPrompts("nope"))));

        var prompts = new ScriptedPrompts("p@ss");
        using var session = ArchiveSession.Open(archive, null, new OperationContext(prompts: prompts));
        Assert.True(session.HeadersEncrypted);
        Assert.Equal(1, prompts.PasswordRequests);

        var result = ExtractEngine.Extract(session, null, new ExtractOptions { Destination = t["d"] }, new OperationContext(prompts: prompts));
        Assert.Equal(0, result.Errors);
        Assert.Equal(1, prompts.PasswordRequests); // password re-used from the session
    }

    [Fact]
    public void WrongPasswordOnZipIsReported()
    {
        using var t = new TempDir();
        var src = MakeSourceTree(t);
        var archive = t["secret.zip"];
        ArchiveWriter.Create(archive, [src], new CompressionSettings { Format = "zip", Password = "right" }, new OperationContext());
        using var session = ArchiveSession.Open(archive);
        Assert.True(session.HasEncryptedItems);
        var result = ExtractEngine.Extract(session, null, new ExtractOptions { Destination = t["d"], Password = "wrong" }, new OperationContext());
        Assert.True(result.WrongPassword);
        Assert.True(result.Errors > 0);
    }

    [Fact]
    public void UpdateAddsRenamesAndDeletes()
    {
        using var t = new TempDir();
        var src = MakeSourceTree(t);
        var archive = t["u.7z"];
        ArchiveWriter.Create(archive, [src], new CompressionSettings(), new OperationContext());

        var extra = t["extra.txt"];
        File.WriteAllText(extra, "added later");

        var session = ArchiveSession.Open(archive);
        var readme = session.Entries.Single(e => e.Name == "readme.txt");
        var docs = session.Root.Navigate("src\\docs")!;
        var plan = new UpdatePlan
        {
            Add = [extra],
            TargetFolder = "src\\docs",
            Delete = [readme.Index],
            Rename = new Dictionary<string, string> { ["src\\docs\\empty"] = "src\\docs\\still-empty" },
            NewFolders = ["src\\new folder"],
        };
        ArchiveWriter.Update(session, plan, null, new OperationContext());

        using var reopened = ArchiveSession.Open(archive);
        Assert.DoesNotContain(reopened.Entries, e => e.Name == "readme.txt");
        Assert.Contains(reopened.Entries, e => e.Path == "src\\docs\\extra.txt");
        Assert.NotNull(reopened.Root.Navigate("src\\docs\\still-empty"));
        Assert.Null(reopened.Root.Navigate("src\\docs\\empty"));
        Assert.NotNull(reopened.Root.Navigate("src\\new folder"));
        Assert.Contains(reopened.Entries, e => e.Path == "src\\docs\\random.bin");
    }

    [Fact]
    public void SplitVolumesOpenAsOneArchive()
    {
        using var t = new TempDir();
        var src = MakeSourceTree(t);
        var archive = t["vol.7z"];
        var result = ArchiveWriter.Create(archive, [src], new CompressionSettings { Level = CompressionLevel.Store, VolumeSize = 100_000 }, new OperationContext());
        Assert.True(result.OutputFiles.Count >= 3);
        Assert.EndsWith(".7z.001", result.OutputFiles[0]);

        using var session = ArchiveSession.Open(result.OutputFiles[0]);
        Assert.Equal("7z", session.Format.Name);
        Assert.Equal("Split", session.ContainerFormat);
        var test = ExtractEngine.Test(session, null, new OperationContext());
        Assert.Equal(0, test.Errors);
        Assert.Equal(3, test.FilesProcessed);
    }

    [Fact]
    public void ExtractionIsConfinedToDestination()
    {
        Assert.Equal(["etc", "passwd"], PathSanitizer.GetSafeComponents("..\\..\\etc\\passwd"));
        Assert.Equal(["Windows", "x"], PathSanitizer.GetSafeComponents("C:\\Windows\\x"));
        Assert.Equal(["_CON.txt"], PathSanitizer.GetSafeComponents("CON.txt"));
        Assert.Equal(["a_b_"], PathSanitizer.GetSafeComponents("a?b."));
        Assert.Null(PathSanitizer.CombineSafe("C:\\dest", ["..", "x"]));
        Assert.Equal("C:\\dest\\x", PathSanitizer.CombineSafe("C:\\dest", ["x"]));
    }

    [Fact]
    public void OverwritePromptsAndKeepBoth()
    {
        using var t = new TempDir();
        var src = MakeSourceTree(t);
        var archive = t["o.zip"];
        ArchiveWriter.Create(archive, [System.IO.Path.Combine(src, "readme.txt")], new CompressionSettings { Format = "zip" }, new OperationContext());
        using var session = ArchiveSession.Open(archive);
        var dest = t["d"];
        ExtractEngine.Extract(session, null, new ExtractOptions { Destination = dest }, new OperationContext());

        var prompts = new ScriptedPrompts(null, OverwriteChoice.KeepBoth);
        ExtractEngine.Extract(session, null, new ExtractOptions { Destination = dest }, new OperationContext(prompts: prompts));
        Assert.Equal(1, prompts.OverwriteRequests);
        Assert.True(File.Exists(System.IO.Path.Combine(dest, "readme (2).txt")));
    }

    [Fact]
    public void ZoneIdentifierPropagates()
    {
        using var t = new TempDir();
        var src = MakeSourceTree(t);
        var archive = t["web.zip"];
        ArchiveWriter.Create(archive, [System.IO.Path.Combine(src, "readme.txt")], new CompressionSettings { Format = "zip" }, new OperationContext());
        File.WriteAllText(archive + ":Zone.Identifier", "[ZoneTransfer]\r\nZoneId=3\r\n");

        using var session = ArchiveSession.Open(archive);
        var dest = t["d"];
        ExtractEngine.Extract(session, null, new ExtractOptions { Destination = dest }, new OperationContext());
        Assert.Contains("ZoneId=3", File.ReadAllText(System.IO.Path.Combine(dest, "readme.txt") + ":Zone.Identifier"));
    }

    [Fact]
    public void HashesMatchKnownVectors()
    {
        var abc = Encoding.ASCII.GetBytes("abc");
        Assert.Equal("352441C2", HashEngine.ComputeBytes(abc, HashEngine.Find("CRC32")!));
        Assert.Equal("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", HashEngine.ComputeBytes(abc, HashEngine.Find("SHA256")!));
        Assert.Contains(HashEngine.Algorithms, a => a.Name.StartsWith("BLAKE2", StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public void SmartDestinationAvoidsDoubleFolders()
    {
        using var t = new TempDir();
        var src = MakeSourceTree(t);
        var single = t["single.zip"];
        ArchiveWriter.Create(single, [src], new CompressionSettings { Format = "zip" }, new OperationContext());
        using (var s = ArchiveSession.Open(single))
            Assert.Equal(t.Path, ExtractPlanner.GetSmartDestination(s, t.Path));

        var loose = t["loose.tar.gz"];
        Assert.Equal("loose", ExtractPlanner.GetFolderNameFor(loose));
        Assert.Equal("data", ExtractPlanner.GetFolderNameFor("data.7z.001"));
    }

    [Fact]
    public void BenchmarkRoundTrips()
    {
        var data = Benchmark.BenchmarkRunner.GenerateData(4 << 20);
        var pass = Benchmark.BenchmarkRunner.RunPass(1, data, 1 << 22, 2, new OperationContext());
        Assert.True(pass.CompressMBps > 0 && pass.DecompressMBps > 0);
        Assert.InRange(pass.Ratio, 0.05, 0.9);
    }

    [Fact]
    public void FormatCount_UsesSingularOnlyForOne()
    {
        Assert.Equal("0 files", PropertyFormatter.FormatCount(0, "file"));
        Assert.Equal("1 file", PropertyFormatter.FormatCount(1, "file"));
        Assert.Equal("2 folders", PropertyFormatter.FormatCount(2, "folder"));
        Assert.Equal("1 entry", PropertyFormatter.FormatCount(1, "entry", "entries"));
        Assert.Equal("3 entries", PropertyFormatter.FormatCount(3, "entry", "entries"));
        Assert.Equal($"{1234:N0} items", PropertyFormatter.FormatCount(1234, "item"));
    }
}
