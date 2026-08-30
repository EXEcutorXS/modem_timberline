namespace ModemTool;

/// <summary>
/// Maps a "config" dump key (see usb_print_config() in the firmware's
/// modem_handler.cpp) to the console "set" command keyword that changes it
/// (see modem_process_usb_set() there, which reuses the same command syntax
/// as an SMS control command — Library/Sms/timberline_sms.cpp's parse_one()).
/// A key with no entry here has no setter on the firmware side (it's either
/// read-only device state re-used in the config dump, like connectionLink,
/// or not yet wired up) — SettingRow.Editable is false for those.
/// </summary>
public static class SettingsMap
{
    private static readonly Dictionary<string, (string Command, string Hint)> Map = new()
    {
        ["cfg.phoneAdmin"] = ("admin", "phone number, e.g. +79001234567"),
        ["cfg.phone1"] = ("phone1", "phone number"),
        ["cfg.phone2"] = ("phone2", "phone number"),
        ["cfg.phone3"] = ("phone3", "phone number"),
        ["cfg.phone4"] = ("phone4", "phone number"),
        ["cfg.pin"] = ("setpin", "4 digits"),
        ["cfg.useInternet"] = ("internet", "on / off"),
        ["cfg.tempUnit"] = ("unit", "c / f"),
        ["cfg.allowRoaming"] = ("roaming", "on / off"),
        ["cfg.force2gOnly"] = ("2g", "on / off"),
        ["cfg.faultReport"] = ("faultreport", "on / off"),
        ["cfg.cmdAck"] = ("ack", "on / off"),
        ["cfg.language"] = ("lang", "en / de"),
        ["mqtt.broker"] = ("server", "hostname, 1-31 chars"),
        ["mqtt.username"] = ("login", "1-15 chars"),
        ["mqtt.password"] = ("password", "1-23 chars"),
        ["net.apn"] = ("apn", "0-31 chars, empty = auto"),
        ["net.apnUsername"] = ("apnuser", "0-31 chars, empty = none"),
        ["net.apnPassword"] = ("apnpass", "0-31 chars, empty = none"),
        ["net.internetCheckUrl"] = ("checkurl", "0-63 chars, empty = skip check"),
    };

    public static bool TryGetCommand(string key, out string command)
    {
        if (Map.TryGetValue(key, out var entry)) { command = entry.Command; return true; }
        command = "";
        return false;
    }

    public static string HintFor(string key) =>
        Map.TryGetValue(key, out var entry) ? entry.Hint : "read-only";
}
