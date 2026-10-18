using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.Windows.AppLifecycle;
using SevenZip.App.Activation;

namespace SevenZip.App;

/// <summary>
/// Custom entry point. Interactive requests (open, compress, hash…) are funnelled into one main window
/// with tabs; quick shell verbs (extract here, test, add to .zip…) run in their own process with a
/// compact progress window, so several can run side by side like the classic 7zG.
/// </summary>
public static class Program
{
    private const string MainInstanceKey = "7-Zip.Main";

    [STAThread]
    public static int Main(string[] args)
    {
        WinRT.ComWrappersSupport.InitializeComWrappers();

        var activation = AppInstance.GetCurrent().GetActivatedEventArgs();
        LaunchRequest request;
        try
        {
            request = LaunchRequest.FromActivation(activation);
        }
        catch
        {
            request = new LaunchRequest();
        }

        if (!request.RunsInOwnWindow)
        {
            var main = AppInstance.FindOrRegisterForKey(MainInstanceKey);
            if (!main.IsCurrent)
            {
                RedirectTo(main, activation);
                return 0;
            }
            main.Activated += OnRedirectedActivation;
        }

        Application.Start(p =>
        {
            var context = new DispatcherQueueSynchronizationContext(DispatcherQueue.GetForCurrentThread());
            SynchronizationContext.SetSynchronizationContext(context);
            _ = new App(request);
        });
        return 0;
    }

    private static void OnRedirectedActivation(object? sender, AppActivationArguments args)
    {
        var request = LaunchRequest.FromActivation(args);
        App.Current?.Dispatcher.TryEnqueue(() => App.Current.Handle(request));
    }

    /// <summary>Hands the activation to the running main instance (must not block the STA thread).</summary>
    private static void RedirectTo(AppInstance target, AppActivationArguments args)
    {
        var done = new ManualResetEventSlim();
        Task.Run(async () =>
        {
            try
            {
                await target.RedirectActivationToAsync(args);
            }
            finally
            {
                done.Set();
            }
        });
        done.Wait(TimeSpan.FromSeconds(10));
    }
}
