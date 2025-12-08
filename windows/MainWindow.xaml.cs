// FAME Smart Flasher - C# Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

using System.Windows;
using FAMESmartFlasher.ViewModels;
using ModernWpf;

namespace FAMESmartFlasher;

/// <summary>
/// Interaction logic for MainWindow.xaml
/// </summary>
public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        DataContext = new MainViewModel();

        // Apply ModernWPF theme
        ThemeManager.Current.ApplicationTheme = ApplicationTheme.Light;

        Closing += (s, e) =>
        {
            if (DataContext is MainViewModel vm)
            {
                vm.Cleanup();
            }
        };
    }
}
