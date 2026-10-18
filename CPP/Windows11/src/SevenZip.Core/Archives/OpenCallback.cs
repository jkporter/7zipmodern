using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;
using SevenZip.Core.Interop;
using SevenZip.Core.Operations;

namespace SevenZip.Core.Archives;

/// <summary>
/// Open-time callback: progress, multi-volume stream lookup (.7z.001, .part2.rar, .z01…) and
/// password requests for archives with encrypted headers.
/// </summary>
[GeneratedComClass]
internal sealed unsafe partial class OpenCallback : IArchiveOpenCallback, IArchiveOpenVolumeCallback, ICryptoGetTextPassword
{
    private readonly OperationContext _ctx;
    private readonly string _directory;
    private readonly string _archiveName;
    private string _currentName;

    public OpenCallback(string archivePath, OperationContext ctx, string? knownPassword)
    {
        _ctx = ctx;
        _directory = Path.GetDirectoryName(Path.GetFullPath(archivePath)) ?? "";
        _archiveName = Path.GetFileName(archivePath);
        _currentName = _archiveName;
        Password = knownPassword;
    }

    public string? Password { get; private set; }
    public bool PasswordRequested { get; private set; }
    public bool PasswordCancelled { get; private set; }
    public List<InStreamWrapper> VolumeStreams { get; } = [];
    public List<string> VolumePaths { get; } = [];
    /// <summary>Set by the caller when the engine has no on-disk volumes (nested/split inner streams).</summary>
    public bool DisableVolumes { get; set; }

    public int SetTotal(ulong* files, ulong* bytes)
    {
        if (bytes != null)
            _ctx.TotalBytes = *bytes;
        if (files != null)
            _ctx.TotalFiles = *files;
        return _ctx.Checkpoint();
    }

    public int SetCompleted(ulong* files, ulong* bytes)
    {
        if (bytes != null)
            _ctx.CompletedBytes = *bytes;
        if (files != null)
            _ctx.CompletedFiles = *files;
        _ctx.Report();
        return _ctx.Checkpoint();
    }

    public int GetProperty(uint propID, PropVariant* value)
    {
        try
        {
            *value = propID == PropId.Name ? PropVariant.FromString(_currentName) : default;
            return HResults.S_OK;
        }
        catch (Exception ex)
        {
            return HResults.FromException(ex);
        }
    }

    public int GetStream(char* name, nint* inStream)
    {
        try
        {
            *inStream = 0;
            if (DisableVolumes)
                return HResults.S_FALSE;
            var volumeName = new string(name);
            // Only accept plain file names in the archive's own folder.
            if (volumeName.IndexOfAny(['\\', '/', ':']) >= 0)
                return HResults.S_FALSE;
            var full = Path.Combine(_directory, volumeName);
            if (!File.Exists(full))
                return HResults.S_FALSE;
            var fs = new FileStream(full, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 16, FileOptions.RandomAccess);
            var wrapper = new InStreamWrapper(fs);
            VolumeStreams.Add(wrapper);
            VolumePaths.Add(full);
            _currentName = volumeName;
            *inStream = ComOut.ToNative<IInStream>(wrapper);
            return HResults.S_OK;
        }
        catch (Exception ex)
        {
            return HResults.FromException(ex);
        }
    }

    public int CryptoGetTextPassword(nint* password)
    {
        try
        {
            PasswordRequested = true;
            if (Password is null)
            {
                Password = _ctx.Prompts is null
                    ? null
                    : OperationContext.Wait(_ctx.Prompts.RequestPasswordAsync(_archiveName, retry: false));
                if (Password is null)
                {
                    PasswordCancelled = true;
                    *password = 0;
                    return HResults.E_ABORT;
                }
            }
            *password = Marshal.StringToBSTR(Password);
            return HResults.S_OK;
        }
        catch (Exception ex)
        {
            return HResults.FromException(ex);
        }
    }
}
