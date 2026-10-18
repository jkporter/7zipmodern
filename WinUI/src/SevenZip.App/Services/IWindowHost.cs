using Microsoft.UI;
using Microsoft.Windows.Storage.Pickers;

namespace SevenZip.App.Services;

/// <summary>What pages need from the window that hosts them.</summary>
public interface IWindowHost
{
    nint Hwnd { get; }
    WindowId WindowId { get; }
    DialogService Dialogs { get; }
    OperationService Operations { get; }

    void OpenArchive(string path, string? format = null);
    void OpenNestedArchive(ViewModels.ArchiveViewModel parent, string extractedPath, string pathInParent);
    void OpenCompress(IReadOnlyList<string> paths, string? targetArchive = null);
    void OpenHash(IReadOnlyList<string> paths, string? algorithm = null);
    void OpenBenchmark();
    void OpenSettings();
    void UpdateTab(object page, string title);
}

/// <summary>Windows App SDK pickers (work elevated and unpackaged; return plain paths).</summary>
public static class Pickers
{
    public static async Task<IReadOnlyList<string>> PickFilesAsync(WindowId window, IEnumerable<string>? filter = null, string? title = null)
    {
        var picker = new FileOpenPicker(window) { SuggestedStartLocation = PickerLocationId.DocumentsLibrary, ViewMode = PickerViewMode.List };
        if (title is not null)
            picker.Title = title;
        if (filter is null)
            picker.FileTypeFilter.Add("*");
        else
            foreach (var f in filter)
                picker.FileTypeFilter.Add(f);
        var results = await picker.PickMultipleFilesAsync();
        return results?.Select(r => r.Path).ToList() ?? [];
    }

    public static async Task<string?> PickArchiveAsync(WindowId window)
    {
        var picker = new FileOpenPicker(window) { SuggestedStartLocation = PickerLocationId.DocumentsLibrary, ViewMode = PickerViewMode.List, Title = "Open archive" };
        picker.FileTypeChoices.Add("Archives", Core.Formats.FormatRegistry.AllExtensions.Select(e => "." + e).Take(250).ToList());
        picker.FileTypeChoices.Add("All files", ["*"]);
        var result = await picker.PickSingleFileAsync();
        return result?.Path;
    }

    public static async Task<IReadOnlyList<string>> PickFoldersAsync(WindowId window, string? title = null)
    {
        var picker = new FolderPicker(window) { SuggestedStartLocation = PickerLocationId.DocumentsLibrary };
        if (title is not null)
            picker.Title = title;
        var results = await picker.PickMultipleFoldersAsync();
        return results?.Select(r => r.Path).ToList() ?? [];
    }

    public static async Task<string?> PickFolderAsync(WindowId window, string? start = null, string? title = null)
    {
        var picker = new FolderPicker(window) { SuggestedStartLocation = PickerLocationId.Downloads };
        if (start is not null && Directory.Exists(start))
            picker.SuggestedStartFolder = start;
        if (title is not null)
            picker.Title = title;
        var result = await picker.PickSingleFolderAsync();
        return result?.Path;
    }

    public static async Task<string?> PickSaveAsync(WindowId window, string suggestedPath, string typeLabel, string extension)
    {
        var picker = new FileSavePicker(window)
        {
            SuggestedFileName = Path.GetFileName(suggestedPath),
            DefaultFileExtension = "." + extension,
        };
        var dir = Path.GetDirectoryName(suggestedPath);
        if (dir is not null && Directory.Exists(dir))
            picker.SuggestedStartFolder = dir;
        picker.FileTypeChoices.Add(typeLabel, ["." + extension]);
        var result = await picker.PickSaveFileAsync();
        return result?.Path;
    }
}
