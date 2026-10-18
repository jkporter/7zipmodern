using System.Runtime.InteropServices;

namespace SevenZip.Core.Interop;

internal static unsafe partial class NativeMethods
{
    [LibraryImport("ole32.dll")]
    public static partial int PropVariantClear(PropVariant* pvar);

    [LibraryImport("oleaut32.dll")]
    public static partial void SysFreeString(nint bstr);
}

/// <summary>
/// Loads 7z.dll (built unchanged from CPP/7zip/Bundles/Format7zF) and exposes its flat exports.
/// The engine is used exactly as 7-Zip's own front ends use it: through CreateObject and the
/// IInArchive / IOutArchive / IHashers interfaces.
/// </summary>
public static unsafe class SevenZipEngine
{
    private static readonly Lock s_lock = new();
    private static nint s_module;

    private static delegate* unmanaged[Stdcall]<Guid*, Guid*, void**, int> s_createObject;
    private static delegate* unmanaged[Stdcall]<uint*, int> s_getNumberOfFormats;
    private static delegate* unmanaged[Stdcall]<uint, uint, PropVariant*, int> s_getHandlerProperty2;
    private static delegate* unmanaged[Stdcall]<void**, int> s_getHashers;
    private static delegate* unmanaged[Stdcall]<uint*, int> s_getNumberOfMethods;
    private static delegate* unmanaged[Stdcall]<uint, uint, PropVariant*, int> s_getMethodProperty;
    private static delegate* unmanaged[Stdcall]<uint, PropVariant*, int> s_getModuleProp;

    public static string? LoadedPath { get; private set; }

    /// <summary>Loads the engine. Searches next to the app first, then the given path.</summary>
    public static void EnsureLoaded(string? explicitPath = null)
    {
        if (s_module != 0)
            return;
        lock (s_lock)
        {
            if (s_module != 0)
                return;

            var candidates = new List<string>();
            if (explicitPath is not null)
                candidates.Add(explicitPath);
            candidates.Add(Path.Combine(AppContext.BaseDirectory, "7z.dll"));
            candidates.Add(Path.Combine(AppContext.BaseDirectory, "Engine", "7z.dll"));

            foreach (var candidate in candidates)
            {
                if (File.Exists(candidate) && NativeLibrary.TryLoad(candidate, out var handle))
                {
                    Bind(handle);
                    LoadedPath = candidate;
                    return;
                }
            }
            throw new DllNotFoundException("The 7-Zip engine (7z.dll) was not found next to the application.");
        }
    }

    private static void Bind(nint handle)
    {
        s_createObject = (delegate* unmanaged[Stdcall]<Guid*, Guid*, void**, int>)NativeLibrary.GetExport(handle, "CreateObject");
        s_getNumberOfFormats = (delegate* unmanaged[Stdcall]<uint*, int>)NativeLibrary.GetExport(handle, "GetNumberOfFormats");
        s_getHandlerProperty2 = (delegate* unmanaged[Stdcall]<uint, uint, PropVariant*, int>)NativeLibrary.GetExport(handle, "GetHandlerProperty2");
        s_getHashers = (delegate* unmanaged[Stdcall]<void**, int>)NativeLibrary.GetExport(handle, "GetHashers");
        s_getNumberOfMethods = (delegate* unmanaged[Stdcall]<uint*, int>)NativeLibrary.GetExport(handle, "GetNumberOfMethods");
        s_getMethodProperty = (delegate* unmanaged[Stdcall]<uint, uint, PropVariant*, int>)NativeLibrary.GetExport(handle, "GetMethodProperty");
        s_getModuleProp = NativeLibrary.TryGetExport(handle, "GetModuleProp", out var p)
            ? (delegate* unmanaged[Stdcall]<uint, PropVariant*, int>)p : null;
        s_module = handle;
    }

    public static uint GetNumberOfFormats()
    {
        EnsureLoaded();
        uint n;
        Marshal.ThrowExceptionForHR(s_getNumberOfFormats(&n));
        return n;
    }

    public static PropVariant GetHandlerProperty(uint formatIndex, uint propId)
    {
        EnsureLoaded();
        PropVariant v = default;
        Marshal.ThrowExceptionForHR(s_getHandlerProperty2(formatIndex, propId, &v));
        return v;
    }

    public static uint GetNumberOfMethods()
    {
        EnsureLoaded();
        uint n;
        Marshal.ThrowExceptionForHR(s_getNumberOfMethods(&n));
        return n;
    }

    public static PropVariant GetMethodProperty(uint index, uint propId)
    {
        EnsureLoaded();
        PropVariant v = default;
        Marshal.ThrowExceptionForHR(s_getMethodProperty(index, propId, &v));
        return v;
    }

    /// <summary>Engine version as reported by GetModuleProp(kVersion): (major &lt;&lt; 16) | minor.</summary>
    public static Version? GetEngineVersion()
    {
        EnsureLoaded();
        if (s_getModuleProp == null)
            return null;
        PropVariant v = default;
        if (s_getModuleProp(1, &v) != 0)
            return null;
        var raw = v.AsUInt32();
        PropVariant.Clear(&v);
        return raw is { } r ? new Version((int)(r >> 16), (int)(r & 0xFFFF)) : null;
    }

    /// <summary>
    /// Runs the handler's quick signature probe (GetIsArc). Returns null when the handler has no probe.
    /// 0 = not this format, 1 = yes, 2 = needs more data.
    /// </summary>
    public static uint? ProbeIsArc(uint formatIndex, ReadOnlySpan<byte> header)
    {
        EnsureLoaded();
        if (!NativeLibrary.TryGetExport(s_module, "GetIsArc", out var export))
            return null;
        var getIsArc = (delegate* unmanaged[Stdcall]<uint, void**, int>)export;
        void* fn = null;
        if (getIsArc(formatIndex, &fn) != 0 || fn == null)
            return null;
        var isArc = (delegate* unmanaged[Stdcall]<byte*, nuint, uint>)fn;
        fixed (byte* p = header)
            return isArc(p, (nuint)header.Length);
    }

    public static T CreateObject<T>(Guid classId) where T : class
    {
        EnsureLoaded();
        Guid iid = typeof(T).GUID;
        void* ptr = null;
        int hr = s_createObject(&classId, &iid, &ptr);
        Marshal.ThrowExceptionForHR(hr);
        return ComOut.FromNative<T>(ptr) ?? throw new InvalidOperationException("CreateObject returned null.");
    }

    public static IHashers GetHashers()
    {
        EnsureLoaded();
        void* ptr = null;
        Marshal.ThrowExceptionForHR(s_getHashers(&ptr));
        return ComOut.FromNative<IHashers>(ptr) ?? throw new InvalidOperationException("GetHashers returned null.");
    }
}
