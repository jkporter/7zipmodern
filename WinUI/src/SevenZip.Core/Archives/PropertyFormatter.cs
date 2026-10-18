using System.Globalization;
using SevenZip.Core.Interop;

namespace SevenZip.Core.Archives;

public static class PropertyFormatter
{
    public static string FormatSize(ulong bytes)
    {
        string[] units = ["bytes", "KB", "MB", "GB", "TB", "PB"];
        if (bytes < 1024)
            return bytes == 1 ? "1 byte" : $"{bytes:N0} bytes";
        double v = bytes;
        int u = 0;
        while (v >= 1024 && u < units.Length - 1)
        {
            v /= 1024;
            u++;
        }
        return v >= 100 ? $"{v:N0} {units[u]}" : v >= 10 ? $"{v:N1} {units[u]}" : $"{v:N2} {units[u]}";
    }

    public static string FormatSizeExact(ulong bytes) => $"{FormatSize(bytes)} ({bytes:N0} bytes)";

    /// <summary>"1 file", "2 files", "1,024 files". Pass <paramref name="plural"/> when it isn't singular + "s".</summary>
    public static string FormatCount(long count, string singular, string? plural = null)
        => $"{count:N0} {(count == 1 ? singular : plural ?? singular + "s")}";

    public static string FormatAttributes(uint attrib)
    {
        // High 16 bits may carry POSIX mode when bit 15 (FILE_ATTRIBUTE_UNIX_EXTENSION) is set.
        Span<char> s = stackalloc char[8];
        int n = 0;
        if ((attrib & 0x01) != 0) s[n++] = 'R';
        if ((attrib & 0x02) != 0) s[n++] = 'H';
        if ((attrib & 0x04) != 0) s[n++] = 'S';
        if ((attrib & 0x10) != 0) s[n++] = 'D';
        if ((attrib & 0x20) != 0) s[n++] = 'A';
        if ((attrib & 0x400) != 0) s[n++] = 'L';
        if ((attrib & 0x800) != 0) s[n++] = 'C';
        if ((attrib & 0x4000) != 0) s[n++] = 'E';
        return n == 0 ? "—" : new string(s[..n]);
    }

    public static string FormatPosixMode(uint mode)
    {
        Span<char> s = stackalloc char[10];
        s[0] = (mode & 0xF000) switch { 0x4000 => 'd', 0xA000 => 'l', 0x2000 => 'c', 0x6000 => 'b', 0x1000 => 'p', 0xC000 => 's', _ => '-' };
        const string rwx = "rwxrwxrwx";
        for (int i = 0; i < 9; i++)
            s[i + 1] = (mode & (1u << (8 - i))) != 0 ? rwx[i] : '-';
        return new string(s);
    }

    public static string FormatValue(uint propId, in PropVariant value)
    {
        switch (propId)
        {
            case PropId.Size or PropId.PackSize or PropId.PhySize or PropId.HeadersSize or PropId.TotalSize
                or PropId.FreeSpace or PropId.ClusterSize or PropId.UnpackSize or PropId.TotalPhySize
                or PropId.DictionarySize or PropId.TailSize or PropId.EmbeddedStubSize or PropId.VirtualSize
                or PropId.AltStreamsSize when value.AsUInt64() is { } size:
                return FormatSizeExact(size);
            case PropId.Crc when value.AsUInt32() is { } crc:
                return crc.ToString("X8", CultureInfo.InvariantCulture);
            case PropId.Attrib when value.AsUInt32() is { } a:
                return FormatAttributes(a);
            case PropId.PosixAttrib when value.AsUInt32() is { } m:
                return FormatPosixMode(m);
            case PropId.Va or PropId.Offset or PropId.Position when value.AsUInt64() is { } hex:
                return "0x" + hex.ToString("X", CultureInfo.InvariantCulture);
            case PropId.ErrorFlags or PropId.WarningFlags when value.AsUInt32() is { } flags:
                return flags == 0 ? "None" : string.Join(", ", ArchiveInfo.DescribeErrorFlags(flags));
        }

        return value.VarType switch
        {
            PropVariant.VT_EMPTY => "",
            PropVariant.VT_BOOL => value.Bool != 0 ? "Yes" : "No",
            PropVariant.VT_BSTR => value.AsString() ?? "",
            PropVariant.VT_FILETIME => value.AsDateTime()?.ToLocalTime().ToString("G", CultureInfo.CurrentCulture) ?? "",
            PropVariant.VT_UI1 or PropVariant.VT_UI2 or PropVariant.VT_UI4 or PropVariant.VT_UI8 => value.AsUInt64()!.Value.ToString("N0", CultureInfo.CurrentCulture),
            PropVariant.VT_I2 or PropVariant.VT_I4 or PropVariant.VT_I8 => ((long)value.AsUInt64()!.Value).ToString("N0", CultureInfo.CurrentCulture),
            _ => $"(type {value.VarType})",
        };
    }
}
