using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml;
using SevenZip.App.Services;
using SevenZip.Core.Archives;
using SevenZip.Core.Benchmark;
using SevenZip.Core.Hashing;
using SevenZip.Core.Operations;

namespace SevenZip.App.ViewModels;

public sealed partial class AlgorithmOption : ObservableObject
{
    public AlgorithmOption(HashAlgorithmInfo info, bool selected)
    {
        Info = info;
        IsSelected = selected;
    }
    public HashAlgorithmInfo Info { get; }
    public string Name => Info.Name;
    [ObservableProperty] public partial bool IsSelected { get; set; }
}

public sealed partial class DigestRow : ObservableObject
{
    public DigestRow(string algorithm, string value)
    {
        Algorithm = algorithm;
        Value = value;
    }
    public string Algorithm { get; }
    public string Value { get; }
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(MatchVisibility))]
    public partial bool Matches { get; set; }
    public Visibility MatchVisibility => Matches ? Visibility.Visible : Visibility.Collapsed;
}

public sealed class FileHashViewModel
{
    public FileHashViewModel(FileHashResult r)
    {
        Result = r;
        Digests = r.Digests.Select(kv => new DigestRow(kv.Key, kv.Value)).ToList();
    }
    public FileHashResult Result { get; }
    public string Name => Result.RelativePath;
    public string SizeText => Result.Error ?? PropertyFormatter.FormatSize(Result.Size);
    public IReadOnlyList<DigestRow> Digests { get; }
}

/// <summary>Checksums page (successor of the CRC SHA context submenu + its results dialog).</summary>
public sealed partial class HashViewModel : ObservableObject
{
    public HashViewModel(IReadOnlyList<string> paths, string? algorithm)
    {
        foreach (var p in paths)
            Sources.Add(new SourceItem(p));
        var preferred = algorithm is null ? new[] { "CRC32", "SHA256" } : [algorithm];
        foreach (var a in HashEngine.Algorithms)
            Algorithms.Add(new AlgorithmOption(a, preferred.Any(p => string.Equals(p.Replace("-", ""), a.Name.Replace("-", ""), StringComparison.OrdinalIgnoreCase)) || algorithm == "*"));
    }

    public ObservableCollection<SourceItem> Sources { get; } = [];
    public ObservableCollection<AlgorithmOption> Algorithms { get; } = [];
    public ObservableCollection<FileHashViewModel> Results { get; } = [];

    [ObservableProperty] public partial bool IsWorking { get; set; }
    [ObservableProperty] public partial string Expected { get; set; } = "";
    [ObservableProperty] public partial string VerifyText { get; private set; } = "";
    [ObservableProperty] public partial bool? VerifyOk { get; private set; }

    partial void OnExpectedChanged(string value) => Verify();

    /// <summary>Paste a published checksum: every digest is compared, regardless of algorithm.</summary>
    private void Verify()
    {
        var expected = new string(Expected.Where(Uri.IsHexDigit).ToArray());
        bool any = false;
        foreach (var r in Results)
            foreach (var d in r.Digests)
            {
                d.Matches = expected.Length >= 8 && string.Equals(d.Value, expected, StringComparison.OrdinalIgnoreCase);
                any |= d.Matches;
            }
        if (expected.Length < 8)
        {
            VerifyOk = null;
            VerifyText = "";
        }
        else if (any)
        {
            var hit = Results.SelectMany(r => r.Digests.Where(d => d.Matches).Select(d => (r.Name, d.Algorithm))).First();
            VerifyOk = true;
            VerifyText = $"Match: {hit.Algorithm} of {hit.Name}";
        }
        else
        {
            VerifyOk = false;
            VerifyText = Results.Count == 0 ? "Compute checksums first." : "No match. The file may be damaged or different.";
        }
    }

    public async Task ComputeAsync(IWindowHost host)
    {
        var algorithms = Algorithms.Where(a => a.IsSelected).Select(a => a.Info).ToList();
        if (algorithms.Count == 0 || Sources.Count == 0)
            return;
        Results.Clear();
        IsWorking = true;
        var paths = Sources.Select(s => s.Path).ToList();
        var dispatcher = host.Dialogs.Dispatcher;
        var op = new OperationViewModel(OperationKind.Hash, "Computing checksums", null);
        try
        {
            await host.Operations.RunAsync(op,
                ctx => HashEngine.ComputeFiles(paths, algorithms, ctx, r => dispatcher.TryEnqueue(() => Results.Add(new FileHashViewModel(r)))),
                r => $"{r.Count} files");
            Verify();
        }
        catch (Exception ex)
        {
            await host.Dialogs.ShowErrorAsync("Couldn't compute checksums", ex.Message);
        }
        finally
        {
            IsWorking = false;
        }
    }

    public string ExportText(string algorithm) => HashEngine.FormatChecksumFile(Results.Select(r => r.Result), algorithm);

    public string CopyAllText()
    {
        var sb = new System.Text.StringBuilder();
        foreach (var r in Results)
        {
            sb.AppendLine(r.Name);
            foreach (var d in r.Digests)
                sb.Append("  ").Append(d.Algorithm.PadRight(10)).Append(' ').AppendLine(d.Value);
        }
        return sb.ToString();
    }
}

public sealed partial class BenchmarkViewModel : ObservableObject
{
    private CancellationTokenSource? _cts;

    public BenchmarkViewModel()
    {
        Dictionaries = [new("4 MB", 4UL << 20), new("16 MB", 16UL << 20), new("32 MB", 32UL << 20), new("64 MB", 64UL << 20)];
        SelectedDictionary = Dictionaries[2];
        Threads = Environment.ProcessorCount;
    }

    public IReadOnlyList<ChoiceItem<ulong>> Dictionaries { get; }
    public int MaxThreads => Environment.ProcessorCount * 2;
    public ObservableCollection<BenchmarkPass> Passes { get; } = [];

    [ObservableProperty] public partial ChoiceItem<ulong> SelectedDictionary { get; set; }
    [ObservableProperty] public partial double Threads { get; set; }
    [ObservableProperty] public partial bool IsRunning { get; set; }
    [ObservableProperty] public partial string CompressSpeed { get; set; } = "—";
    [ObservableProperty] public partial string DecompressSpeed { get; set; } = "—";
    [ObservableProperty] public partial string AverageText { get; set; } = "Measures LZMA2 compression and decompression speed with the real engine. Data is generated in memory; nothing is written to disk.";
    [ObservableProperty] public partial string? Error { get; set; }

    public async Task StartAsync()
    {
        if (IsRunning)
        {
            _cts?.Cancel();
            return;
        }
        Error = null;
        Passes.Clear();
        IsRunning = true;
        _cts = new CancellationTokenSource();
        var token = _cts.Token;
        var dict = SelectedDictionary.Value;
        int threads = (int)Threads;
        try
        {
            var data = await Task.Run(() => BenchmarkRunner.GenerateData((int)Math.Max(dict * 2, 32UL << 20)), token);
            for (int pass = 1; pass <= 10 && !token.IsCancellationRequested; pass++)
            {
                var result = await Task.Factory.StartNew(() => BenchmarkRunner.RunPass(pass, data, dict, threads, new OperationContext(token)),
                    token, TaskCreationOptions.LongRunning, TaskScheduler.Default);
                Passes.Add(result);
                CompressSpeed = $"{result.CompressMBps:N1} MB/s";
                DecompressSpeed = $"{result.DecompressMBps:N1} MB/s";
                AverageText = $"Average of {Passes.Count} passes: compress {Passes.Average(p => p.CompressMBps):N1} MB/s · decompress {Passes.Average(p => p.DecompressMBps):N1} MB/s · ratio {Passes.Average(p => p.Ratio):P0}";
            }
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception ex)
        {
            Error = ex.Message;
        }
        finally
        {
            IsRunning = false;
        }
    }
}
