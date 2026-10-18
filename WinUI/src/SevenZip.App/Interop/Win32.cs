using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;

namespace SevenZip.App.Interop;

internal static unsafe partial class Win32
{
    // ---- shell icons / type names ----

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct SHFILEINFOW
    {
        public nint hIcon;
        public int iIcon;
        public uint dwAttributes;
        public fixed char szDisplayName[260];
        public fixed char szTypeName[80];
    }

    public const uint SHGFI_ICON = 0x100;
    public const uint SHGFI_TYPENAME = 0x400;
    public const uint SHGFI_LARGEICON = 0x0;
    public const uint SHGFI_SMALLICON = 0x1;
    public const uint SHGFI_USEFILEATTRIBUTES = 0x10;
    public const uint FILE_ATTRIBUTE_NORMAL = 0x80;
    public const uint FILE_ATTRIBUTE_DIRECTORY = 0x10;

    [LibraryImport("shell32.dll", EntryPoint = "SHGetFileInfoW", StringMarshalling = StringMarshalling.Utf16)]
    public static partial nint SHGetFileInfo(string pszPath, uint dwFileAttributes, SHFILEINFOW* psfi, uint cbFileInfo, uint uFlags);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool DestroyIcon(nint hIcon);

    [StructLayout(LayoutKind.Sequential)]
    public struct ICONINFO
    {
        public int fIcon;
        public int xHotspot;
        public int yHotspot;
        public nint hbmMask;
        public nint hbmColor;
    }

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool GetIconInfo(nint hIcon, ICONINFO* piconinfo);

    [StructLayout(LayoutKind.Sequential)]
    public struct BITMAPINFOHEADER
    {
        public uint biSize;
        public int biWidth;
        public int biHeight;
        public ushort biPlanes;
        public ushort biBitCount;
        public uint biCompression;
        public uint biSizeImage;
        public int biXPelsPerMeter;
        public int biYPelsPerMeter;
        public uint biClrUsed;
        public uint biClrImportant;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct BITMAP
    {
        public int bmType;
        public int bmWidth;
        public int bmHeight;
        public int bmWidthBytes;
        public ushort bmPlanes;
        public ushort bmBitsPixel;
        public nint bmBits;
    }

    [LibraryImport("gdi32.dll", EntryPoint = "GetObjectW")]
    public static partial int GetObject(nint h, int c, void* pv);

    [LibraryImport("gdi32.dll")]
    public static partial int GetDIBits(nint hdc, nint hbm, uint start, uint cLines, void* lpvBits, BITMAPINFOHEADER* lpbmi, uint usage);

    [LibraryImport("gdi32.dll")]
    public static partial nint CreateCompatibleDC(nint hdc);

    [LibraryImport("gdi32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool DeleteDC(nint hdc);

    [LibraryImport("gdi32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool DeleteObject(nint ho);

    // ---- shell helpers ----

    [LibraryImport("shell32.dll", EntryPoint = "SHAddToRecentDocs")]
    public static partial void SHAddToRecentDocs(uint uFlags, char* pv);
    public const uint SHARD_PATHW = 3;

    [LibraryImport("shell32.dll", EntryPoint = "ILCreateFromPathW", StringMarshalling = StringMarshalling.Utf16)]
    public static partial nint ILCreateFromPath(string path);

    [LibraryImport("shell32.dll")]
    public static partial void ILFree(nint pidl);

    [LibraryImport("shell32.dll")]
    public static partial int SHOpenFolderAndSelectItems(nint pidlFolder, uint cidl, nint* apidl, uint dwFlags);

    [StructLayout(LayoutKind.Sequential)]
    public struct OPENASINFO
    {
        public char* pcszFile;
        public char* pcszClass;
        public uint oaifInFlags;
    }
    public const uint OAIF_ALLOW_REGISTRATION = 0x1;
    public const uint OAIF_EXEC = 0x4;

    [LibraryImport("shell32.dll")]
    public static partial int SHOpenWithDialog(nint hwndParent, OPENASINFO* poainfo);

    [LibraryImport("shell32.dll", EntryPoint = "CommandLineToArgvW", StringMarshalling = StringMarshalling.Utf16)]
    public static partial char** CommandLineToArgv(string lpCmdLine, int* pNumArgs);

    [LibraryImport("kernel32.dll")]
    public static partial nint LocalFree(nint hMem);

    [LibraryImport("kernel32.dll")]
    public static partial int GetCurrentPackageFullName(uint* packageFullNameLength, char* packageFullName);

    [LibraryImport("kernel32.dll")]
    public static partial int GetCurrentPackageFamilyName(uint* length, char* name);

    // ---- window helpers ----

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool SetForegroundWindow(nint hwnd);

    [LibraryImport("user32.dll")]
    public static partial uint GetDpiForWindow(nint hwnd);

    [LibraryImport("user32.dll")]
    public static partial nint GetForegroundWindow();

    // ---- efficiency mode (EcoQoS) ----

    [StructLayout(LayoutKind.Sequential)]
    public struct PROCESS_POWER_THROTTLING_STATE
    {
        public uint Version;
        public uint ControlMask;
        public uint StateMask;
    }
    public const int ProcessPowerThrottling = 4;
    public const uint PROCESS_POWER_THROTTLING_EXECUTION_SPEED = 0x1;
    public const uint IDLE_PRIORITY_CLASS = 0x40;
    public const uint NORMAL_PRIORITY_CLASS = 0x20;

    [LibraryImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool SetProcessInformation(nint hProcess, int infoClass, void* info, uint size);

    [LibraryImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool SetPriorityClass(nint hProcess, uint priorityClass);

    [LibraryImport("kernel32.dll")]
    public static partial nint GetCurrentProcess();

    // ---- Credential Manager ----

    [StructLayout(LayoutKind.Sequential)]
    public struct CREDENTIALW
    {
        public uint Flags;
        public uint Type;
        public char* TargetName;
        public char* Comment;
        public long LastWritten;
        public uint CredentialBlobSize;
        public byte* CredentialBlob;
        public uint Persist;
        public uint AttributeCount;
        public nint Attributes;
        public char* TargetAlias;
        public char* UserName;
    }
    public const uint CRED_TYPE_GENERIC = 1;
    public const uint CRED_PERSIST_LOCAL_MACHINE = 2;

    [LibraryImport("advapi32.dll", EntryPoint = "CredWriteW", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool CredWrite(CREDENTIALW* credential, uint flags);

    [LibraryImport("advapi32.dll", EntryPoint = "CredReadW", SetLastError = true, StringMarshalling = StringMarshalling.Utf16)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool CredRead(string target, uint type, uint flags, CREDENTIALW** credential);

    [LibraryImport("advapi32.dll", EntryPoint = "CredDeleteW", SetLastError = true, StringMarshalling = StringMarshalling.Utf16)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool CredDelete(string target, uint type, uint flags);

    [LibraryImport("advapi32.dll", EntryPoint = "CredEnumerateW", SetLastError = true, StringMarshalling = StringMarshalling.Utf16)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool CredEnumerate(string filter, uint flags, uint* count, CREDENTIALW*** credentials);

    [LibraryImport("advapi32.dll")]
    public static partial void CredFree(void* buffer);

    // ---- COM activation ----

    public const uint CLSCTX_INPROC_SERVER = 0x1;

    [LibraryImport("ole32.dll")]
    public static partial int CoCreateInstance(Guid* rclsid, nint pUnkOuter, uint dwClsContext, Guid* riid, void** ppv);

    [LibraryImport("shell32.dll", StringMarshalling = StringMarshalling.Utf16)]
    public static partial int SHCreateItemFromParsingName(string pszPath, nint pbc, Guid* riid, void** ppv);

    public static T CreateComObject<T>(Guid clsid) where T : class
    {
        Guid iid = typeof(T).GUID;
        void* ppv;
        Marshal.ThrowExceptionForHR(CoCreateInstance(&clsid, 0, CLSCTX_INPROC_SERVER, &iid, &ppv));
        var obj = ComInterfaceMarshaller<T>.ConvertToManaged(ppv)!;
        ComInterfaceMarshaller<T>.Free(ppv);
        return obj;
    }

    public static string[] SplitCommandLine(string commandLine)
    {
        if (string.IsNullOrWhiteSpace(commandLine))
            return [];
        int count;
        char** argv = CommandLineToArgv(commandLine, &count);
        if (argv == null)
            return [];
        try
        {
            var result = new string[count];
            for (int i = 0; i < count; i++)
                result[i] = new string(argv[i]);
            return result;
        }
        finally
        {
            LocalFree((nint)argv);
        }
    }
}

// ---- Taskbar progress (ITaskbarList3) ----

[GeneratedComInterface, Guid("EA1AFB91-9E28-4B86-90E9-9E9F8A5EEFAF")]
internal partial interface ITaskbarList3
{
    // ITaskbarList
    [PreserveSig] int HrInit();
    [PreserveSig] int AddTab(nint hwnd);
    [PreserveSig] int DeleteTab(nint hwnd);
    [PreserveSig] int ActivateTab(nint hwnd);
    [PreserveSig] int SetActiveAlt(nint hwnd);
    // ITaskbarList2
    [PreserveSig] int MarkFullscreenWindow(nint hwnd, int fFullscreen);
    // ITaskbarList3
    [PreserveSig] int SetProgressValue(nint hwnd, ulong ullCompleted, ulong ullTotal);
    [PreserveSig] int SetProgressState(nint hwnd, int tbpFlags);
}

// ---- IFileOperation (Recycle Bin deletes) ----

[GeneratedComInterface, Guid("43826D1E-E718-42EE-BC55-A1E261C37BFE")]
internal partial interface IShellItem
{
    [PreserveSig] int BindToHandler(nint pbc, nint bhid, nint riid, nint ppv);
    [PreserveSig] int GetParent(out IShellItem? ppsi);
    [PreserveSig] int GetDisplayName(uint sigdnName, out nint ppszName);
    [PreserveSig] int GetAttributes(uint sfgaoMask, out uint psfgaoAttribs);
    [PreserveSig] int Compare(IShellItem psi, uint hint, out int piOrder);
}

[GeneratedComInterface, Guid("947AAB5F-0A5C-4C13-B4D6-4BF7836FC9F8")]
internal partial interface IFileOperation
{
    [PreserveSig] int Advise(nint pfops, out uint pdwCookie);
    [PreserveSig] int Unadvise(uint dwCookie);
    [PreserveSig] int SetOperationFlags(uint dwOperationFlags);
    [PreserveSig] int SetProgressMessage(nint pszMessage);
    [PreserveSig] int SetProgressDialog(nint popd);
    [PreserveSig] int SetProperties(nint pproparray);
    [PreserveSig] int SetOwnerWindow(nint hwndOwner);
    [PreserveSig] int ApplyPropertiesToItem(IShellItem psiItem);
    [PreserveSig] int ApplyPropertiesToItems(nint punkItems);
    [PreserveSig] int RenameItem(IShellItem psiItem, nint pszNewName, nint pfopsItem);
    [PreserveSig] int RenameItems(nint pUnkItems, nint pszNewName);
    [PreserveSig] int MoveItem(IShellItem psiItem, IShellItem psiDestinationFolder, nint pszNewName, nint pfopsItem);
    [PreserveSig] int MoveItems(nint punkItems, IShellItem psiDestinationFolder);
    [PreserveSig] int CopyItem(IShellItem psiItem, IShellItem psiDestinationFolder, nint pszCopyName, nint pfopsItem);
    [PreserveSig] int CopyItems(nint punkItems, IShellItem psiDestinationFolder);
    [PreserveSig] int DeleteItem(IShellItem psiItem, nint pfopsItem);
    [PreserveSig] int DeleteItems(nint punkItems);
    [PreserveSig] int NewItem(IShellItem psiDestinationFolder, uint dwFileAttributes, nint pszName, nint pszTemplateName, nint pfopsItem);
    [PreserveSig] int PerformOperations();
    [PreserveSig] int GetAnyOperationsAborted(out int pfAnyOperationsAborted);
}
