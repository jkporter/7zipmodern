using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Windows.AppNotifications;
using Microsoft.Windows.AppNotifications.Builder;
using SevenZip.App.Interop;
using Windows.ApplicationModel.DataTransfer;
using Windows.Security.Credentials.UI;
using Windows.Storage;
using Windows.UI.StartScreen;

namespace SevenZip.App.Services;

/// <summary>Progress in the taskbar button (green/yellow/red bar), driven by the busiest operation.</summary>
public sealed class TaskbarProgress
{
    private const int TBPF_NOPROGRESS = 0, TBPF_INDETERMINATE = 1, TBPF_NORMAL = 2, TBPF_ERROR = 4, TBPF_PAUSED = 8;
    private ITaskbarList3? _taskbar;

    private ITaskbarList3? Taskbar
    {
        get
        {
            if (_taskbar is null)
            {
                try
                {
                    _taskbar = Win32.CreateComObject<ITaskbarList3>(new Guid("56FDF344-FD6D-11d0-958A-006097C9A090"));
                    _taskbar.HrInit();
                }
                catch { }
            }
            return _taskbar;
        }
    }

    public void Set(nint hwnd, double? fraction, bool paused = false, bool error = false)
    {
        var tb = Taskbar;
        if (tb is null || hwnd == 0)
            return;
        if (fraction is null)
        {
            tb.SetProgressState(hwnd, TBPF_NOPROGRESS);
            return;
        }
        tb.SetProgressState(hwnd, error ? TBPF_ERROR : paused ? TBPF_PAUSED : fraction < 0 ? TBPF_INDETERMINATE : TBPF_NORMAL);
        if (fraction >= 0)
            tb.SetProgressValue(hwnd, (ulong)(fraction.Value * 10000), 10000);
    }
}

/// <summary>
/// Windows 11 "Efficiency mode": EcoQoS + idle priority so long background compressions stay off the
/// performance cores and keep the PC responsive (replaces 7-Zip's old "Background" priority button).
/// </summary>
public static unsafe class EfficiencyMode
{
    private static int s_users;

    public static void Enter()
    {
        if (Interlocked.Increment(ref s_users) == 1)
            Apply(true);
    }

    public static void Leave()
    {
        if (Interlocked.Decrement(ref s_users) == 0)
            Apply(false);
    }

    private static void Apply(bool on)
    {
        var state = new Win32.PROCESS_POWER_THROTTLING_STATE
        {
            Version = 1,
            ControlMask = Win32.PROCESS_POWER_THROTTLING_EXECUTION_SPEED,
            StateMask = on ? Win32.PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0,
        };
        var process = Win32.GetCurrentProcess();
        Win32.SetProcessInformation(process, Win32.ProcessPowerThrottling, &state, (uint)sizeof(Win32.PROCESS_POWER_THROTTLING_STATE));
        Win32.SetPriorityClass(process, on ? Win32.IDLE_PRIORITY_CLASS : Win32.NORMAL_PRIORITY_CLASS);
    }
}

/// <summary>Completion toasts with actions ("Open folder"), via Windows App SDK app notifications.</summary>
public static class Notifications
{
    private static bool s_registered;

    public static void Register(Action<IDictionary<string, string>> onInvoked)
    {
        try
        {
            AppNotificationManager.Default.NotificationInvoked += (_, e) => onInvoked(e.Arguments);
            AppNotificationManager.Default.Register();
            s_registered = true;
        }
        catch
        {
            s_registered = false;
        }
    }

    public static void Unregister()
    {
        if (s_registered)
        {
            try { AppNotificationManager.Default.Unregister(); } catch { }
        }
    }

    public static void ShowCompleted(string title, string body, string? revealPath, bool failed = false)
    {
        if (!s_registered)
            return;
        try
        {
            var builder = new AppNotificationBuilder()
                .AddText(title)
                .AddText(body);
            if (revealPath is not null && !failed)
            {
                builder.AddButton(new AppNotificationButton("Show in folder")
                    .AddArgument("action", "reveal").AddArgument("path", revealPath));
            }
            AppNotificationManager.Default.Show(builder.BuildNotification());
        }
        catch
        {
            // Notifications are best-effort (e.g. disabled by Focus).
        }
    }
}

/// <summary>Windows Share sheet (replaces 7-Zip's MAPI "Compress and email").</summary>
public static class ShareService
{
    public static void ShareFiles(nint hwnd, IReadOnlyList<string> paths, string title)
    {
        var manager = DataTransferManagerInterop.GetForWindow(hwnd);
        void Handler(DataTransferManager sender, DataRequestedEventArgs args)
        {
            sender.DataRequested -= Handler;
            var deferral = args.Request.GetDeferral();
            args.Request.Data.Properties.Title = title;
            args.Request.Data.Properties.Description = paths.Count == 1 ? Path.GetFileName(paths[0]) : $"{paths.Count} items";
            _ = FillAsync();

            async Task FillAsync()
            {
                try
                {
                    var items = new List<IStorageItem>();
                    foreach (var p in paths)
                        items.Add(Directory.Exists(p) ? await StorageFolder.GetFolderFromPathAsync(p) : await StorageFile.GetFileFromPathAsync(p));
                    args.Request.Data.SetStorageItems(items);
                }
                finally
                {
                    deferral.Complete();
                }
            }
        }
        manager.DataRequested += Handler;
        DataTransferManagerInterop.ShowShareUIForWindow(hwnd);
    }
}

/// <summary>
/// Optional "remember password": stored in Windows Credential Manager, released only after Windows Hello
/// (face, fingerprint or PIN) verification when Hello is set up.
/// </summary>
public static class PasswordStore
{
    private const string Prefix = "7-Zip/archive/";

    private static string KeyFor(string archivePath)
    {
        var hash = SHA256.HashData(Encoding.UTF8.GetBytes(Path.GetFullPath(archivePath).ToUpperInvariant()));
        return Prefix + Convert.ToHexString(hash, 0, 16);
    }

    public static unsafe bool Has(string archivePath)
    {
        Win32.CREDENTIALW* cred;
        if (!Win32.CredRead(KeyFor(archivePath), Win32.CRED_TYPE_GENERIC, 0, &cred))
            return false;
        Win32.CredFree(cred);
        return true;
    }

    public static unsafe void Save(string archivePath, string password)
    {
        var target = KeyFor(archivePath);
        var blob = Encoding.Unicode.GetBytes(password);
        var comment = Path.GetFileName(archivePath);
        fixed (char* t = target)
        fixed (char* c = comment)
        fixed (byte* b = blob)
        {
            var cred = new Win32.CREDENTIALW
            {
                Type = Win32.CRED_TYPE_GENERIC,
                TargetName = t,
                Comment = c,
                CredentialBlob = b,
                CredentialBlobSize = (uint)blob.Length,
                Persist = Win32.CRED_PERSIST_LOCAL_MACHINE,
            };
            Win32.CredWrite(&cred, 0);
        }
        CryptographicOperations.ZeroMemory(blob);
    }

    public static async Task<string?> TryUnlockAsync(nint hwnd, string archivePath)
    {
        if (!Has(archivePath))
            return null;
        try
        {
            var availability = await UserConsentVerifier.CheckAvailabilityAsync();
            if (availability == UserConsentVerifierAvailability.Available)
            {
                var result = await UserConsentVerifierInterop.RequestVerificationForWindowAsync(hwnd, $"Unlock the saved password for {Path.GetFileName(archivePath)}");
                if (result != UserConsentVerificationResult.Verified)
                    return null;
            }
        }
        catch
        {
            return null;
        }
        return ReadSecret(archivePath);
    }

    private static unsafe string? ReadSecret(string archivePath)
    {
        Win32.CREDENTIALW* cred;
        if (!Win32.CredRead(KeyFor(archivePath), Win32.CRED_TYPE_GENERIC, 0, &cred))
            return null;
        try
        {
            return new string((char*)cred->CredentialBlob, 0, (int)cred->CredentialBlobSize / 2);
        }
        finally
        {
            Win32.CredFree(cred);
        }
    }

    public static void Forget(string archivePath) => Win32.CredDelete(KeyFor(archivePath), Win32.CRED_TYPE_GENERIC, 0);

    public static unsafe int ForgetAll()
    {
        uint count;
        Win32.CREDENTIALW** creds;
        if (!Win32.CredEnumerate(Prefix + "*", 0, &count, &creds))
            return 0;
        var targets = new List<string>();
        for (int i = 0; i < count; i++)
            targets.Add(new string(creds[i]->TargetName));
        Win32.CredFree(creds);
        foreach (var t in targets)
            Win32.CredDelete(t, Win32.CRED_TYPE_GENERIC, 0);
        return targets.Count;
    }
}

/// <summary>Taskbar/Start jump list: tasks plus the system "Recent" group.</summary>
public static class JumpListService
{
    public static async Task UpdateAsync()
    {
        if (!JumpList.IsSupported() || !ShellActions.IsPackaged)
            return;
        try
        {
            var list = await JumpList.LoadCurrentAsync();
            list.SystemGroupKind = JumpListSystemGroupKind.Recent;
            list.Items.Clear();
            var create = JumpListItem.CreateWithArguments("compress", "Create archive");
            create.Logo = new Uri("ms-appx:///Assets/JumpCompress.png");
            var hash = JumpListItem.CreateWithArguments("hash", "Verify checksums");
            hash.Logo = new Uri("ms-appx:///Assets/JumpHash.png");
            list.Items.Add(create);
            list.Items.Add(hash);
            await list.SaveAsync();
        }
        catch
        {
        }
    }
}

/// <summary>Per-session scratch space for "open file from archive", drag-out and nested archives.</summary>
public static class TempFiles
{
    private static readonly string s_root = Path.Combine(Path.GetTempPath(), "7-Zip WinUI");
    private static readonly string s_session = Path.Combine(s_root, $"{Environment.ProcessId:x}-{Guid.NewGuid():N}"[..20]);

    public static string NewFolder(string? hint = null)
    {
        var dir = Path.Combine(s_session, $"{Guid.NewGuid():N}"[..8], hint ?? "");
        Directory.CreateDirectory(dir);
        return dir;
    }

    public static long MeasureAll()
    {
        if (!Directory.Exists(s_root))
            return 0;
        try
        {
            return new DirectoryInfo(s_root).EnumerateFiles("*", SearchOption.AllDirectories).Sum(f => f.Length);
        }
        catch
        {
            return 0;
        }
    }

    /// <summary>Removes this session's files and leftovers from sessions that crashed.</summary>
    public static void CleanUp(bool everything = false)
    {
        if (!Directory.Exists(s_root))
            return;
        foreach (var dir in Directory.EnumerateDirectories(s_root))
        {
            bool mine = string.Equals(dir, s_session, StringComparison.OrdinalIgnoreCase);
            bool stale = Directory.GetLastWriteTimeUtc(dir) < DateTime.UtcNow.AddDays(-1);
            if (mine || stale || everything)
            {
                try { Directory.Delete(dir, recursive: true); } catch { }
            }
        }
    }
}
