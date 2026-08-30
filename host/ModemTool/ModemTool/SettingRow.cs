using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace ModemTool;

/// <summary>
/// One row of the Settings grid: a "cfg."/"mqtt."/"net." key from the
/// "config" dump (see usb_print_config() in the firmware's modem_handler.cpp),
/// its last-known current value, and an editable new-value box the user fills
/// in before clicking that row's Apply button (see SettingsMap for how a key
/// maps to the console "set" command that actually changes it).
/// </summary>
public sealed class SettingRow(string key, string currentValue) : INotifyPropertyChanged
{
    public string Key { get; } = key;

    /// <summary>Shown as a tooltip on the Parameter cell — the "set" command's expected argument syntax.</summary>
    public string Hint { get; } = SettingsMap.HintFor(key);

    /// <summary>False for keys with no corresponding "set" command (e.g. connectionLink, which is server-generated) — Apply is disabled.</summary>
    public bool Editable { get; } = SettingsMap.TryGetCommand(key, out _);

    private string _currentValue = currentValue;
    public string CurrentValue
    {
        get => _currentValue;
        set { _currentValue = value; OnPropertyChanged(); }
    }

    private string _newValue = "";
    public string NewValue
    {
        get => _newValue;
        set { _newValue = value; OnPropertyChanged(); }
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
