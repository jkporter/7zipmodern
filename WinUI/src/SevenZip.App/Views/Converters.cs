using Microsoft.UI.Xaml;
using SevenZip.App.ViewModels;

namespace SevenZip.App.Views;

/// <summary>Functions for x:Bind (compile-time checked, no IValueConverter boilerplate).</summary>
public static class Converters
{
    public static Visibility Visible(bool value) => value ? Visibility.Visible : Visibility.Collapsed;
    public static Visibility Collapsed(bool value) => value ? Visibility.Collapsed : Visibility.Visible;
    public static Visibility VisibleIfNotEmpty(string? value) => string.IsNullOrEmpty(value) ? Visibility.Collapsed : Visibility.Visible;
    public static Visibility VisibleIfNotNull(object? value) => value is null ? Visibility.Collapsed : Visibility.Visible;
    public static Visibility VisibleIfZero(int value) => value == 0 ? Visibility.Visible : Visibility.Collapsed;
    public static Visibility VisibleIfNonZero(int value) => value != 0 ? Visibility.Visible : Visibility.Collapsed;
    public static bool Not(bool value) => !value;
    public static bool HasAny(int count) => count > 0;
    public static bool IsNotEmpty(string? value) => !string.IsNullOrEmpty(value);

    public static string SortGlyph(SortColumn current, string column, bool descending)
        => current.ToString() == column ? (descending ? "" : "") : "";
}

/// <summary>Shared, responsive column widths for the archive list header and rows.</summary>
public sealed partial class ColumnLayout : CommunityToolkit.Mvvm.ComponentModel.ObservableObject
{
    [CommunityToolkit.Mvvm.ComponentModel.ObservableProperty] public partial GridLength ModifiedWidth { get; set; } = new(170);
    [CommunityToolkit.Mvvm.ComponentModel.ObservableProperty] public partial GridLength TypeWidth { get; set; } = new(160);
    [CommunityToolkit.Mvvm.ComponentModel.ObservableProperty] public partial GridLength SizeWidth { get; set; } = new(100);
    [CommunityToolkit.Mvvm.ComponentModel.ObservableProperty] public partial GridLength PackedWidth { get; set; } = new(100);

    public void Fit(double width)
    {
        ModifiedWidth = new(width < 620 ? 0 : 170);
        TypeWidth = new(width < 900 ? 0 : 160);
        PackedWidth = new(width < 760 ? 0 : 100);
        SizeWidth = new(width < 460 ? 0 : 100);
    }
}
