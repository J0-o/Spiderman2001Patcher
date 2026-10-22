using KPatchCore.Common;
using KPatchCore.Managers;
using KPatchCore.Models;

namespace KPatchCore.Applicators;

/// <summary>
/// Handles patch removal and restoration
/// </summary>
public static class PatchRemover
{
    /// <summary>
    /// Safely deletes a file if it exists and adds a message
    /// </summary>
    /// <param name="gameDir">Game directory</param>
    /// <param name="fileName">File name to delete</param>
    /// <param name="removedFiles">List to add removed file names to</param>
    /// <param name="messages">List to add messages to</param>
    private static void SafeDeleteFile(string gameDir, string fileName, List<string> removedFiles, List<string> messages)
    {
        var filePath = Path.Combine(gameDir, fileName);
        if (File.Exists(filePath))
        {
            File.Delete(filePath);
            removedFiles.Add(fileName);
            messages.Add($"  Removed {fileName}");
        }
    }

    /// <summary>
    /// Removal result with detailed information
    /// </summary>
    public sealed class RemovalResult
    {
        /// <summary>
        /// Whether removal succeeded
        /// </summary>
        public required bool Success { get; init; }

        /// <summary>
        /// Error message if failed
        /// </summary>
        public string? Error { get; init; }

        /// <summary>
        /// List of files that were removed
        /// </summary>
        public List<string> RemovedFiles { get; init; } = new();

        /// <summary>
        /// Whether backup was restored
        /// </summary>
        public bool BackupRestored { get; init; }

        /// <summary>
        /// Additional messages
        /// </summary>
        public List<string> Messages { get; init; } = new();
    }

    /// <summary>
    /// Removes all patches from a game installation
    /// </summary>
    /// <param name="gameExePath">Path to the game executable</param>
    /// <param name="removeManagedState">Whether to remove KPM's managed install identity file.</param>
    /// <returns>Removal result</returns>
    public static RemovalResult RemoveAllPatches(string gameExePath, bool removeManagedState = true)
    {
        var messages = new List<string>();
        var removedFiles = new List<string>();

        try
        {
            // Step 1: Validate inputs
            messages.Add("Step 1/4: Validating inputs...");
            if (!File.Exists(gameExePath))
            {
                return new RemovalResult
                {
                    Success = false,
                    Error = $"Game executable not found: {gameExePath}",
                    Messages = messages
                };
            }

            var gameDir = Path.GetDirectoryName(gameExePath);
            if (gameDir == null)
            {
                return new RemovalResult
                {
                    Success = false,
                    Error = "Invalid game executable path",
                    Messages = messages
                };
            }

            // Step 2: Find and restore backup
            messages.Add("Step 2/4: Finding backup...");
            var backupResult = BackupManager.FindLatestBackup(gameExePath);

            if (!backupResult.Success || backupResult.Data == null)
            {
                messages.Add($"  No backup found: {backupResult.Error}");
                messages.Add("  Will clean up patch files without restoring executable");
            }
            else
            {
                var backup = backupResult.Data;
                messages.Add($"  Found backup: {Path.GetFileName(backup.BackupPath)}");

                // Restore backup
                messages.Add("  Restoring backup...");
                var restoreResult = BackupManager.RestoreBackup(backup);

                if (!restoreResult.Success)
                {
                    return new RemovalResult
                    {
                        Success = false,
                        Error = $"Failed to restore backup: {restoreResult.Error}",
                        Messages = messages
                    };
                }

                messages.Add("  Backup restored successfully");

                // Clean up backup files after successful restore
                messages.Add("  Cleaning up backup files...");
                var deleteResult = BackupManager.DeleteBackup(backup);
                if (deleteResult.Success)
                {
                    messages.AddRange(deleteResult.Messages.Select(m => $"  {m}"));
                }
                else
                {
                    messages.Add($"  ⚠️ Warning: Failed to delete backup files: {deleteResult.Error}");
                }
            }

            // Step 3: Remove patch files
            messages.Add("Step 3/4: Removing patch files...");

            // Remove patches directory
            var patchesDir = Path.Combine(gameDir, "patches");
            if (Directory.Exists(patchesDir))
            {
                var dllFiles = Directory.GetFiles(patchesDir, "*.dll");
                foreach (var dll in dllFiles)
                {
                    File.Delete(dll);
                    removedFiles.Add(Path.GetFileName(dll));
                }

                if (Directory.GetFiles(patchesDir).Length == 0 &&
                    Directory.GetDirectories(patchesDir).Length == 0)
                {
                    Directory.Delete(patchesDir);
                    messages.Add($"  Removed patches directory");
                }
            }

            // List of files to remove from game directory
            var filesToRemove = new List<string>
            {
                "patch_config.toml",
                "SpiderManPatcher.Runtime.dll"
            };

            if (removeManagedState)
            {
                filesToRemove.Add(InstallStateManager.StateFileName);
            }

            foreach (var fileName in filesToRemove)
            {
                SafeDeleteFile(gameDir, fileName, removedFiles, messages);
            }

            // Remove only KPM's applied option sections; preserve the rest of each
            // patch-owned settings file.
            foreach (var optionsFile in PatchOptionsIni.RemoveSections(gameDir))
            {
                removedFiles.Add(optionsFile);
                messages.Add($"  Removed {optionsFile}");
            }

            // Step 4: Verify clean state
            messages.Add("Step 4/4: Verifying clean state...");
            var configPath = Path.Combine(gameDir, "patch_config.toml");
            var isClean = !File.Exists(configPath) && !Directory.Exists(patchesDir);

            if (isClean)
            {
                messages.Add("  Game is now patch-free");
            }
            else
            {
                messages.Add("  ⚠️ Warning: Some patch files may remain");
            }

            return new RemovalResult
            {
                Success = true,
                RemovedFiles = removedFiles,
                BackupRestored = backupResult.Success && backupResult.Data != null,
                Messages = messages
            };
        }
        catch (Exception ex)
        {
            return new RemovalResult
            {
                Success = false,
                Error = $"Removal failed: {ex.Message}",
                RemovedFiles = removedFiles,
                Messages = messages
            };
        }
    }

    /// <summary>
    /// Checks if a game has patches installed
    /// </summary>
    /// <param name="gameExePath">Path to the game executable</param>
    /// <returns>Result indicating if patches are installed</returns>
    public static PatchResult<bool> HasPatchesInstalled(string gameExePath)
    {
        if (!File.Exists(gameExePath))
        {
            return PatchResult<bool>.Fail($"Game executable not found: {gameExePath}");
        }

        var gameDir = Path.GetDirectoryName(gameExePath);
        if (gameDir == null)
        {
            return PatchResult<bool>.Fail("Invalid game executable path");
        }

        try
        {
            var hasManagedState = InstallStateManager.Load(gameExePath).Success;
            var isPatched = hasManagedState;

            var details = new List<string>();
            if (hasManagedState) details.Add($"{InstallStateManager.StateFileName} exists");

            var message = isPatched
                ? $"Patches installed ({string.Join(", ", details)})"
                : "No patches detected";

            return PatchResult<bool>.Ok(isPatched, message);
        }
        catch (Exception ex)
        {
            return PatchResult<bool>.Fail($"Failed to check patch status: {ex.Message}");
        }
    }
}
