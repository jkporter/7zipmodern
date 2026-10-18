using Microsoft.Windows.AppLifecycle;
using SevenZip.App.Interop;
using SevenZip.Core.Formats;
using Windows.ApplicationModel.Activation;

namespace SevenZip.App.Activation;

public enum LaunchVerb
{
    Home,
    Open,
    /// <summary>"Extract files…" — ask for options first.</summary>
    Extract,
    ExtractHere,
    /// <summary>"Extract to smart folder" (classic: Extract to "name\").</summary>
    ExtractSmart,
    Test,
    /// <summary>"Add to archive…" — full options in the main window.</summary>
    Compress,
    Compress7z,
    CompressZip,
    /// <summary>Compress to a temporary zip and open the Windows Share sheet.</summary>
    CompressShare,
    Hash,
    Benchmark,
    Settings,
    /// <summary>Toast button.</summary>
    Reveal,
}

/// <summary>
/// What the app was asked to do, from any activation source: command line / execution alias,
/// File Explorer context menu (via the shell extension), file-type association, Share target,
/// jump list task or a toast.
/// </summary>
public sealed class LaunchRequest
{
    public LaunchVerb Verb { get; init; }
    public IReadOnlyList<string> Paths { get; init; } = [];
    public string? Format { get; init; }
    public string? HashAlgorithm { get; init; }
    public Windows.ApplicationModel.DataTransfer.ShareTarget.ShareOperation? ShareOperation { get; init; }

    /// <summary>Verbs that run headless in their own process with a compact progress window (like classic 7zG).</summary>
    public bool RunsInOwnWindow => Verb is LaunchVerb.Extract or LaunchVerb.ExtractHere or LaunchVerb.ExtractSmart or LaunchVerb.Test
        or LaunchVerb.Compress7z or LaunchVerb.CompressZip or LaunchVerb.CompressShare or LaunchVerb.Reveal;

    public static LaunchRequest FromActivation(AppActivationArguments args)
    {
        switch (args.Kind)
        {
            case ExtendedActivationKind.File when args.Data is IFileActivatedEventArgs file:
            {
                var paths = file.Files.Select(f => f.Path).Where(p => !string.IsNullOrEmpty(p)).ToList();
                var verb = ParseVerb(file.Verb) ?? LaunchVerb.Open;
                return new LaunchRequest { Verb = verb, Paths = paths };
            }
            case ExtendedActivationKind.ShareTarget when args.Data is IShareTargetActivatedEventArgs share:
                return new LaunchRequest { Verb = LaunchVerb.Compress, ShareOperation = share.ShareOperation };
            case ExtendedActivationKind.AppNotification when args.Data is Microsoft.Windows.AppNotifications.AppNotificationActivatedEventArgs toast:
                return FromToast(toast.Arguments);
            case ExtendedActivationKind.Launch when args.Data is ILaunchActivatedEventArgs launch:
                return Parse(Win32.SplitCommandLine(launch.Arguments ?? ""), skipExe: false);
            default:
                return Parse(Environment.GetCommandLineArgs(), skipExe: true);
        }
    }

    public static LaunchRequest FromToast(IDictionary<string, string> arguments)
    {
        if (arguments.TryGetValue("action", out var action) && action == "reveal" && arguments.TryGetValue("path", out var path))
            return new LaunchRequest { Verb = LaunchVerb.Reveal, Paths = [path] };
        return new LaunchRequest();
    }

    /// <summary>
    /// Command line: <c>SevenZip [verb] [--format NAME] [--algorithm NAME] paths…</c>. Paths may be
    /// given as <c>@listfile</c> (UTF-16/UTF-8 lines), which the shell extension uses for big selections.
    /// </summary>
    public static LaunchRequest Parse(IReadOnlyList<string> argv, bool skipExe)
    {
        var args = argv.Skip(skipExe ? 1 : 0).ToList();
        if (args.Count > 0 && args[0].EndsWith(".exe", StringComparison.OrdinalIgnoreCase) && File.Exists(args[0]) && !FormatRegistry.LooksLikeArchive(args[0]))
            args.RemoveAt(0);

        LaunchVerb? verb = null;
        string? format = null, algorithm = null;
        var paths = new List<string>();
        for (int i = 0; i < args.Count; i++)
        {
            var a = args[i];
            if (a is "--format" or "-t" && i + 1 < args.Count) { format = args[++i]; continue; }
            if (a is "--algorithm" or "-a" && i + 1 < args.Count) { algorithm = args[++i]; continue; }
            if (verb is null && paths.Count == 0 && ParseVerb(a) is { } v) { verb = v; continue; }
            if (a.StartsWith('@') && File.Exists(a[1..]))
            {
                paths.AddRange(ReadListFile(a[1..]));
                continue;
            }
            if (a.Length > 0)
                paths.Add(Path.GetFullPath(a));
        }

        if (verb is null)
        {
            // Bare paths: archives open, anything else goes to "Create archive".
            verb = paths.Count == 0 ? LaunchVerb.Home
                : paths.All(p => File.Exists(p) && FormatRegistry.LooksLikeArchive(p)) ? LaunchVerb.Open
                : LaunchVerb.Compress;
        }
        return new LaunchRequest { Verb = verb.Value, Paths = paths, Format = format, HashAlgorithm = algorithm };
    }

    private static IEnumerable<string> ReadListFile(string listFile)
    {
        string[] lines;
        try
        {
            lines = File.ReadAllLines(listFile);
        }
        catch
        {
            yield break;
        }
        // List files written by the shell extension live in %TEMP% and are single-use.
        if (Path.GetFullPath(listFile).StartsWith(Path.GetTempPath(), StringComparison.OrdinalIgnoreCase))
        {
            try { File.Delete(listFile); } catch { }
        }
        foreach (var line in lines)
            if (line.Trim() is { Length: > 0 } p)
                yield return p;
    }

    private static LaunchVerb? ParseVerb(string? s) => s?.ToLowerInvariant() switch
    {
        "open" => LaunchVerb.Open,
        "extract" => LaunchVerb.Extract,
        "extract-here" or "x-here" => LaunchVerb.ExtractHere,
        "extract-smart" or "extract-to" => LaunchVerb.ExtractSmart,
        "test" => LaunchVerb.Test,
        "compress" or "add" => LaunchVerb.Compress,
        "compress-7z" => LaunchVerb.Compress7z,
        "compress-zip" => LaunchVerb.CompressZip,
        "compress-share" => LaunchVerb.CompressShare,
        "hash" => LaunchVerb.Hash,
        "benchmark" => LaunchVerb.Benchmark,
        "settings" => LaunchVerb.Settings,
        _ => null,
    };
}
