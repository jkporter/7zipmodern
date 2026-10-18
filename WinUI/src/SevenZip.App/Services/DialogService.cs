using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using SevenZip.Core.Archives;
using SevenZip.Core.Operations;

namespace SevenZip.App.Services;

/// <summary>ContentDialog helpers bound to one window. Safe to call from any thread; dialogs are serialised.</summary>
public sealed class DialogService
{
    private readonly Func<XamlRoot?> _root;
    private readonly DispatcherQueue _dispatcher;
    private readonly SemaphoreSlim _gate = new(1, 1);

    public DialogService(Func<XamlRoot?> root, DispatcherQueue dispatcher, Func<nint> hwnd)
    {
        _root = root;
        _dispatcher = dispatcher;
        Hwnd = hwnd;
    }

    public Func<nint> Hwnd { get; }
    public DispatcherQueue Dispatcher => _dispatcher;

    public Task<T> OnUiAsync<T>(Func<Task<T>> action)
    {
        if (_dispatcher.HasThreadAccess)
            return action();
        var tcs = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);
        _dispatcher.TryEnqueue(async () =>
        {
            try { tcs.SetResult(await action()); }
            catch (Exception ex) { tcs.SetException(ex); }
        });
        return tcs.Task;
    }

    public Task<ContentDialogResult> ShowAsync(ContentDialog dialog) => OnUiAsync(async () =>
    {
        await _gate.WaitAsync();
        try
        {
            dialog.XamlRoot = _root();
            dialog.Style ??= (Style)Application.Current.Resources["DefaultContentDialogStyle"];
            if (dialog.XamlRoot?.Content is FrameworkElement fe)
                dialog.RequestedTheme = fe.ActualTheme;
            return await dialog.ShowAsync();
        }
        finally
        {
            _gate.Release();
        }
    });

    public async Task<bool> ConfirmAsync(string title, string message, string primary, bool destructive = false)
    {
        var result = await OnUiAsync(() =>
        {
            var d = new ContentDialog
            {
                Title = title,
                Content = new TextBlock { Text = message, TextWrapping = TextWrapping.Wrap },
                PrimaryButtonText = primary,
                CloseButtonText = "Cancel",
                DefaultButton = destructive ? ContentDialogButton.Close : ContentDialogButton.Primary,
            };
            return ShowAsync(d);
        });
        return result == ContentDialogResult.Primary;
    }

    public Task ShowErrorAsync(string title, string message) => OnUiAsync(async () =>
    {
        var d = new ContentDialog
        {
            Title = title,
            Content = new TextBlock { Text = message, TextWrapping = TextWrapping.Wrap, IsTextSelectionEnabled = true },
            CloseButtonText = "OK",
            DefaultButton = ContentDialogButton.Close,
        };
        await ShowAsync(d);
        return true;
    });

    public Task<string?> PromptTextAsync(string title, string label, string initial, string primary, Func<string, string?>? validate = null) => OnUiAsync(async () =>
    {
        var box = new TextBox { Header = label, Text = initial, MinWidth = 360 };
        var error = new TextBlock { Foreground = (Brush)Application.Current.Resources["SystemFillColorCriticalBrush"], Visibility = Visibility.Collapsed, Margin = new Thickness(0, 8, 0, 0), TextWrapping = TextWrapping.Wrap };
        var d = new ContentDialog
        {
            Title = title,
            Content = new StackPanel { Children = { box, error } },
            PrimaryButtonText = primary,
            CloseButtonText = "Cancel",
            DefaultButton = ContentDialogButton.Primary,
        };
        box.Loaded += (_, _) =>
        {
            box.Focus(FocusState.Programmatic);
            var dot = initial.LastIndexOf('.');
            box.Select(0, dot > 0 ? dot : initial.Length);
        };
        box.TextChanged += (_, _) =>
        {
            var msg = validate?.Invoke(box.Text);
            error.Text = msg ?? "";
            error.Visibility = msg is null ? Visibility.Collapsed : Visibility.Visible;
            d.IsPrimaryButtonEnabled = msg is null && box.Text.Trim().Length > 0;
        };
        return await ShowAsync(d) == ContentDialogResult.Primary ? box.Text.Trim() : null;
    });

    /// <summary>Lists warnings/errors after an operation (replaces the classic Messages dialog).</summary>
    public Task ShowMessagesAsync(string title, IReadOnlyList<OperationMessage> messages) => OnUiAsync(async () =>
    {
        var list = new ListView { SelectionMode = ListViewSelectionMode.None, MaxHeight = 360 };
        foreach (var m in messages.Take(500))
        {
            var glyph = m.Severity switch { MessageSeverity.Error => "", MessageSeverity.Warning => "", _ => "" };
            var brush = m.Severity switch
            {
                MessageSeverity.Error => "SystemFillColorCriticalBrush",
                MessageSeverity.Warning => "SystemFillColorCautionBrush",
                _ => "TextFillColorSecondaryBrush",
            };
            var row = new Grid { ColumnSpacing = 10, Padding = new Thickness(0, 4, 0, 4) };
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            row.ColumnDefinitions.Add(new ColumnDefinition());
            row.Children.Add(new FontIcon { Glyph = glyph, FontSize = 16, Foreground = (Brush)Application.Current.Resources[brush], VerticalAlignment = VerticalAlignment.Top });
            var text = new StackPanel();
            if (m.ItemPath is not null)
                text.Children.Add(new TextBlock { Text = m.ItemPath, Style = (Style)Application.Current.Resources["BodyStrongTextBlockStyle"], TextTrimming = TextTrimming.CharacterEllipsis });
            text.Children.Add(new TextBlock { Text = m.Text, TextWrapping = TextWrapping.Wrap, IsTextSelectionEnabled = true });
            Grid.SetColumn(text, 1);
            row.Children.Add(text);
            list.Items.Add(row);
        }
        var d = new ContentDialog { Title = title, Content = list, CloseButtonText = "Close", DefaultButton = ContentDialogButton.Close };
        await ShowAsync(d);
        return true;
    });
}

/// <summary>Engine prompts (password, overwrite) for one operation, shown as Windows 11 dialogs.</summary>
public sealed class UiPrompts(DialogService dialogs, string? archivePath) : IOperationPrompts
{
    private bool _triedVault;
    private int _asked;

    public Task<string?> RequestPasswordAsync(string archiveName, bool retry) => dialogs.OnUiAsync(async () =>
    {
        retry |= _asked++ > 0;
        if (!_triedVault && archivePath is not null && App.Current.Settings.Current.OfferSavedPasswords)
        {
            _triedVault = true;
            var saved = await PasswordStore.TryUnlockAsync(dialogs.Hwnd(), archivePath);
            if (saved is not null)
                return saved;
        }

        var box = new PasswordBox { Header = "Password", MinWidth = 340, PasswordRevealMode = PasswordRevealMode.Peek };
        var remember = new CheckBox
        {
            Content = "Remember for this archive (unlocked with Windows Hello)",
            Visibility = archivePath is null ? Visibility.Collapsed : Visibility.Visible,
        };
        var d = new ContentDialog
        {
            Title = retry ? "Wrong password — try again" : "Enter password",
            Content = new StackPanel
            {
                Spacing = 12,
                Children =
                {
                    new TextBlock { Text = $"“{archiveName}” is encrypted.", TextWrapping = TextWrapping.Wrap },
                    box,
                    remember,
                },
            },
            PrimaryButtonText = "OK",
            CloseButtonText = "Cancel",
            DefaultButton = ContentDialogButton.Primary,
        };
        box.Loaded += (_, _) => box.Focus(FocusState.Programmatic);
        if (await dialogs.ShowAsync(d) != ContentDialogResult.Primary)
            return null;
        if (remember.IsChecked == true && archivePath is not null)
            PasswordStore.Save(archivePath, box.Password);
        return box.Password;
    });

    public Task<OverwriteChoice> ResolveOverwriteAsync(OverwriteRequest request) => dialogs.OnUiAsync(async () =>
    {
        static string Describe(string label, ulong size, DateTime? modified)
            => $"{label}\n{PropertyFormatter.FormatSize(size)} · {(modified?.ToLocalTime().ToString("g") ?? "unknown date")}";

        var applyAll = new CheckBox { Content = "Do this for all conflicts" };
        var choice = new RadioButtons
        {
            SelectedIndex = request.NewModified > request.ExistingModified ? 0 : 1,
            Items =
            {
                new RadioButton { Content = Describe("Replace with the file from the archive", request.NewSize, request.NewModified) },
                new RadioButton { Content = Describe("Keep the file in the destination", (ulong)request.ExistingSize, request.ExistingModified) },
                new RadioButton { Content = "Keep both (rename the extracted file)" },
            },
        };
        var d = new ContentDialog
        {
            Title = "Replace or skip file",
            Content = new StackPanel
            {
                Spacing = 12,
                MaxWidth = 460,
                Children =
                {
                    new TextBlock { Text = $"The destination already has a file named “{Path.GetFileName(request.TargetPath)}”.", TextWrapping = TextWrapping.Wrap },
                    choice,
                    applyAll,
                },
            },
            PrimaryButtonText = "Continue",
            CloseButtonText = "Cancel extraction",
            DefaultButton = ContentDialogButton.Primary,
        };
        if (await dialogs.ShowAsync(d) != ContentDialogResult.Primary)
            return OverwriteChoice.Cancel;
        bool all = applyAll.IsChecked == true;
        return choice.SelectedIndex switch
        {
            0 => all ? OverwriteChoice.ReplaceAll : OverwriteChoice.Replace,
            1 => all ? OverwriteChoice.SkipAll : OverwriteChoice.Skip,
            _ => all ? OverwriteChoice.KeepBothAll : OverwriteChoice.KeepBoth,
        };
    });
}
