using System.Diagnostics;

namespace KPatchCore.Launcher;

public sealed class LaunchResult
{
    public required bool Success { get; init; }
    public string? Error { get; init; }
    public Process? GameProcess { get; init; }

    public static LaunchResult Ok(Process process) =>
        new()
        {
            Success = true,
            GameProcess = process
        };

    public static LaunchResult Fail(string error) =>
        new()
        {
            Success = false,
            Error = error
        };
}
