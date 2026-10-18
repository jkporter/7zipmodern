using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;
using SevenZip.Core.Compression;
using SevenZip.Core.Formats;
using SevenZip.Core.Interop;
using SevenZip.Core.Operations;

namespace SevenZip.Core.Benchmark;

public sealed record BenchmarkPass(int Pass, double CompressMBps, double DecompressMBps, double Ratio, int Threads, ulong Dictionary);

/// <summary>
/// LZMA2 round-trip benchmark through the real engine (the classic dialog's numbers, simplified):
/// compresses generated, moderately compressible data in memory and decompresses it again.
/// </summary>
public static unsafe partial class BenchmarkRunner
{
    public static byte[] GenerateData(int size, int seed = 0x7A1)
    {
        // Mix of literal noise and back-references, ~3:1 under LZMA like typical binaries/text.
        var data = new byte[size];
        var rng = new Random(seed);
        int pos = 0;
        while (pos < size)
        {
            if (pos > 4096 && rng.Next(4) != 0)
            {
                int dist = rng.Next(1, Math.Min(pos, 1 << rng.Next(4, 22)));
                int len = Math.Min(size - pos, rng.Next(3, 96));
                for (int i = 0; i < len; i++, pos++)
                    data[pos] = data[pos - dist];
            }
            else
            {
                int len = Math.Min(size - pos, rng.Next(1, 32));
                for (int i = 0; i < len; i++)
                    data[pos++] = (byte)(rng.Next(64) + (rng.Next(8) == 0 ? rng.Next(192) : 32));
            }
        }
        return data;
    }

    public static BenchmarkPass RunPass(int pass, byte[] data, ulong dictionary, int threads, OperationContext ctx)
    {
        var format = FormatRegistry.Get("7z");

        // Compress
        var output = new MemoryStream(data.Length / 2);
        var outArchive = SevenZipEngine.CreateObject<IOutArchive>(format.ClassId);
        ArchiveWriter.ApplyProperties(outArchive,
        [
            ("x", PropVariant.FromUInt32(5)),
            ("0", PropVariant.FromString("LZMA2")),
            ("0d", PropVariant.FromString($"{dictionary}b")),
            ("mt", PropVariant.FromUInt32((uint)threads)),
        ]);
        var items = new List<UpdateItem>
        {
            new() { NewData = true, NewProps = true, ArchivePath = "bench.bin", Size = (ulong)data.Length, Attributes = 0x20,
                    Modified = DateTime.UtcNow, OpenSource = () => new MemoryStream(data, writable: false) },
        };
        var callback = new UpdateCallback(items, ctx, null, null, false, false);
        var sw = Stopwatch.StartNew();
        int hr = outArchive.UpdateItems(new OutStreamWrapper(output, ownsStream: false), 1, callback);
        callback.CloseAll();
        Hr.Check(hr, "Benchmark (compress)", ctx);
        double compressSeconds = sw.Elapsed.TotalSeconds;

        // Decompress
        output.Position = 0;
        var inArchive = SevenZipEngine.CreateObject<IInArchive>(format.ClassId);
        ulong maxCheck = 0;
        Hr.Check(inArchive.Open(new InStreamWrapper(output, ownsStream: false), &maxCheck, null), "Benchmark (open)", ctx);
        var sink = new BenchExtractCallback(ctx);
        sw.Restart();
        hr = inArchive.Extract(null, uint.MaxValue, 0, sink);
        double decompressSeconds = sw.Elapsed.TotalSeconds;
        inArchive.Close();
        Hr.Check(hr, "Benchmark (decompress)", ctx);
        if (sink.Failed || sink.Bytes != data.Length)
            throw new SevenZipException("Benchmark verification failed: decompressed data does not match. Your hardware may be unstable.");

        double mb = data.Length / (1024.0 * 1024.0);
        return new BenchmarkPass(pass, mb / compressSeconds, mb / decompressSeconds, (double)output.Length / data.Length, threads, dictionary);
    }

    [GeneratedComClass]
    private sealed partial class BenchExtractCallback : IArchiveExtractCallback
    {
        private readonly OperationContext _ctx;
        public BenchExtractCallback(OperationContext ctx) => _ctx = ctx;
        public long Bytes { get; private set; }
        public bool Failed { get; private set; }

        public int SetTotal(ulong total) => _ctx.Checkpoint();
        public int SetCompleted(ulong* completeValue) => _ctx.Checkpoint();

        public int GetStream(uint index, nint* outStream, int askExtractMode)
        {
            *outStream = askExtractMode == AskMode.Extract
                ? ComOut.ToNative<ISequentialOutStream>(new SinkOutStream(span => Bytes += span.Length))
                : 0;
            return HResults.S_OK;
        }

        public int PrepareOperation(int askExtractMode) => HResults.S_OK;

        public int SetOperationResult(int opRes)
        {
            if (opRes != 0)
                Failed = true;
            return HResults.S_OK;
        }
    }
}
