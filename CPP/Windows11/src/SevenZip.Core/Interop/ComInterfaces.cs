using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;

// Source-generated COM bindings for the 7-Zip engine interfaces (CPP/7zip/IStream.h, IProgress.h,
// IPassword.h, ICoder.h and Archive/IArchive.h). Vtable order must match the C++ declarations exactly.
// IIDs follow 7-Zip's scheme: 23170F69-40C1-278A-0000-00{group}00{sub}0000.
//
// Every method uses [PreserveSig]: the engine speaks raw HRESULTs and expects callbacks never to throw.

namespace SevenZip.Core.Interop;

internal static class HResults
{
    public const int S_OK = 0;
    public const int S_FALSE = 1;
    public const int E_NOTIMPL = unchecked((int)0x80004001);
    public const int E_ABORT = unchecked((int)0x80004004);
    public const int E_FAIL = unchecked((int)0x80004005);
    public const int E_INVALIDARG = unchecked((int)0x80070057);
    public const int E_OUTOFMEMORY = unchecked((int)0x8007000E);
    public const int STG_E_INVALIDFUNCTION = unchecked((int)0x80030001);
    public const int ERROR_NEGATIVE_SEEK = unchecked((int)0x80070083);

    public static int FromException(Exception ex) => ex switch
    {
        OperationCanceledException => E_ABORT,
        OutOfMemoryException => E_OUTOFMEMORY,
        _ => ex.HResult != 0 ? ex.HResult : E_FAIL,
    };
}

// ---- Streams (group 3) ----

[GeneratedComInterface, Guid("23170F69-40C1-278A-0000-000300010000")]
public unsafe partial interface ISequentialInStream
{
    [PreserveSig] int Read(byte* data, uint size, uint* processedSize);
}

[GeneratedComInterface, Guid("23170F69-40C1-278A-0000-000300020000")]
public unsafe partial interface ISequentialOutStream
{
    [PreserveSig] int Write(byte* data, uint size, uint* processedSize);
}

[GeneratedComInterface, Guid("23170F69-40C1-278A-0000-000300030000")]
public unsafe partial interface IInStream : ISequentialInStream
{
    [PreserveSig] int Seek(long offset, uint seekOrigin, ulong* newPosition);
}

[GeneratedComInterface, Guid("23170F69-40C1-278A-0000-000300040000")]
public unsafe partial interface IOutStream : ISequentialOutStream
{
    [PreserveSig] int Seek(long offset, uint seekOrigin, ulong* newPosition);
    [PreserveSig] int SetSize(ulong newSize);
}

[GeneratedComInterface, Guid("23170F69-40C1-278A-0000-000300060000")]
public unsafe partial interface IStreamGetSize
{
    [PreserveSig] int GetSize(ulong* size);
}

// ---- Progress (group 0) ----

[GeneratedComInterface, Guid("23170F69-40C1-278A-0000-000000050000")]
public unsafe partial interface IProgress
{
    [PreserveSig] int SetTotal(ulong total);
    [PreserveSig] int SetCompleted(ulong* completeValue);
}

// ---- Passwords (group 5) ----

[GeneratedComInterface, Guid("23170F69-40C1-278A-0000-000500100000")]
public unsafe partial interface ICryptoGetTextPassword
{
    [PreserveSig] int CryptoGetTextPassword(nint* password);
}

[GeneratedComInterface, Guid("23170F69-40C1-278A-0000-000500110000")]
public unsafe partial interface ICryptoGetTextPassword2
{
    [PreserveSig] int CryptoGetTextPassword2(int* passwordIsDefined, nint* password);
}

// ---- Coders / hashers (group 4) ----

[GeneratedComInterface, Guid("23170F69-40C1-278A-0000-000400C00000")]
public unsafe partial interface IHasher
{
    [PreserveSig] void Init();
    [PreserveSig] void Update(byte* data, uint size);
    [PreserveSig] void Final(byte* digest);
    [PreserveSig] uint GetDigestSize();
}

[GeneratedComInterface, Guid("23170F69-40C1-278A-0000-000400C10000")]
public unsafe partial interface IHashers
{
    [PreserveSig] uint GetNumHashers();
    [PreserveSig] int GetHasherProp(uint index, uint propID, PropVariant* value);
    [PreserveSig] int CreateHasher(uint index, out IHasher? hasher);
}

// ---- Archives (group 6) ----

[GeneratedComInterface, Guid("23170F69-40C1-278A-0000-000600030000")]
public unsafe partial interface ISetProperties
{
    [PreserveSig] int SetProperties(char** names, PropVariant* values, uint numProps);
}

[GeneratedComInterface, Guid("23170F69-40C1-278A-0000-000600100000")]
public unsafe partial interface IArchiveOpenCallback
{
    [PreserveSig] int SetTotal(ulong* files, ulong* bytes);
    [PreserveSig] int SetCompleted(ulong* files, ulong* bytes);
}

[GeneratedComInterface, Guid("23170F69-40C1-278A-0000-000600200000")]
public unsafe partial interface IArchiveExtractCallback : IProgress
{
    [PreserveSig] int GetStream(uint index, nint* outStream, int askExtractMode);
    [PreserveSig] int PrepareOperation(int askExtractMode);
    [PreserveSig] int SetOperationResult(int opRes);
}

[GeneratedComInterface, Guid("23170F69-40C1-278A-0000-000600300000")]
public unsafe partial interface IArchiveOpenVolumeCallback
{
    [PreserveSig] int GetProperty(uint propID, PropVariant* value);
    [PreserveSig] int GetStream(char* name, nint* inStream);
}

[GeneratedComInterface, Guid("23170F69-40C1-278A-0000-000600400000")]
public unsafe partial interface IInArchiveGetStream
{
    [PreserveSig] int GetStream(uint index, out ISequentialInStream? stream);
}

[GeneratedComInterface, Guid("23170F69-40C1-278A-0000-000600600000")]
public unsafe partial interface IInArchive
{
    [PreserveSig] int Open(IInStream stream, ulong* maxCheckStartPosition, IArchiveOpenCallback? openCallback);
    [PreserveSig] int Close();
    [PreserveSig] int GetNumberOfItems(uint* numItems);
    [PreserveSig] int GetProperty(uint index, uint propID, PropVariant* value);
    [PreserveSig] int Extract(uint* indices, uint numItems, int testMode, IArchiveExtractCallback extractCallback);
    [PreserveSig] int GetArchiveProperty(uint propID, PropVariant* value);
    [PreserveSig] int GetNumberOfProperties(uint* numProps);
    [PreserveSig] int GetPropertyInfo(uint index, nint* name, uint* propID, ushort* varType);
    [PreserveSig] int GetNumberOfArchiveProperties(uint* numProps);
    [PreserveSig] int GetArchivePropertyInfo(uint index, nint* name, uint* propID, ushort* varType);
}

[GeneratedComInterface, Guid("23170F69-40C1-278A-0000-000600800000")]
public unsafe partial interface IArchiveUpdateCallback : IProgress
{
    [PreserveSig] int GetUpdateItemInfo(uint index, int* newData, int* newProps, uint* indexInArchive);
    [PreserveSig] int GetProperty(uint index, uint propID, PropVariant* value);
    [PreserveSig] int GetStream(uint index, nint* inStream);
    [PreserveSig] int SetOperationResult(int operationResult);
}

[GeneratedComInterface, Guid("23170F69-40C1-278A-0000-000600820000")]
public unsafe partial interface IArchiveUpdateCallback2 : IArchiveUpdateCallback
{
    [PreserveSig] int GetVolumeSize(uint index, ulong* size);
    [PreserveSig] int GetVolumeStream(uint index, nint* volumeStream);
}

[GeneratedComInterface, Guid("23170F69-40C1-278A-0000-000600A00000")]
public unsafe partial interface IOutArchive
{
    [PreserveSig] int UpdateItems(ISequentialOutStream outStream, uint numItems, IArchiveUpdateCallback updateCallback);
    [PreserveSig] int GetFileTimeType(uint* type);
}

/// <summary>Marshalling helpers for handing managed callback objects to the engine through raw out-pointers.</summary>
internal static unsafe class ComOut
{
    public static nint ToNative<T>(T? value) where T : class
        => value is null ? 0 : (nint)ComInterfaceMarshaller<T>.ConvertToUnmanaged(value);

    public static T? FromNative<T>(void* ptr) where T : class
    {
        if (ptr == null)
            return null;
        var managed = ComInterfaceMarshaller<T>.ConvertToManaged(ptr);
        ComInterfaceMarshaller<T>.Free(ptr);
        return managed;
    }
}
