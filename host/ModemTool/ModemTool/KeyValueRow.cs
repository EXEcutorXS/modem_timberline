namespace ModemTool;

/// <summary>One row of a parsed "status"/"config" dump, for grid binding.</summary>
public sealed class KeyValueRow(string key, string value)
{
    public string Key { get; } = key;
    public string Value { get; } = value;
}
