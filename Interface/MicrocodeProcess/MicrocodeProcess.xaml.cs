﻿using Interface.Framework;
using System;
using System.Windows;
using System.Windows.Controls.Primitives;

namespace Interface.Views {
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MicrocodeProcess : Window {

        private SubWindow<Log> Log = new SubWindow<Log>();
        private SubWindow<MemoryViewer> MemoryViewer = new SubWindow<MemoryViewer>();

        public MicrocodeProcess() {
            InitializeComponent();
            ((ViewModels.MicrocodeProcess)DataContext).ShowLogEvent += ShowLog;
            ((ViewModels.MicrocodeProcess)DataContext).ShowMemoryViewerEvent += ShowMemoryViewer;

            Log.DataContext(Globals.Log);
            MemoryViewer.DataContext(new ViewModels.MemoryViewer(((ViewModels.MicrocodeProcess)DataContext).VM));
        }

        private void ShowLog(object sender, EventArgs e) {
            Log.Open();
        }
        private void ShowMemoryViewer(object sender, EventArgs e) {
            MemoryViewer.Open();
        }

        // run when the main window's close button is clicked
        private void WindowClosed(object sender, EventArgs e) {
            // closes all windows in the application
            // without this, the log would remain open, with the main window closed.
            Application.Current.Shutdown();
        }

        private void ToggleButton_Click(object sender, RoutedEventArgs e) {
            if(((ToggleButton)sender).IsChecked == true) {
                ((ToggleButton)sender).Content = "Pause";
            } else {
                ((ToggleButton)sender).Content = "Play";
            }
        }
    }
}
