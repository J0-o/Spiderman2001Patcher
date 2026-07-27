namespace KPatchCore.Launcher;

/// <summary>
/// Launches an installed, patched game with the runtime injected.
/// </summary>
public static class GameLauncher
{
    public static LaunchResult LaunchPatchedGame(
        string gameExePath,
        string? commandLineArgs = null)
    {
        if (string.IsNullOrWhiteSpace(gameExePath) || !File.Exists(gameExePath))
        {
            return LaunchResult.Fail($"Game executable not found: {gameExePath}");
        }

        var gameDir = Path.GetDirectoryName(gameExePath);
        if (string.IsNullOrWhiteSpace(gameDir))
        {
            return LaunchResult.Fail($"Could not determine game directory from path: {gameExePath}");
        }

        var configPath = Path.Combine(gameDir, "patch_config.toml");
        if (!File.Exists(configPath))
        {
            return LaunchResult.Fail($"Patch configuration not found: {configPath}");
        }

        var runtimePath = Path.Combine(gameDir, "SpiderManPatcher.Runtime.dll");
        if (!File.Exists(runtimePath))
        {
            return LaunchResult.Fail($"Patch runtime not found: {runtimePath}");
        }

        return ProcessInjector.Launch(gameExePath, runtimePath, commandLineArgs);
    }
}
