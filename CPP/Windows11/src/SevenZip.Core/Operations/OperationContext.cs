using System.Diagnostics;
using SevenZip.Core.Interop;

namespace SevenZip.Core.Operations;

public enum OperationKind { Open, Extract, Test, Compress, Update, Hash, Benchmark }

public enum MessageSeverity { Info, Warning, Error }

public sealed record OperationMessage(MessageSeverity Severity, string Text, string? ItemPath = null);

/// <summary>Snapshot of operation progress. Byte counts refer to uncompressed (input) data.</summary>
public readonly record struct OperationProgress(
    ulong TotalBytes,
    ulong CompletedBytes,
    ulong TotalFiles,
    ulong CompletedFiles,
    ulong OutputBytes,
    string? CurrentItem,
    TimeSpan Elapsed)
{
    public double Fraction => TotalBytes == 0 ? 0 : Math.Clamp((double)CompletedBytes / TotalBytes, 0, 1);
    public double BytesPerSecond => Elapsed.TotalSeconds < 0.25 ? 0 : CompletedBytes / Elapsed.TotalSeconds;
    public TimeSpan? Remaining
    {
        get
        {
            var speed = BytesPerSecond;
            if (speed <= 0 || CompletedBytes == 0 || TotalBytes <= CompletedBytes)
                return null;
            return TimeSpan.FromSeconds((TotalBytes - CompletedBytes) / speed);
        }
    }
    public double? Ratio => CompletedBytes == 0 || OutputBytes == 0 ? null : (double)OutputBytes / CompletedBytes;
}

public enum OverwriteChoice { Replace, ReplaceAll, Skip, SkipAll, KeepBoth, KeepBothAll, Cancel }

public sealed record OverwriteRequest(
    string TargetPath,
    long ExistingSize,
    DateTime? ExistingModified,
    ulong NewSize,
    DateTime? NewModified);

/// <summary>Interactive questions the engine may need answered mid-operation. Calls arrive on worker threads.</summary>
public interface IOperationPrompts
{
    /// <summary>Returns null when the user cancels.</summary>
    Task<string?> RequestPasswordAsync(string archiveName, bool retry);
    Task<OverwriteChoice> ResolveOverwriteAsync(OverwriteRequest request);
}

/// <summary>Cancellation, pause, progress and prompt plumbing shared by all engine operations.</summary>
public sealed class OperationContext
{
    private readonly ManualResetEventSlim _runGate = new(true);
    private readonly Stopwatch _clock = new();
    private readonly List<OperationMessage> _messages = [];
    private long _lastReportTicks;
    private TimeSpan _pausedTotal;
    private long _pauseStarted;

    public OperationContext(CancellationToken cancellation = default, IProgress<OperationProgress>? progress = null, IOperationPrompts? prompts = null)
    {
        Cancellation = cancellation;
        Progress = progress;
        Prompts = prompts;
        _clock.Start();
    }

    public CancellationToken Cancellation { get; }
    public IProgress<OperationProgress>? Progress { get; }
    public IOperationPrompts? Prompts { get; }

    public ulong TotalBytes { get; set; }
    public ulong CompletedBytes { get; set; }
    public ulong TotalFiles { get; set; }
    public ulong CompletedFiles { get; set; }
    public ulong OutputBytes { get; set; }
    public string? CurrentItem { get; set; }

    /// <summary>Progress throttle interval; the UI only needs ~10 updates per second.</summary>
    public TimeSpan ReportInterval { get; init; } = TimeSpan.FromMilliseconds(100);

    public bool IsPaused => !_runGate.IsSet;

    public IReadOnlyList<OperationMessage> Messages
    {
        get { lock (_messages) return _messages.ToArray(); }
    }

    public bool HasErrors
    {
        get { lock (_messages) return _messages.Any(m => m.Severity == MessageSeverity.Error); }
    }

    public TimeSpan Elapsed => _clock.Elapsed - _pausedTotal;

    public void Pause()
    {
        if (_runGate.IsSet)
        {
            _pauseStarted = Stopwatch.GetTimestamp();
            _runGate.Reset();
        }
    }

    public void Resume()
    {
        if (!_runGate.IsSet)
        {
            _pausedTotal += Stopwatch.GetElapsedTime(_pauseStarted);
            _runGate.Set();
        }
    }

    public void AddMessage(MessageSeverity severity, string text, string? item = null)
    {
        lock (_messages)
            _messages.Add(new OperationMessage(severity, text, item));
    }

    /// <summary>Called from engine callbacks: blocks while paused, returns E_ABORT when cancelled.</summary>
    internal int Checkpoint()
    {
        if (!_runGate.IsSet)
        {
            try { _runGate.Wait(Cancellation); }
            catch (OperationCanceledException) { return HResults.E_ABORT; }
        }
        return Cancellation.IsCancellationRequested ? HResults.E_ABORT : HResults.S_OK;
    }

    internal void Report(bool force = false)
    {
        if (Progress is null)
            return;
        long now = Stopwatch.GetTimestamp();
        if (!force && Stopwatch.GetElapsedTime(_lastReportTicks, now) < ReportInterval)
            return;
        _lastReportTicks = now;
        Progress.Report(new OperationProgress(TotalBytes, CompletedBytes, TotalFiles, CompletedFiles, OutputBytes, CurrentItem, Elapsed));
    }

    /// <summary>Synchronously waits for a UI prompt; engine callbacks are synchronous by contract.</summary>
    internal static T Wait<T>(Task<T> task) => task.ConfigureAwait(false).GetAwaiter().GetResult();
}

public class SevenZipException : Exception
{
    public SevenZipException(string message, int hresult = HResults.E_FAIL) : base(message) => HResult = hresult;

    public static SevenZipException FromHResult(int hr, string context)
    {
        string detail = hr switch
        {
            HResults.E_OUTOFMEMORY => "There is not enough memory for this operation.",
            HResults.E_NOTIMPL => "The operation is not supported for this archive type.",
            HResults.E_INVALIDARG => "An option is not valid for this archive type.",
            HResults.S_FALSE => "The data is not a supported archive or is damaged.",
            _ => System.Runtime.InteropServices.Marshal.GetExceptionForHR(hr)?.Message ?? $"Error 0x{hr:X8}",
        };
        return new SevenZipException($"{context}: {detail}", hr);
    }
}

public sealed class WrongPasswordException : SevenZipException
{
    public WrongPasswordException(string archive) : base($"The password for '{archive}' is incorrect.") { }
}

internal static class Hr
{
    public static void Check(int hr, string context, OperationContext? ctx = null)
    {
        if (hr == HResults.S_OK)
            return;
        if (hr == HResults.E_ABORT)
            throw new OperationCanceledException(ctx?.Cancellation ?? default);
        throw SevenZipException.FromHResult(hr, context);
    }
}
