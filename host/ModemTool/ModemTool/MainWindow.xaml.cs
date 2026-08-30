using System.Collections.ObjectModel;
using System.IO;
using System.IO.Ports;
using System.Windows;
using System.Windows.Input;
using System.Windows.Threading;
using Microsoft.Win32;

namespace ModemTool;

public partial class MainWindow : Window
{
    private readonly SerialLink _link = new();
    private readonly AtBridgeClient _atBridge = new();
    private readonly DispatcherTimer _statusTimer;

    // Accumulator for the "[STATUS]"/"[CONFIG]" ... "[END]" framed dumps
    // (see usb_print_status()/usb_print_config() in the firmware's
    // modem_handler.cpp) — every other line just goes straight to the log.
    private readonly List<KeyValuePair<string, string>> _pendingDump = new();
    private string? _pendingDumpKind;

    // Every line also goes straight to disk as it arrives, independent of
    // LogBox's own display cap below — the window trims old text to stay
    // responsive, but nothing is lost, unlike a terminal that just clears
    // itself once its own scrollback limit is hit. One file per connection.
    private StreamWriter? _logFileWriter;

    public ObservableCollection<KeyValueRow> StatusRows { get; } = new();
    public ObservableCollection<SettingRow> ConfigRows { get; } = new();

    public MainWindow()
    {
        InitializeComponent();
        DataContext = this;

        RefreshPorts();
        _link.LineReceived += line => Dispatcher.Invoke(() => OnLineReceived(line));

        // AT-bridge relay: pipe raw bytes both ways between the serial port and
        // the WebSocket, completely independent of the line-based console
        // protocol above (see SerialLink's and AtBridgeClient's own comments).
        _link.RawDataReceived += data => { if (_atBridge.IsRunning) _atBridge.SendFromSerial(data); };
        _atBridge.DataReceived += data => _link.SendRaw(data);
        _atBridge.StatusChanged += status => Dispatcher.Invoke(() => BridgeStatus.Text = status);

        _statusTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        _statusTimer.Tick += (_, _) => { if (_link.IsOpen) _link.Send("status"); };
    }

    // ── Connection ──────────────────────────────────────────────────────

    private void RefreshPorts_Click(object sender, RoutedEventArgs e) => RefreshPorts();

    private void RefreshPorts()
    {
        var ports = SerialPort.GetPortNames().OrderBy(p => p).ToList();
        var current = PortCombo.SelectedItem as string;
        PortCombo.ItemsSource = ports;
        if (current != null && ports.Contains(current))
            PortCombo.SelectedItem = current;
        else if (ports.Count > 0)
            PortCombo.SelectedIndex = 0;
    }

    private void Connect_Click(object sender, RoutedEventArgs e)
    {
        if (_link.IsOpen)
        {
            _statusTimer.Stop();
            _link.Close();
            StopLogFile();
            ConnectButton.Content = "Connect";
            ConnectionState.Text = "Disconnected";
            return;
        }

        if (PortCombo.SelectedItem is not string port)
        {
            MessageBox.Show(this, "Select a COM port.", "Modem Tool", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        try
        {
            _link.Open(port);
            ConnectButton.Content = "Disconnect";
            ConnectionState.Text = $"Connected: {port}";
            StartLogFile(port);
            _link.Send("status");
            _link.Send("config");
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, $"Failed to open {port}:\n{ex.Message}", "Modem Tool",
                MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void Reset_Click(object sender, RoutedEventArgs e)
    {
        if (!_link.IsOpen) return;
        if (MessageBox.Show(this, "Reset the modem now?", "Confirm",
                MessageBoxButton.YesNo, MessageBoxImage.Question) == MessageBoxResult.Yes)
            _link.Send("r");
    }

    // ── Incoming data ───────────────────────────────────────────────────

    private void OnLineReceived(string line)
    {
        AppendLog(line);

        if (line.StartsWith("[USB SET]"))
            StatusBar.Text = line;

        switch (line)
        {
            case "[STATUS]":
                _pendingDumpKind = "STATUS";
                _pendingDump.Clear();
                return;
            case "[CONFIG]":
                _pendingDumpKind = "CONFIG";
                _pendingDump.Clear();
                return;
            case "[END]":
                if (_pendingDumpKind == "STATUS") PopulateStatus(_pendingDump);
                else if (_pendingDumpKind == "CONFIG") PopulateSettings(_pendingDump);
                _pendingDumpKind = null;
                return;
        }

        if (_pendingDumpKind != null)
        {
            var eq = line.IndexOf('=');
            if (eq > 0)
                _pendingDump.Add(new KeyValuePair<string, string>(line[..eq], line[(eq + 1)..]));
        }
    }

    private void PopulateStatus(List<KeyValuePair<string, string>> src)
    {
        StatusRows.Clear();
        foreach (var kv in src) StatusRows.Add(new KeyValueRow(kv.Key, kv.Value));
    }

    // Updates existing SettingRow objects in place (matched by Key) instead of
    // replacing the collection — a "config" refresh (e.g. the one triggered
    // right after an Apply, see ApplySetting_Click) would otherwise wipe out
    // whatever the user is mid-typing into every OTHER row's New value box.
    private void PopulateSettings(List<KeyValuePair<string, string>> src)
    {
        foreach (var kv in src)
        {
            var existing = ConfigRows.FirstOrDefault(r => r.Key == kv.Key);
            if (existing != null) existing.CurrentValue = kv.Value;
            else ConfigRows.Add(new SettingRow(kv.Key, kv.Value));
        }
    }

    // ── Status / config tabs ────────────────────────────────────────────

    private void RefreshStatus_Click(object sender, RoutedEventArgs e)
    {
        if (_atBridge.IsRunning) return;
        _link.Send("status");
    }

    private void RefreshConfig_Click(object sender, RoutedEventArgs e)
    {
        if (_atBridge.IsRunning) return;
        _link.Send("config");
    }

    private void AutoRefresh_Checked(object sender, RoutedEventArgs e) { if (!_atBridge.IsRunning) _statusTimer.Start(); }
    private void AutoRefresh_Unchecked(object sender, RoutedEventArgs e) => _statusTimer.Stop();

    private void ApplySetting_Click(object sender, RoutedEventArgs e)
    {
        if (_atBridge.IsRunning) return;
        if (!_link.IsOpen)
        {
            MessageBox.Show(this, "Connect to the modem first.", "Modem Tool", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }
        if (sender is not FrameworkElement { DataContext: SettingRow row }) return;
        if (!SettingsMap.TryGetCommand(row.Key, out var command)) return;
        if (row.NewValue.Length == 0)
        {
            MessageBox.Show(this, "Enter a new value first.", "Modem Tool", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        _link.Send($"set {command} {row.NewValue}");
        row.NewValue = "";
        RequestConfigRefreshSoon();
    }

    // "set ..." is applied on the modem's next main-loop iteration, not
    // synchronously — a "config" sent back-to-back right after it would just
    // read the still-stale value. A short delay is plenty (the loop runs far
    // faster than this) and lets the just-applied row's Current value column
    // catch up automatically.
    private void RequestConfigRefreshSoon()
    {
        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(300) };
        timer.Tick += (s, _) =>
        {
            ((DispatcherTimer)s!).Stop();
            if (_link.IsOpen && !_atBridge.IsRunning) _link.Send("config");
        };
        timer.Start();
    }

    // ── Log / console tab ───────────────────────────────────────────────

    // Purely a display-performance cap for the TextBox — trims the OLDER half
    // and keeps the rest, it never wipes the whole thing the way the user's
    // other terminal app does. The real safety net is the log file below,
    // which is never trimmed.
    private const int LogCap = 1_000_000;

    private void AppendLog(string line)
    {
        _logFileWriter?.WriteLine(line);

        LogBox.AppendText(line + Environment.NewLine);
        if (LogBox.Text.Length > LogCap)
            LogBox.Text = LogBox.Text[^(LogCap / 2)..];
        if (AutoScrollCheck.IsChecked == true)
            LogBox.ScrollToEnd();
    }

    private void StartLogFile(string port)
    {
        try
        {
            var dir = Path.Combine(AppContext.BaseDirectory, "logs");
            Directory.CreateDirectory(dir);
            var path = Path.Combine(dir, $"{DateTime.Now:yyyyMMdd_HHmmss}_{port}.log");
            _logFileWriter = new StreamWriter(path, append: false) { AutoFlush = true };
            LogFilePath.Text = $"Logging to: {path}";
        }
        catch (IOException)
        {
            // Disk/permission trouble — keep going without a file rather than
            // blocking the user from using the rest of the app over this.
            LogFilePath.Text = "Logging to file failed — check disk/permissions.";
        }
    }

    private void StopLogFile()
    {
        _logFileWriter?.Dispose();
        _logFileWriter = null;
        LogFilePath.Text = "";
    }

    private void ClearLog_Click(object sender, RoutedEventArgs e) => LogBox.Clear();

    private void SaveLog_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new SaveFileDialog
        {
            Filter = "Log files (*.log)|*.log|All files (*.*)|*.*",
            FileName = $"modemtool_{DateTime.Now:yyyyMMdd_HHmmss}.log",
        };
        if (dialog.ShowDialog(this) != true) return;
        try { File.WriteAllText(dialog.FileName, LogBox.Text); }
        catch (IOException ex)
        {
            MessageBox.Show(this, $"Failed to save:\n{ex.Message}", "Modem Tool", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void LogAt_Click(object sender, RoutedEventArgs e) { if (!_atBridge.IsRunning) _link.Send("l"); }
    private void LogGsm_Click(object sender, RoutedEventArgs e) { if (!_atBridge.IsRunning) _link.Send("g"); }
    private void LogAll_Click(object sender, RoutedEventArgs e) { if (!_atBridge.IsRunning) _link.Send("a"); }

    private void SendConsole_Click(object sender, RoutedEventArgs e) => SendConsoleLine();

    private void ConsoleInput_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter) SendConsoleLine();
    }

    private void SendConsoleLine()
    {
        if (_atBridge.IsRunning) return;
        var text = ConsoleInput.Text;
        if (string.IsNullOrEmpty(text)) return;
        _link.Send(text);
        AppendLog("> " + text);
        ConsoleInput.Clear();
    }

    // ── AT bridge tab ────────────────────────────────────────────────────
    // See AtBridgeClient's own comment — a C# port of host/at-bridge-relay.py's
    // "relay" role. While it's running, every other control that writes to the
    // port is disabled: the modem's bridgeMode passthrough (hw_config.c) takes
    // over the whole USB console the moment the remote viewer types "b", so any
    // console command we sent locally at that point would just get sucked into
    // the raw AT stream instead of being parsed.

    private void BridgeToggle_Click(object sender, RoutedEventArgs e)
    {
        if (_atBridge.IsRunning)
        {
            _atBridge.Stop();
            BridgeToggleButton.Content = "Start relay";
            SetPortControlsEnabled(true);
            return;
        }

        if (!_link.IsOpen)
        {
            MessageBox.Show(this, "Connect to the modem first.", "Modem Tool", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        var username = BridgeUsernameBox.Text.Trim();
        var password = BridgePasswordBox.Text;
        if (username.Length == 0 || password.Length == 0)
        {
            MessageBox.Show(this, "Enter the website username and password.", "Modem Tool", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        _statusTimer.Stop();
        AutoRefreshCheck.IsChecked = false;
        SetPortControlsEnabled(false);
        BridgeToggleButton.Content = "Stop relay";
        _atBridge.Start(username, password);
    }

    private void SetPortControlsEnabled(bool enabled)
    {
        StatusRefreshButton.IsEnabled = enabled;
        AutoRefreshCheck.IsEnabled = enabled;
        ConfigRefreshButton.IsEnabled = enabled;
        SettingsGrid.IsEnabled = enabled;
        LogAtButton.IsEnabled = enabled;
        LogGsmButton.IsEnabled = enabled;
        LogAllButton.IsEnabled = enabled;
        ConsoleInput.IsEnabled = enabled;
        SendConsoleButton.IsEnabled = enabled;
        ResetButton.IsEnabled = enabled;
    }

    protected override void OnClosed(EventArgs e)
    {
        _statusTimer.Stop();
        _atBridge.Dispose();
        _link.Dispose();
        StopLogFile();
        base.OnClosed(e);
    }
}
