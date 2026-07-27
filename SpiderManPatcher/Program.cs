using KPatchCore.Applicators;
using KPatchCore.Launcher;
using KPatchCore.Managers;

namespace SpiderManPatcher;

internal static class Program
{
    private const string GameExecutableName = "SpideyPC.exe";
    private const string RuntimeDllName = "SpiderManPatcher.Runtime.dll";

    private static int Main(string[] args)
    {
        if (args.Length > 2 || args.Any(arg => arg is "-h" or "--help"))
        {
            PrintUsage();
            return args.Length > 2 ? 1 : 0;
        }

        string? extractedRuntimePath = null;
        try
        {
            var baseDirectory = AppContext.BaseDirectory;
            var gamePath = Path.GetFullPath(
                args.Length >= 1 ? args[0] : Path.Combine(baseDirectory, GameExecutableName));
            var patchesDirectory = Path.GetFullPath(
                args.Length >= 2 ? args[1] : Path.Combine(baseDirectory, "patches"));
            extractedRuntimePath = ExtractRuntimeDll();

            if (!File.Exists(gamePath))
            {
                return Fail($"Game executable not found: {gamePath}");
            }

            if (!Directory.Exists(patchesDirectory))
            {
                return Fail($"Patches directory not found: {patchesDirectory}");
            }

            Console.WriteLine($"Game:    {gamePath}");
            Console.WriteLine($"Patches: {patchesDirectory}");

            var orchestrator = new PatchOrchestrator(patchesDirectory);
            var scanResult = orchestrator.RescanPatches();
            if (!scanResult.Success)
            {
                return Fail(scanResult.Error ?? "Patch scan failed.");
            }

            var patchIds = orchestrator.GetAvailablePatches().Keys
                .OrderBy(id => id, StringComparer.OrdinalIgnoreCase)
                .ToList();
            if (patchIds.Count == 0)
            {
                return Fail($"No .kpatch files found in: {patchesDirectory}");
            }

            var status = PatchRemover.HasPatchesInstalled(gamePath);
            if (!status.Success)
            {
                return Fail(status.Error ?? "Could not inspect the current patch state.");
            }

            if (status.Data)
            {
                Console.WriteLine("Restoring the previous managed patch state...");
                var removal = PatchRemover.RemoveAllPatches(gamePath, removeManagedState: false);
                if (!removal.Success)
                {
                    return Fail(removal.Error ?? "Could not remove the previous patch state.");
                }
            }

            Console.WriteLine($"Applying {patchIds.Count} patch(es): {string.Join(", ", patchIds)}");
            var install = orchestrator.InstallPatches(
                gamePath,
                patchIds,
                createBackup: true,
                patcherDllPath: extractedRuntimePath);
            if (!install.Success)
            {
                return Fail(install.Error ?? "Patch installation failed.");
            }

            Console.WriteLine("Launching Spider-Man...");
            var launch = GameLauncher.LaunchPatchedGame(gamePath);
            if (!launch.Success)
            {
                return Fail(launch.Error ?? "Game launch failed.");
            }

            Console.WriteLine(
                launch.GameProcess == null
                    ? "Spider-Man launched with all patches."
                    : $"Spider-Man launched with all patches (PID {launch.GameProcess.Id}).");
            return 0;
        }
        catch (Exception ex)
        {
            return Fail(ex.Message);
        }
        finally
        {
            if (extractedRuntimePath != null)
            {
                try
                {
                    File.Delete(extractedRuntimePath);
                }
                catch
                {
                    // The installed copy in the game directory is used after this point.
                }
            }
        }
    }

    private static int Fail(string message)
    {
        Console.Error.WriteLine($"ERROR: {message}");
        return 1;
    }

    private static string ExtractRuntimeDll()
    {
        using var source = typeof(Program).Assembly.GetManifestResourceStream(RuntimeDllName)
            ?? throw new InvalidOperationException("Embedded patch runtime was not found.");

        var path = Path.Combine(
            Path.GetTempPath(),
            $"SpiderManPatcher.Runtime.{Environment.ProcessId}.dll");
        using var destination = File.Create(path);
        source.CopyTo(destination);
        return path;
    }

    private static void PrintUsage()
    {
        Console.WriteLine("SpideyLauncher [game-executable] [patches-directory]");
        Console.WriteLine();
        Console.WriteLine("Defaults:");
        Console.WriteLine($"  game-executable:   .\\{GameExecutableName}");
        Console.WriteLine("  patches-directory: .\\patches");
    }
}
