using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using SevenZip.App.Services;
using SevenZip.App.ViewModels;

namespace SevenZip.App.Views;

public sealed partial class ExtractOptionsPanel : UserControl
{
    private readonly WindowId _window;

    public ExtractOptionsPanel(ExtractOptionsModel model, WindowId window)
    {
        Model = model;
        _window = window;
        InitializeComponent();
    }

    public ExtractOptionsModel Model { get; }

    public void ShowError(string message)
    {
        ErrorBar.Message = message;
        ErrorBar.IsOpen = true;
    }

    private async void OnBrowseClick(object sender, RoutedEventArgs e)
    {
        var start = Directory.Exists(Model.Destination) ? Model.Destination : Path.GetDirectoryName(Model.Destination);
        if (await Pickers.PickFolderAsync(_window, start, "Extract to") is { } folder)
        {
            Model.Destination = Model.CreateSubfolder ? Path.Combine(folder, Model.FolderName) : folder;
        }
    }
}
