using System.Collections.ObjectModel;
using SevenZip.App.ViewModels;
using SevenZip.Core.Operations;

namespace SevenZip.App.Services;

/// <summary>
/// Runs engine work off the UI thread (one dedicated thread per job, as the engine's callbacks are
/// synchronous) and mirrors progress to the taskbar and, when the app isn't in front, to a toast.
/// Several jobs can run at once; nothing is modal.
/// </summary>
public sealed class OperationService
{
    private readonly TaskbarProgress _taskbar = new();
    private readonly Func<nint> _hwnd;
    private readonly Func<bool> _isForeground;

    public OperationService(Func<nint> hwnd, Func<bool> isForeground)
    {
        _hwnd = hwnd;
        _isForeground = isForeground;
    }

    public ObservableCollection<OperationViewModel> Operations { get; } = [];

    public bool HasActive => Operations.Any(o => o.IsActive);

    public async Task<T?> RunAsync<T>(OperationViewModel op, Func<OperationContext, T> work, Func<T, string> describe, Func<T, string?>? resultPath = null)
        where T : class
    {
        Operations.Insert(0, op);
        op.ProgressChanged += OnProgress;
        op.Start();
        UpdateTaskbar();
        try
        {
            var result = await Task.Factory.StartNew(() => work(op.Context), CancellationToken.None, TaskCreationOptions.LongRunning, TaskScheduler.Default);
            op.ResultPath = resultPath?.Invoke(result);
            op.Complete(OperationState.Succeeded, describe(result));
            Notify(op);
            return result;
        }
        catch (OperationCanceledException)
        {
            op.Complete(OperationState.Cancelled, "Cancelled");
            return null;
        }
        catch (Exception ex)
        {
            op.Complete(OperationState.Failed, ex.Message);
            Notify(op);
            throw;
        }
        finally
        {
            op.ProgressChanged -= OnProgress;
            UpdateTaskbar();
            // Finished jobs stay listed briefly so the user can see the outcome.
            _ = RemoveLaterAsync(op);
        }
    }

    private async Task RemoveLaterAsync(OperationViewModel op)
    {
        await Task.Delay(op.State == OperationState.Succeeded ? TimeSpan.FromSeconds(20) : TimeSpan.FromMinutes(10));
        Operations.Remove(op);
    }

    public void Dismiss(OperationViewModel op)
    {
        if (op.IsFinished)
            Operations.Remove(op);
    }

    private void OnProgress(object? sender, EventArgs e) => UpdateTaskbar();

    private void UpdateTaskbar()
    {
        var active = Operations.Where(o => o.IsActive).ToList();
        if (active.Count == 0)
        {
            _taskbar.Set(_hwnd(), null);
            return;
        }
        double fraction = active.Any(o => o.IsIndeterminate) ? -1 : active.Average(o => o.Percent / 100);
        _taskbar.Set(_hwnd(), fraction, paused: active.All(o => o.IsPaused));
    }

    private void Notify(OperationViewModel op)
    {
        if (_isForeground() || !App.Current.Settings.Current.NotifyOnCompletion)
            return;
        Notifications.ShowCompleted(op.Title, op.Summary, op.ResultPath, op.State == OperationState.Failed);
    }
}
