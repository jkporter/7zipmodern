using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using SevenZip.App.ViewModels;

namespace SevenZip.App.Views;

public sealed partial class BenchmarkPage : UserControl, ITabPage
{
    public BenchmarkPage(BenchmarkViewModel viewModel)
    {
        ViewModel = viewModel;
        InitializeComponent();
        ViewModel.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(BenchmarkViewModel.IsRunning))
                StartButton.Content = ViewModel.IsRunning ? "Stop" : "Start";
        };
    }

    public BenchmarkViewModel ViewModel { get; }
    public string TabTitle => "Benchmark";
    public IconSource TabIcon => new FontIconSource { Glyph = "" };
    public void OnTabClosed() { }

    public static string PassLabel(int pass) => $"Pass {pass}";
    public static string Speed(double mbps) => $"{mbps:N1} MB/s";
    public static string RatioLabel(double ratio) => $"{ratio:P0}";

    private async void OnStartClick(object sender, RoutedEventArgs e) => await ViewModel.StartAsync();
}
