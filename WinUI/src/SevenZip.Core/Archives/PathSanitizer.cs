using System.Text;

namespace SevenZip.Core.Archives;

/// <summary>
/// Turns archive item paths into safe Windows paths: no traversal ("..", rooted, drive or UNC
/// prefixes), no reserved device names, no characters Windows rejects, no trailing dots/spaces.
/// </summary>
public static class PathSanitizer
{
    private static readonly HashSet<string> s_reserved = new(StringComparer.OrdinalIgnoreCase)
    {
        "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "COM¹", "COM²", "COM³",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9", "LPT¹", "LPT²", "LPT³",
    };

    /// <summary>Returns safe relative components, or an empty array when nothing remains.</summary>
    public static string[] GetSafeComponents(string archivePath, bool allowAltStream = false)
    {
        var parts = archivePath.Replace('/', '\\').Split('\\', StringSplitOptions.RemoveEmptyEntries);
        var result = new List<string>(parts.Length);
        for (int i = 0; i < parts.Length; i++)
        {
            var part = parts[i];
            if (part == "." || part == "..")
                continue;
            // Drop a leading drive designator ("C:") or "\\?\" style prefixes.
            if (result.Count == 0 && part.Length == 2 && part[1] == ':' && char.IsAsciiLetter(part[0]))
                continue;
            if (result.Count == 0 && part == "?")
                continue;
            bool last = i == parts.Length - 1;
            var clean = CleanComponent(part, allowAltStream && last);
            if (clean.Length > 0)
                result.Add(clean);
        }
        return result.ToArray();
    }

    public static string CleanComponent(string name, bool allowAltStream = false)
    {
        string? stream = null;
        if (allowAltStream)
        {
            int colon = name.IndexOf(':');
            if (colon > 0)
            {
                stream = CleanComponent(name[(colon + 1)..]);
                name = name[..colon];
            }
        }

        var sb = new StringBuilder(name.Length);
        foreach (var c in name)
            sb.Append(c < 32 || c is '<' or '>' or ':' or '"' or '|' or '?' or '*' ? '_' : c);

        // Windows silently strips trailing dots and spaces which could merge two items.
        int end = sb.Length;
        while (end > 0 && (sb[end - 1] == '.' || sb[end - 1] == ' '))
            end--;
        if (end < sb.Length)
        {
            if (end == 0 && sb.ToString() is "." or "..")
                return "";
            for (int i = end; i < sb.Length; i++)
                sb[i] = '_';
        }

        var result = sb.ToString();
        var stem = result.Split('.')[0].TrimEnd(' ');
        if (s_reserved.Contains(stem))
            result = "_" + result;
        if (stream is { Length: > 0 })
            result += ":" + stream;
        return result;
    }

    /// <summary>Combines and verifies that the result stays inside <paramref name="root"/>.</summary>
    public static string? CombineSafe(string root, IEnumerable<string> components)
    {
        var full = Path.GetFullPath(Path.Combine([root, .. components]));
        var normalizedRoot = Path.TrimEndingDirectorySeparator(Path.GetFullPath(root)) + Path.DirectorySeparatorChar;
        return full.StartsWith(normalizedRoot, StringComparison.OrdinalIgnoreCase) || string.Equals(full + Path.DirectorySeparatorChar, normalizedRoot, StringComparison.OrdinalIgnoreCase)
            ? full
            : null;
    }

    /// <summary>Finds a free "name (2).ext" next to an existing path.</summary>
    public static string MakeUnique(string path)
    {
        if (!File.Exists(path) && !Directory.Exists(path))
            return path;
        var dir = Path.GetDirectoryName(path) ?? "";
        var stem = Path.GetFileNameWithoutExtension(path);
        var ext = Path.GetExtension(path);
        for (int i = 2; ; i++)
        {
            var candidate = Path.Combine(dir, $"{stem} ({i}){ext}");
            if (!File.Exists(candidate) && !Directory.Exists(candidate))
                return candidate;
        }
    }
}
