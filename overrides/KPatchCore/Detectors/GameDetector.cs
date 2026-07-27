using System.Text.Json;
using KPatchCore.Common;
using KPatchCore.Managers;
using KPatchCore.Models;

namespace KPatchCore.Detectors;

/// <summary>
/// Detects game version by hashing the executable
/// </summary>
public static class GameDetector
{
    /// <summary>
    /// Known game versions database
    /// Maps SHA256 hash to GameVersion
    /// </summary>
    private static readonly Dictionary<string, GameVersion> KnownVersions = new()
    {
        ["0A11A49F3F63D4BB15650F322D654553FC47234B01885B852A058FF074689DEA"] = new GameVersion
        {
            Platform = Platform.Windows,
            Distribution = Distribution.Physical,
            Version = "Retail",
            Architecture = Architecture.x86,
            Title = GameTitle.SpiderMan,
            FileSize = 1507328,
            Hash = "0A11A49F3F63D4BB15650F322D654553FC47234B01885B852A058FF074689DEA"
        },
        ["D55A0BB0E920C497CE1CA76F08ED2E62FEEFCB6FF3C2901C0D59890F099BA93C"] = new GameVersion
        {
            Platform = Platform.Windows,
            Distribution = Distribution.Physical,
            Version = "No-CD",
            Architecture = Architecture.x86,
            Title = GameTitle.SpiderMan,
            FileSize = 1507328,
            Hash = "D55A0BB0E920C497CE1CA76F08ED2E62FEEFCB6FF3C2901C0D59890F099BA93C"
        }
    };

    /// <summary>
    /// Detects game version from executable path.
    /// </summary>
    /// <param name="exePath">Path to game executable</param>
    /// <param name="allowManagedInstallState">
    /// If true, an unknown current executable hash can be resolved from KPM's
    /// persisted managed-install state or legacy KPM identity carriers. This is
    /// intended for UI status, launch decisions, and subsequent KPM-managed patch
    /// installs after STATIC hooks have intentionally modified the executable.
    /// </param>
    /// <param name="requireKnownManagedStateHash">
    /// If true, kpm_install_state.json is accepted only when OriginalHash maps to
    /// a known GameVersion. Legacy patch_config.toml and backup fallbacks are not
    /// disabled by this setting.
    /// </param>
    /// <returns>Result containing GameVersion or error</returns>
    public static PatchResult<GameVersion> DetectVersion(
        string exePath,
        bool allowManagedInstallState = false,
        bool requireKnownManagedStateHash = false)
    {
        if (!File.Exists(exePath))
        {
            return PatchResult<GameVersion>.Fail($"Executable not found: {exePath}");
        }

        try
        {
            // Compute hash and file size
            var (hash, fileSize) = FileHasher.ComputeHashAndSize(exePath);

            // Look up in known versions
            if (TryGetKnownVersion(hash, out var gameVersion))
            {
                return PatchResult<GameVersion>.Ok(
                    gameVersion,
                    $"Detected: {gameVersion.DisplayName}"
                );
            }

            // STATIC hooks intentionally mutate the executable. When enabled,
            // resolve the game identity from KPM-owned persisted state.
            if (allowManagedInstallState)
            {
                var managedResult = DetectVersionFromManagedInstallState(
                    exePath,
                    hash,
                    requireKnownManagedStateHash);
                if (managedResult.Success && managedResult.Data != null)
                {
                    return managedResult;
                }
            }

            // Version not recognized - create unknown version with hash info
            var unknownVersion = new GameVersion
            {
                Platform = Platform.Windows, // Assume Windows for now
                Distribution = Distribution.Other,
                Version = "Unknown",
                Architecture = Architecture.x86, // Default assumption
                Title = GameTitle.Unknown,
                FileSize = fileSize,
                Hash = hash
            };

            return PatchResult<GameVersion>.Ok(
                unknownVersion,
                $"Unknown version (hash: {PreviewHash(hash)}...)"
            );
        }
        catch (Exception ex)
        {
            return PatchResult<GameVersion>.Fail($"Failed to detect version: {ex.Message}");
        }
    }

    private static PatchResult<GameVersion> DetectVersionFromManagedInstallState(
        string exePath,
        string currentHash,
        bool requireKnownManagedStateHash)
    {
        // Primary source: KPM's explicit managed install state. This is the
        // authoritative post-install identity for statically-patched executables.
        var stateResult = InstallStateManager.Load(exePath);
        if (stateResult.Success && stateResult.Data != null)
        {
            var state = stateResult.Data;
            var version = ResolveVersionFromState(state, requireKnownManagedStateHash);
            if (version != null)
            {
                return PatchResult<GameVersion>.Ok(
                    version,
                    $"Detected from KPM install state: {version.DisplayName}"
                );
            }
        }

        // Last resort: backup metadata contains the original clean executable hash
        // and detected version captured before static hooks were applied.
        var backupVersion = TryReadVersionFromLatestBackup(exePath);
        if (backupVersion != null)
        {
            return PatchResult<GameVersion>.Ok(
                backupVersion,
                $"Detected from latest backup metadata: {backupVersion.DisplayName}"
            );
        }

        return PatchResult<GameVersion>.Fail(
            $"Managed KPM identity sources did not identify executable hash {PreviewHash(currentHash)}...");
    }

    private static GameVersion? ResolveVersionFromState(
        ManagedInstallState state,
        bool requireKnownManagedStateHash)
    {
        if (!string.IsNullOrWhiteSpace(state.OriginalHash) &&
            TryGetKnownVersion(state.OriginalHash, out var versionFromKnownHash))
        {
            return versionFromKnownHash;
        }

        if (requireKnownManagedStateHash)
        {
            return null;
        }

        if (state.OriginalVersion != null && state.OriginalVersion.Version != "Unknown")
        {
            return state.OriginalVersion;
        }

        return null;
    }

    private static GameVersion? TryReadVersionFromLatestBackup(string exePath)
    {
        var backupPath = PathHelpers.FindLatestBackup(exePath);
        if (string.IsNullOrWhiteSpace(backupPath))
        {
            return null;
        }

        var metadataPath = $"{backupPath}.json";
        if (File.Exists(metadataPath))
        {
            try
            {
                var backupInfo = JsonSerializer.Deserialize<BackupInfo>(File.ReadAllText(metadataPath));
                if (backupInfo != null)
                {
                    if (backupInfo.DetectedVersion != null &&
                        backupInfo.DetectedVersion.Version != "Unknown")
                    {
                        return backupInfo.DetectedVersion;
                    }

                    if (!string.IsNullOrWhiteSpace(backupInfo.Hash) &&
                        TryGetKnownVersion(backupInfo.Hash, out var versionFromMetadataHash))
                    {
                        return versionFromMetadataHash;
                    }
                }
            }
            catch
            {
                // Fall back to hashing the backup file below.
            }
        }

        try
        {
            var (backupHash, _) = FileHasher.ComputeHashAndSize(backupPath);

            return TryGetKnownVersion(backupHash, out var versionFromBackupHash)
                ? versionFromBackupHash
                : null;
        }
        catch
        {
            return null;
        }
    }

    /// <summary>
    /// Registers a new known version in the database
    /// Useful for testing and adding new versions dynamically
    /// </summary>
    /// <param name="hash">SHA256 hash of the executable</param>
    /// <param name="version">Game version information</param>
    /// <returns>Result indicating success or failure</returns>
    public static PatchResult RegisterKnownVersion(string hash, GameVersion version)
    {
        try
        {
            var normalizedHash = NormalizeHash(hash);
            if (KnownVersions.ContainsKey(normalizedHash))
            {
                return PatchResult.Fail($"Hash already registered: {PreviewHash(normalizedHash)}...");
            }

            KnownVersions[normalizedHash] = version;

            return PatchResult.Ok($"Registered version: {version.DisplayName}");
        }
        catch (Exception ex)
        {
            return PatchResult.Fail($"Failed to register version: {ex.Message}");
        }
    }

    /// <summary>
    /// Gets all known versions
    /// </summary>
    /// <returns>Dictionary of known versions</returns>
    public static IReadOnlyDictionary<string, GameVersion> GetKnownVersions()
    {
        return KnownVersions;
    }

    /// <summary>
    /// Checks if a specific hash is in the known versions database
    /// </summary>
    /// <param name="hash">SHA256 hash to check</param>
    /// <returns>True if hash is known, false otherwise</returns>
    public static bool IsKnownVersion(string hash)
    {
        return KnownVersions.ContainsKey(NormalizeHash(hash));
    }

    /// <summary>
    /// Helper method to detect version and get actual hash for registration
    /// Useful for building the known versions database
    /// </summary>
    /// <param name="exePath">Path to executable</param>
    /// <returns>Result containing hash and file size for database entry</returns>
    public static PatchResult<(string Hash, long FileSize)> GetExecutableInfo(string exePath)
    {
        if (!File.Exists(exePath))
        {
            return PatchResult<(string, long)>.Fail($"Executable not found: {exePath}");
        }

        try
        {
            var (hash, fileSize) = FileHasher.ComputeHashAndSize(exePath);
            return PatchResult<(string, long)>.Ok(
                (hash, fileSize),
                $"Hash: {hash}, Size: {fileSize} bytes"
            );
        }
        catch (Exception ex)
        {
            return PatchResult<(string, long)>.Fail($"Failed to get executable info: {ex.Message}");
        }
    }

    private static bool TryGetKnownVersion(string hash, out GameVersion version)
    {
        var found = KnownVersions.TryGetValue(NormalizeHash(hash), out var matchedVersion);
        version = matchedVersion!;
        return found;
    }

    private static string NormalizeHash(string hash)
    {
        return hash.Trim().ToUpperInvariant();
    }

    private static string PreviewHash(string hash)
    {
        return hash.Length > 16 ? hash.Substring(0, 16) : hash;
    }
}
