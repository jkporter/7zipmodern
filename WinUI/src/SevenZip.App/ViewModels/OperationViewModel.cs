using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using SevenZip.App.Services;
using SevenZip.Core.Archives;
using SevenZip.Core.Operations;

namespace SevenZip.App.ViewModels;

public enum OperationState { Running, Paused, Succeeded, CompletedWithErrors, Failed, Cancelled }

/// <summary>One running engine job, shown in the Activity flyout or a compact progress window.</summary>
public sealed partial class OperationViewModel : ObservableObject
{
    private readonly CancellationTokenSource _cts = new();
    private bool _efficiency;

    public OperationViewModel(OperationKind kind, string title, IOperationPrompts? prompts)
    {
        Kind = kind;
        Title = title;
        Context = new OperationContext(_cts.Token, new Progress<OperationProgress>(OnProgress), prompts);
    }

    public OperationKind Kind { get; }
    public OperationContext Context { get; }
    public ObservableCollection<OperationMessage> Messages { get; } = [];

    [ObservableProperty] public partial string Title { get; set; }
    [ObservableProperty] public partial string Detail { get; set; } = "Preparing…";
    [ObservableProperty] public partial double Percent { get; set; }
    [ObservableProperty] public partial bool IsIndeterminate { get; set; } = true;
    [ObservableProperty] public partial string Stats { get; set; } = "";
    [ObservableProperty] public partial string Summary { get; set; } = "";
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsActive), nameof(IsFinished), nameof(IsPaused), nameof(PauseGlyph), nameof(PauseLabel))]
    public partial OperationState State { get; set; } = OperationState.Running;
    /// <summary>Destination or output file to reveal when done.</summary>
    [ObservableProperty] public partial string? ResultPath { get; set; }

    public bool IsActive => State is OperationState.Running or OperationState.Paused;
    public bool IsFinished => !IsActive;
    public bool IsPaused => State == OperationState.Paused;
    public string PauseGlyph => IsPaused ? "" : "";
    public string PauseLabel => IsPaused ? "Resume" : "Pause";

    public bool EfficiencyMode
    {
        get => _efficiency;
        set
        {
            if (_efficiency == value)
                return;
            _efficiency = value;
            if (IsActive)
            {
                if (value) Services.EfficiencyMode.Enter();
                else Services.EfficiencyMode.Leave();
            }
            OnPropertyChanged();
        }
    }

    public event EventHandler? Finished;

    [RelayCommand]
    private void TogglePause()
    {
        if (State == OperationState.Running)
        {
            Context.Pause();
            State = OperationState.Paused;
        }
        else if (State == OperationState.Paused)
        {
            Context.Resume();
            State = OperationState.Running;
        }
    }

    [RelayCommand]
    private void Cancel()
    {
        Context.Resume();
        _cts.Cancel();
    }

    [RelayCommand]
    private void Reveal()
    {
        if (ResultPath is null)
            return;
        if (Directory.Exists(ResultPath) && Kind is OperationKind.Extract)
            ShellActions.OpenFolder(ResultPath);
        else
            ShellActions.RevealInExplorer([ResultPath]);
    }

    private void OnProgress(OperationProgress p)
    {
        if (p.TotalBytes > 0)
        {
            IsIndeterminate = false;
            Percent = p.Fraction * 100;
        }
        Detail = p.CurrentItem is { Length: > 0 } item ? item : Detail;

        var parts = new List<string>();
        if (p.TotalBytes > 0)
            parts.Add($"{PropertyFormatter.FormatSize(p.CompletedBytes)} of {PropertyFormatter.FormatSize(p.TotalBytes)}");
        if (p.BytesPerSecond > 0)
            parts.Add($"{PropertyFormatter.FormatSize((ulong)p.BytesPerSecond)}/s");
        if (p.Remaining is { } r)
            parts.Add(r.TotalHours >= 1 ? $"{(int)r.TotalHours}h {r.Minutes}m left" : r.TotalMinutes >= 1 ? $"{(int)r.TotalMinutes} min {r.Seconds}s left" : $"{Math.Max(1, r.Seconds)} s left");
        if (Kind is OperationKind.Compress or OperationKind.Update && p.Ratio is { } ratio)
            parts.Add($"ratio {ratio:P0}");
        if (p.TotalFiles > 1)
            parts.Add($"{p.CompletedFiles:N0} / {p.TotalFiles:N0} files");
        Stats = string.Join(" · ", parts);
        ProgressChanged?.Invoke(this, EventArgs.Empty);
    }

    public event EventHandler? ProgressChanged;

    internal void Start()
    {
        if (_efficiency)
            Services.EfficiencyMode.Enter();
    }

    internal void Complete(OperationState state, string summary)
    {
        if (_efficiency && IsActive)
            Services.EfficiencyMode.Leave();
        foreach (var m in Context.Messages)
            Messages.Add(m);
        if (state == OperationState.Succeeded && Context.HasErrors)
            state = OperationState.CompletedWithErrors;
        Summary = summary;
        IsIndeterminate = false;
        Percent = state is OperationState.Succeeded or OperationState.CompletedWithErrors ? 100 : Percent;
        Detail = summary;
        State = state;
        Finished?.Invoke(this, EventArgs.Empty);
    }
}
