using System.Diagnostics;
using System.Runtime.InteropServices.WindowsRuntime;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using SevenZip.App.Interop;

namespace SevenZip.App.Services;

/// <summary>Real Explorer icons and type names for items inside archives (looked up by extension, cached).</summary>
public static unsafe class ShellIcons
{
    private static readonly Dictionary<string, ImageSource?> s_icons = new(StringComparer.OrdinalIgnoreCase);
    private static readonly Dictionary<string, string> s_types = new(StringComparer.OrdinalIgnoreCase);
    private const string FolderKey = "<folder>";

    public static ImageSource? GetFolderIcon() => GetIconCore(FolderKey, "folder", Win32.FILE_ATTRIBUTE_DIRECTORY);

    public static ImageSource? GetIcon(string fileName)
    {
        var ext = Path.GetExtension(fileName);
        // Executables and shortcuts carry their own icons; use the generic one for the extension.
        return GetIconCore(ext.Length == 0 ? "<none>" : ext, "file" + ext, Win32.FILE_ATTRIBUTE_NORMAL);
    }

    /// <summary>Icon of a real file on disk (archives in the recent list).</summary>
    public static ImageSource? GetIconForPath(string path) => GetIcon(path);

    public static string GetTypeName(string fileName, bool isFolder)
    {
        var key = isFolder ? FolderKey : Path.GetExtension(fileName);
        if (s_types.TryGetValue(key, out var cached))
            return cached;
        Win32.SHFILEINFOW info;
        string name = "";
        if (Win32.SHGetFileInfo(isFolder ? "folder" : "file" + key, isFolder ? Win32.FILE_ATTRIBUTE_DIRECTORY : Win32.FILE_ATTRIBUTE_NORMAL,
                &info, (uint)sizeof(Win32.SHFILEINFOW), Win32.SHGFI_TYPENAME | Win32.SHGFI_USEFILEATTRIBUTES) != 0)
            name = new string(info.szTypeName);
        if (string.IsNullOrEmpty(name))
            name = isFolder ? "File folder" : key.Length > 1 ? key[1..].ToUpperInvariant() + " File" : "File";
        s_types[key] = name;
        return name;
    }

    private static ImageSource? GetIconCore(string key, string probe, uint attributes)
    {
        if (s_icons.TryGetValue(key, out var cached))
            return cached;
        ImageSource? result = null;
        try
        {
            Win32.SHFILEINFOW info;
            if (Win32.SHGetFileInfo(probe, attributes, &info, (uint)sizeof(Win32.SHFILEINFOW),
                    Win32.SHGFI_ICON | Win32.SHGFI_LARGEICON | Win32.SHGFI_USEFILEATTRIBUTES) != 0 && info.hIcon != 0)
            {
                result = ToBitmap(info.hIcon);
                Win32.DestroyIcon(info.hIcon);
            }
        }
        catch
        {
            result = null;
        }
        s_icons[key] = result;
        return result;
    }

    private static WriteableBitmap? ToBitmap(nint hIcon)
    {
        Win32.ICONINFO ii;
        if (!Win32.GetIconInfo(hIcon, &ii))
            return null;
        try
        {
            if (ii.hbmColor == 0)
                return null;
            Win32.BITMAP bm;
            Win32.GetObject(ii.hbmColor, sizeof(Win32.BITMAP), &bm);
            int w = bm.bmWidth, h = bm.bmHeight;
            var pixels = new byte[w * h * 4];
            var bih = new Win32.BITMAPINFOHEADER
            {
                biSize = (uint)sizeof(Win32.BITMAPINFOHEADER),
                biWidth = w,
                biHeight = -h,
                biPlanes = 1,
                biBitCount = 32,
            };
            nint dc = Win32.CreateCompatibleDC(0);
            fixed (byte* p = pixels)
                Win32.GetDIBits(dc, ii.hbmColor, 0, (uint)h, p, &bih, 0);

            bool hasAlpha = false;
            for (int i = 3; i < pixels.Length; i += 4)
                if (pixels[i] != 0) { hasAlpha = true; break; }

            if (!hasAlpha && ii.hbmMask != 0)
            {
                var mask = new byte[w * h * 4];
                fixed (byte* m = mask)
                    Win32.GetDIBits(dc, ii.hbmMask, 0, (uint)h, m, &bih, 0);
                for (int i = 0; i < pixels.Length; i += 4)
                    pixels[i + 3] = mask[i] == 0 ? (byte)255 : (byte)0;
            }
            Win32.DeleteDC(dc);

            // WriteableBitmap expects premultiplied BGRA.
            for (int i = 0; i < pixels.Length; i += 4)
            {
                int a = pixels[i + 3];
                if (a == 255) continue;
                pixels[i] = (byte)(pixels[i] * a / 255);
                pixels[i + 1] = (byte)(pixels[i + 1] * a / 255);
                pixels[i + 2] = (byte)(pixels[i + 2] * a / 255);
            }

            var bitmap = new WriteableBitmap(w, h);
            using (var stream = bitmap.PixelBuffer.AsStream())
                stream.Write(pixels, 0, pixels.Length);
            bitmap.Invalidate();
            return bitmap;
        }
        finally
        {
            if (ii.hbmColor != 0) Win32.DeleteObject(ii.hbmColor);
            if (ii.hbmMask != 0) Win32.DeleteObject(ii.hbmMask);
        }
    }
}

public static unsafe class ShellActions
{
    /// <summary>Opens File Explorer with the items selected (instead of just opening the folder).</summary>
    public static void RevealInExplorer(IReadOnlyList<string> paths)
    {
        if (paths.Count == 0)
            return;
        var folder = Path.GetDirectoryName(paths[0]);
        if (folder is null)
            return;
        nint folderPidl = Win32.ILCreateFromPath(folder);
        var items = paths.Where(p => string.Equals(Path.GetDirectoryName(p), folder, StringComparison.OrdinalIgnoreCase))
                         .Select(Win32.ILCreateFromPath).Where(p => p != 0).ToArray();
        try
        {
            if (folderPidl == 0)
                return;
            fixed (nint* pItems = items)
                Win32.SHOpenFolderAndSelectItems(folderPidl, (uint)items.Length, pItems, 0);
        }
        finally
        {
            Win32.ILFree(folderPidl);
            foreach (var p in items)
                Win32.ILFree(p);
        }
    }

    public static void OpenFolder(string folder)
        => Process.Start(new ProcessStartInfo { FileName = folder, UseShellExecute = true });

    public static void Launch(string path)
        => Process.Start(new ProcessStartInfo { FileName = path, UseShellExecute = true, WorkingDirectory = Path.GetDirectoryName(path) });

    /// <summary>The system "Open with" picker.</summary>
    public static void OpenWith(nint hwnd, string path)
    {
        fixed (char* p = path)
        {
            var info = new Win32.OPENASINFO { pcszFile = p, oaifInFlags = Win32.OAIF_EXEC | Win32.OAIF_ALLOW_REGISTRATION };
            Win32.SHOpenWithDialog(hwnd, &info);
        }
    }

    /// <summary>Feeds Explorer's Recent list and the taskbar jump list's "Recent" category.</summary>
    public static void AddToRecentDocuments(string path)
    {
        try
        {
            fixed (char* p = path)
                Win32.SHAddToRecentDocs(Win32.SHARD_PATHW, p);
        }
        catch
        {
        }
    }

    /// <summary>Moves files to the Recycle Bin with IFileOperation (undoable, unlike a hard delete).</summary>
    public static void Recycle(IEnumerable<string> paths, nint owner)
    {
        const uint FOF_SILENT = 0x4, FOF_NOCONFIRMATION = 0x10, FOF_ALLOWUNDO = 0x40, FOF_NOERRORUI = 0x400, FOFX_RECYCLEONDELETE = 0x00080000;
        var op = Win32.CreateComObject<IFileOperation>(new Guid("3AD05575-8857-4850-9277-11B85BDB8E09"));
        op.SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOFX_RECYCLEONDELETE);
        if (owner != 0)
            op.SetOwnerWindow(owner);
        Guid iid = typeof(IShellItem).GUID;
        int count = 0;
        foreach (var path in paths)
        {
            void* ppv;
            if (Win32.SHCreateItemFromParsingName(path, 0, &iid, &ppv) != 0)
                continue;
            var item = System.Runtime.InteropServices.Marshalling.ComInterfaceMarshaller<IShellItem>.ConvertToManaged(ppv)!;
            System.Runtime.InteropServices.Marshalling.ComInterfaceMarshaller<IShellItem>.Free(ppv);
            op.DeleteItem(item, 0);
            count++;
        }
        if (count > 0)
            op.PerformOperations();
    }

    public static bool IsPackaged
    {
        get
        {
            uint len = 0;
            return Win32.GetCurrentPackageFullName(&len, null) != 15700; // APPMODEL_ERROR_NO_PACKAGE
        }
    }
}
