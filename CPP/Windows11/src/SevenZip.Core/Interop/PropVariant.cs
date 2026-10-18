using System.Runtime.InteropServices;

namespace SevenZip.Core.Interop;

/// <summary>
/// Blittable PROPVARIANT. Layout matches the Windows SDK definition on 32- and 64-bit
/// (8 byte header followed by a union whose largest member is BLOB).
/// </summary>
[StructLayout(LayoutKind.Explicit)]
public struct PropVariant
{
    [FieldOffset(0)] public ushort VarType;
    [FieldOffset(2)] public ushort Reserved1;
    [FieldOffset(4)] public ushort Reserved2;
    [FieldOffset(6)] public ushort Reserved3;

    [FieldOffset(8)] public nint Pointer;
    [FieldOffset(8)] public uint UInt32;
    [FieldOffset(8)] public int Int32;
    [FieldOffset(8)] public ulong UInt64;
    [FieldOffset(8)] public long Int64;
    [FieldOffset(8)] public short Bool;
    [FieldOffset(8)] public ushort UInt16;
    [FieldOffset(8)] public byte Byte;
    [FieldOffset(8)] private BlobPadding _blob;

    [StructLayout(LayoutKind.Sequential)]
    private struct BlobPadding
    {
        public uint Size;
        public nint Data;
    }

    public const ushort VT_EMPTY = 0;
    public const ushort VT_I2 = 2;
    public const ushort VT_I4 = 3;
    public const ushort VT_BSTR = 8;
    public const ushort VT_BOOL = 11;
    public const ushort VT_UI1 = 17;
    public const ushort VT_UI2 = 18;
    public const ushort VT_UI4 = 19;
    public const ushort VT_I8 = 20;
    public const ushort VT_UI8 = 21;
    public const ushort VT_FILETIME = 64;

    public readonly bool IsEmpty => VarType == VT_EMPTY;

    public readonly string? AsString() => VarType == VT_BSTR && Pointer != 0 ? Marshal.PtrToStringBSTR(Pointer) : null;

    /// <summary>Returns the raw bytes of a VT_BSTR (7-Zip stores binary blobs such as GUIDs and signatures this way).</summary>
    public readonly byte[]? AsBinary()
    {
        if (VarType != VT_BSTR || Pointer == 0)
            return null;
        // BSTR length prefix (in bytes) lives just before the pointer.
        int len = Marshal.ReadInt32(Pointer, -4);
        var data = new byte[len];
        Marshal.Copy(Pointer, data, 0, len);
        return data;
    }

    public readonly ulong? AsUInt64() => VarType switch
    {
        VT_UI1 => Byte,
        VT_UI2 => UInt16,
        VT_UI4 => UInt32,
        VT_UI8 => UInt64,
        VT_I2 => (ulong)(long)(short)UInt16,
        VT_I4 => (ulong)(long)Int32,
        VT_I8 => (ulong)Int64,
        _ => null,
    };

    public readonly uint? AsUInt32() => AsUInt64() is { } v ? (uint)v : null;

    public readonly bool? AsBool() => VarType == VT_BOOL ? Bool != 0 : null;

    public readonly DateTime? AsDateTime()
    {
        if (VarType != VT_FILETIME || Int64 <= 0)
            return null;
        try { return DateTime.FromFileTimeUtc(Int64); }
        catch (ArgumentOutOfRangeException) { return null; }
    }

    public readonly object? ToObject() => VarType switch
    {
        VT_EMPTY => null,
        VT_BSTR => AsString(),
        VT_BOOL => AsBool(),
        VT_FILETIME => AsDateTime(),
        VT_I2 or VT_I4 or VT_I8 => (long)AsUInt64()!.Value,
        VT_UI1 or VT_UI2 or VT_UI4 or VT_UI8 => AsUInt64(),
        _ => null,
    };

    public static PropVariant FromUInt32(uint value) => new() { VarType = VT_UI4, UInt32 = value };
    public static PropVariant FromUInt64(ulong value) => new() { VarType = VT_UI8, UInt64 = value };
    public static PropVariant FromBool(bool value) => new() { VarType = VT_BOOL, Bool = value ? (short)-1 : (short)0 };

    public static PropVariant FromDateTime(DateTime? value)
    {
        if (value is not { } dt || dt.Year < 1601)
            return default;
        return new() { VarType = VT_FILETIME, Int64 = dt.ToFileTimeUtc() };
    }

    /// <summary>Allocates a BSTR; ownership transfers to the receiver (7-Zip frees it with PropVariantClear).</summary>
    public static PropVariant FromString(string? value)
    {
        if (value is null)
            return default;
        return new() { VarType = VT_BSTR, Pointer = Marshal.StringToBSTR(value) };
    }

    public static unsafe void Clear(PropVariant* value) => NativeMethods.PropVariantClear(value);
}
