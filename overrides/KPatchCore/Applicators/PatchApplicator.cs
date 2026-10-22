using KPatchCore.Common;
using KPatchCore.Detectors;
using KPatchCore.Managers;
using KPatchCore.Models;
using KPatchCore.Validators;

namespace KPatchCore.Applicators;

/// <summary>
/// Orchestrates the full patch installation process
/// </summary>
public class PatchApplicator
{
    private readonly PatchRepository _repository;

    /// <summary>
    /// Installation options
    /// </summary>
    public sealed class InstallOptions
    {
        /// <summary>
        /// Path to the game executable
        /// </summary>
        public required string GameExePath { get; init; }

        /// <summary>
        /// Patch IDs to install
        /// </summary>
        public required List<string> PatchIds { get; init; }

        /// <summary>
        /// Whether to create a backup before installation
        /// </summary>
        public bool CreateBackup { get; init; } = true;

        /// <summary>
        /// Values chosen for the patches' options: patch id, option id, then value.
        /// An option without an entry takes its manifest default.
        /// </summary>
        public Dictionary<string, Dictionary<string, string>> OptionValues { get; init; } = new();

        /// <summary>
        /// Path to SpiderManPatcher.Runtime.dll.
        /// </summary>
        public string? PatcherDllPath { get; init; }

    }

    /// <summary>
    /// Installation result with detailed information
    /// </summary>
    public sealed class InstallResult
    {
        /// <summary>
        /// Whether installation succeeded
        /// </summary>
        public required bool Success { get; init; }

        /// <summary>
        /// Error message if failed
        /// </summary>
        public string? Error { get; init; }

        /// <summary>
        /// List of patches that were installed
        /// </summary>
        public List<string> InstalledPatches { get; init; } = new();

        /// <summary>
        /// Backup information (if backup was created)
        /// </summary>
        public BackupInfo? Backup { get; init; }

        /// <summary>
        /// Detected game version
        /// </summary>
        public GameVersion? DetectedVersion { get; init; }

        /// <summary>
        /// Path to generated patch_config.toml
        /// </summary>
        public string? ConfigPath { get; init; }

        /// <summary>
        /// Additional messages
        /// </summary>
        public List<string> Messages { get; init; } = new();
    }

    public PatchApplicator(PatchRepository repository)
    {
        _repository = repository ?? throw new ArgumentNullException(nameof(repository));
    }

    /// <summary>
    /// Whether a patch's module is extracted and named in patch_config.toml, given how
    /// many hooks its hooks file declares and the hooks its options left installed.
    /// </summary>
    /// <remarks>
    /// A patch whose hooks file declares no hooks is DLL-only: KotorPatcher loads its
    /// module for its own sake, and its code runs. A patch whose options turned off every
    /// one of its hooks is not; it is decided from the hooks before selection, so turning
    /// options off never starts loading a module that the same patch with one SIMPLE hook
    /// left on would not load. Its options are still recorded in the configs folder.
    /// </remarks>
    public static bool InstallsModule(int declaredHookCount, IReadOnlyCollection<Hook> installedHooks) =>
        declaredHookCount == 0 || installedHooks.Count > 0;

    /// <summary>
    /// Installs patches to a game
    /// </summary>
    /// <param name="options">Installation options</param>
    /// <returns>Installation result</returns>
    public InstallResult InstallPatches(InstallOptions options)
    {
        var messages = new List<string>();
        BackupInfo? backup = null;

        try
        {
            // Step 1: Validate inputs
            messages.Add("Step 1/7: Validating inputs...");
            if (!File.Exists(options.GameExePath))
            {
                return new InstallResult
                {
                    Success = false,
                    Error = $"Game executable not found: {options.GameExePath}",
                    Messages = messages
                };
            }

            var gameDir = Path.GetDirectoryName(options.GameExePath);
            if (gameDir == null)
            {
                return new InstallResult
                {
                    Success = false,
                    Error = "Invalid game executable path",
                    Messages = messages
                };
            }

            // Step 2: Detect game version
            messages.Add("Step 2/7: Detecting game version...");
            var versionResult = GameDetector.DetectVersion(
                options.GameExePath,
                allowManagedInstallState: true,
                requireKnownManagedStateHash: true);
            if (!versionResult.Success || versionResult.Data == null)
            {
                return new InstallResult
                {
                    Success = false,
                    Error = $"Failed to detect game version: {versionResult.Error}",
                    Messages = messages
                };
            }

            var gameVersion = versionResult.Data;
            if (versionResult.Messages.Count > 0)
            {
                messages.AddRange(versionResult.Messages.Select(m => $"  {m}"));
            }
            else
            {
                messages.Add($"  Detected: {gameVersion.DisplayName}");
            }

            // Step 3: Load and validate patches
            messages.Add("Step 3/7: Loading and validating patches...");
            var patchEntries = new Dictionary<string, PatchRepository.PatchEntry>();
            foreach (var patchId in options.PatchIds)
            {
                var patchResult = _repository.GetPatch(patchId);
                if (!patchResult.Success || patchResult.Data == null)
                {
                    return new InstallResult
                    {
                        Success = false,
                        Error = $"Patch not found: {patchId}",
                        DetectedVersion = gameVersion,
                        Messages = messages
                    };
                }

                patchEntries[patchId] = patchResult.Data;
            }

            // Validate all patches
            var manifests = patchEntries.Values.Select(e => e.Manifest).ToList();
            var patchDict = patchEntries.ToDictionary(kv => kv.Key, kv => kv.Value.Manifest);

            // Check dependencies
            var depResult = DependencyValidator.ValidateDependencies(patchDict, options.PatchIds);
            if (!depResult.Success)
            {
                return new InstallResult
                {
                    Success = false,
                    Error = depResult.Error,
                    DetectedVersion = gameVersion,
                    Messages = messages
                };
            }

            // Check for conflicts
            var conflictResult = DependencyValidator.ValidateNoConflicts(patchDict, options.PatchIds);
            if (!conflictResult.Success)
            {
                return new InstallResult
                {
                    Success = false,
                    Error = conflictResult.Error,
                    DetectedVersion = gameVersion,
                    Messages = messages
                };
            }

            // Check game version compatibility
            var versionCheckResult = GameVersionValidator.ValidateAllPatchesSupported(manifests, gameVersion);
            if (!versionCheckResult.Success)
            {
                return new InstallResult
                {
                    Success = false,
                    Error = versionCheckResult.Error,
                    DetectedVersion = gameVersion,
                    Messages = messages
                };
            }

            // Load version-specific hooks before validating or applying anything.
            // PatchEntry.Hooks is populated during repository scan and is intentionally not
            // version-specific; using it here causes multi-version static patches to always
            // use whichever hooks file appears first in the archive.
            var hooksByPatch = new Dictionary<string, List<Hook>>();
            foreach (var patchId in options.PatchIds)
            {
                var hooksResult = _repository.LoadHooksForVersion(patchId, gameVersion.Hash);
                if (!hooksResult.Success || hooksResult.Data == null)
                {
                    return new InstallResult
                    {
                        Success = false,
                        Error = $"Failed to load hooks for {patchId}: {hooksResult.Error}",
                        DetectedVersion = gameVersion,
                        Messages = messages
                    };
                }

                hooksByPatch[patchId] = hooksResult.Data;
                foreach (var message in hooksResult.Messages)
                {
                    messages.Add($"  {patchId}: {message}");
                }
            }

            // Resolve patch options: check every hook's condition against the options its
            // manifest declares, then keep only the hooks whose condition holds for the
            // chosen values. Everything from here on, conflict checks included, sees the
            // hooks that will actually be installed.
            var optionsByPatch = new Dictionary<string, Dictionary<string, string>>();
            // How many hooks each patch's hooks file declares, before options select any:
            // what makes a patch DLL-only (see InstallsModule).
            var declaredHookCounts = hooksByPatch.ToDictionary(p => p.Key, p => p.Value.Count);
            var strayOptions = options.OptionValues.Keys.FirstOrDefault(id => !patchEntries.ContainsKey(id));
            if (strayOptions != null)
            {
                return new InstallResult
                {
                    Success = false,
                    Error = $"Options were given for {strayOptions}, which is not being installed",
                    DetectedVersion = gameVersion,
                    Messages = messages
                };
            }

            foreach (var patchId in options.PatchIds)
            {
                var manifest = patchEntries[patchId].Manifest;
                var conditionResult = OptionValidator.ValidateHookConditions(manifest, hooksByPatch[patchId]);
                if (!conditionResult.Success)
                {
                    return new InstallResult
                    {
                        Success = false,
                        Error = conditionResult.Error,
                        DetectedVersion = gameVersion,
                        Messages = messages
                    };
                }

                options.OptionValues.TryGetValue(patchId, out var chosen);
                var valuesResult = OptionValidator.ResolveValues(manifest, chosen);
                if (!valuesResult.Success || valuesResult.Data == null)
                {
                    return new InstallResult
                    {
                        Success = false,
                        Error = valuesResult.Error,
                        DetectedVersion = gameVersion,
                        Messages = messages
                    };
                }

                optionsByPatch[patchId] = valuesResult.Data;
                if (valuesResult.Data.Count > 0)
                {
                    var before = hooksByPatch[patchId].Count;
                    hooksByPatch[patchId] = OptionValidator.SelectHooks(hooksByPatch[patchId], valuesResult.Data);
                    messages.Add($"  {patchId}: options " +
                                 string.Join(", ", valuesResult.Data.Select(kv => $"{kv.Key}={kv.Value}")) +
                                 $" ({hooksByPatch[patchId].Count} of {before} hooks)");
                }
            }

            // Check for hook conflicts using only hooks that apply to the detected version.
            var hookConflictResult = HookValidator.ValidateMultiPatchHooks(hooksByPatch);
            if (!hookConflictResult.Success)
            {
                return new InstallResult
                {
                    Success = false,
                    Error = hookConflictResult.Error,
                    DetectedVersion = gameVersion,
                    Messages = messages
                };
            }

            // The parameter sources a hook names have to be ones this build's wrapper
            // generator can read, which is not knowable until the build is.
            var parameterSourceResult = HookValidator.ValidateParameterSources(
                hooksByPatch, gameVersion.Architecture);
            if (!parameterSourceResult.Success)
            {
                return new InstallResult
                {
                    Success = false,
                    Error = parameterSourceResult.Error,
                    DetectedVersion = gameVersion,
                    Messages = messages
                };
            }

            // Read the hooks' addresses out of the game and check they hold what the patch expects.
            // A hook aimed at the wrong address passes every check above, then does nothing once the
            // game runs, with no sign to the person who installed it that anything went wrong. This
            // runs before the backup so a refusal leaves the game untouched.
            var hookTargetResult = HookTargetValidator.VerifyAgainstExecutable(
                options.GameExePath, hooksByPatch);
            if (!hookTargetResult.Success)
            {
                return new InstallResult
                {
                    Success = false,
                    Error = hookTargetResult.Error,
                    DetectedVersion = gameVersion,
                    Messages = messages
                };
            }

            messages.AddRange(hookTargetResult.Messages.Select(m => $"  {m}"));

            // Calculate install order
            var orderResult = DependencyValidator.CalculateInstallOrder(patchDict, options.PatchIds);
            if (!orderResult.Success || orderResult.Data == null)
            {
                return new InstallResult
                {
                    Success = false,
                    Error = $"Failed to calculate install order: {orderResult.Error}",
                    DetectedVersion = gameVersion,
                    Messages = messages
                };
            }

            var installOrder = orderResult.Data;
            messages.Add($"  Install order: {string.Join(" -> ", installOrder)}");

            // Step 4: Create backup
            if (options.CreateBackup)
            {
                messages.Add("Step 4/7: Creating backup...");
                var backupResult = BackupManager.CreateBackup(
                    options.GameExePath,
                    gameVersion,
                    options.PatchIds
                );

                if (!backupResult.Success || backupResult.Data == null)
                {
                    return new InstallResult
                    {
                        Success = false,
                        Error = $"Failed to create backup: {backupResult.Error}",
                        DetectedVersion = gameVersion,
                        Messages = messages
                    };
                }

                backup = backupResult.Data;
                messages.Add($"  Backup created: {Path.GetFileName(backup.BackupPath)}");
            }
            else
            {
                messages.Add("Step 4/7: Skipping backup (disabled)");
            }

            // Step 4.5: Apply STATIC hooks to executable
            messages.Add("Step 4.5/8: Applying static hooks...");

            // Collect all static hooks from all patches for the detected game version.
            var allStaticHooks = new List<Hook>();
            foreach (var patchId in installOrder)
            {
                var hooks = hooksByPatch[patchId];
                var staticHooks = hooks.Where(h => h.Type == HookType.Static).ToList();

                if (staticHooks.Count > 0)
                {
                    allStaticHooks.AddRange(staticHooks);
                    messages.Add($"  {patchId}: {staticHooks.Count} static hook(s)");
                }
            }

            // Apply all static hooks. StaticHookApplicator maps the addresses for the executable's
            // format, so this works on a native Linux ELF as well as a Windows PE. Order matters: this
            // runs before the DT_NEEDED injection (Step 4.6), which is address-preserving, so the .text
            // bytes patched here survive that rewrite.
            if (allStaticHooks.Count > 0)
            {
                var applyResult = StaticHookApplicator.ApplyStaticHooks(
                    options.GameExePath,
                    allStaticHooks);

                if (!applyResult.Success)
                {
                    // Restore backup on failure
                    if (backup != null)
                    {
                        var restoreResult = BackupManager.RestoreBackup(backup);
                        if (restoreResult.Success)
                        {
                            messages.Add("  Backup restored after static hook failure");
                        }
                    }

                    return new InstallResult
                    {
                        Success = false,
                        Error = $"Static hook application failed: {applyResult.Error}",
                        DetectedVersion = gameVersion,
                        Backup = backup,
                        Messages = messages
                    };
                }

                messages.Add($"  Successfully applied {allStaticHooks.Count} static hook(s) to executable");
            }
            else
            {
                messages.Add("  No static hooks to apply");
            }

            // Step 5: Extract patch DLLs (for DETOUR hooks and DLL-only patches)
            messages.Add("Step 5/8: Extracting patch DLLs...");
            var patchesDir = Path.Combine(gameDir, "patches");

            var extractedDlls = new Dictionary<string, string>();
            foreach (var patchId in installOrder)
            {
                var hooks = hooksByPatch[patchId];
                if (!InstallsModule(declaredHookCounts[patchId], hooks))
                {
                    messages.Add($"  Skipped: {patchId} (its options leave none of its hooks installed)");
                    continue;
                }

                var hasDetourHooks = hooks.Any(h => h.Type == HookType.Detour);
                var hasDllOnlyPatch = declaredHookCounts[patchId] == 0; // DLL-only patch (no hooks)

                // Try to extract DLL if it exists (supports DETOUR hooks and DLL-only patches)
                var extractResult = _repository.ExtractPatchDll(patchId, patchesDir, gameVersion);

                if (extractResult.Success && extractResult.Data != null)
                {
                    // DLL exists and was extracted successfully
                    Directory.CreateDirectory(patchesDir);
                    extractedDlls[patchId] = extractResult.Data;

                    // The name carries the extension the game's platform uses, so it is read
                    // back rather than rebuilt from the patch id.
                    var extractedName = Path.GetFileName(extractResult.Data);
                    if (hasDetourHooks)
                    {
                        messages.Add($"  Extracted: {extractedName} (DETOUR hooks)");
                    }
                    else if (hasDllOnlyPatch)
                    {
                        messages.Add($"  Extracted: {extractedName} (DLL-only patch)");
                    }
                    else
                    {
                        messages.Add($"  Extracted: {extractedName}");
                    }
                }
                else
                {
                    // No DLL in archive
                    if (hasDetourHooks)
                    {
                        // DETOUR hooks require a DLL - this is an error
                        if (backup != null)
                        {
                            BackupManager.RestoreBackup(backup);
                        }

                        return new InstallResult
                        {
                            Success = false,
                            Error = $"Failed to extract {patchId}: {extractResult.Error}. Its DETOUR " +
                                    "hooks need windows_x86.dll.",
                            DetectedVersion = gameVersion,
                            Backup = backup,
                            Messages = messages
                        };
                    }
                    else
                    {
                        // SIMPLE, REPLACE, or STATIC-only patch - no DLL required
                        messages.Add($"  Skipped: {patchId} (no module required for selected hooks)");
                    }
                }
            }

            // Step 6: Generate patch_config.toml
            messages.Add("Step 6/8: Generating patch_config.toml...");
            var config = new PatchConfig
            {
                TargetVersionSha = gameVersion.Hash  // NEW: Set target version SHA
            };

            foreach (var patchId in installOrder)
            {
                var hooks = hooksByPatch[patchId];

                // Get DLL path if this patch has DETOUR hooks, otherwise use empty string
                var dllPath = extractedDlls.ContainsKey(patchId)
                    ? Path.GetRelativePath(gameDir, extractedDlls[patchId])
                    : string.Empty;

                config.AddPatch(patchId, dllPath, hooks, optionsByPatch[patchId]);
            }

            var configPath = Path.Combine(gameDir, "patch_config.toml");
            var configResult = ConfigGenerator.GenerateConfigFile(config, configPath);
            if (!configResult.Success)
            {
                // Cleanup on failure
                if (backup != null)
                {
                    BackupManager.RestoreBackup(backup);
                }

                return new InstallResult
                {
                    Success = false,
                    Error = $"Failed to generate config: {configResult.Error}",
                    DetectedVersion = gameVersion,
                    Backup = backup,
                    Messages = messages
                };
            }

            messages.Add($"  Config generated: patch_config.toml");

            var optionsResult = PatchOptionsIni.WriteFiles(config, gameDir);
            if (!optionsResult.Success)
            {
                if (backup != null)
                {
                    BackupManager.RestoreBackup(backup);
                }

                return new InstallResult
                {
                    Success = false,
                    Error = optionsResult.Error,
                    DetectedVersion = gameVersion,
                    Backup = backup,
                    Messages = messages
                };
            }

            if (config.Patches.Any(p => p.Options.Count > 0))
            {
                messages.Add($"  Options recorded: {PatchOptionsIni.DirectoryName}");
            }

            // Step 7: Copy patcher DLL
            messages.Add("Step 7/8: Installing patcher DLL...");

            var destPath = Path.Combine(gameDir, "SpiderManPatcher.Runtime.dll");
            if (!string.IsNullOrEmpty(options.PatcherDllPath))
            {
                if (!File.Exists(options.PatcherDllPath))
                {
                    return new InstallResult
                    {
                        Success = false,
                        Error = $"SpiderManPatcher.Runtime.dll not found at: {options.PatcherDllPath}",
                        DetectedVersion = gameVersion,
                        Backup = backup,
                        Messages = messages
                    };
                }

                // If the module is already present, skip the copy step
                if (PathHelpers.SamePath(options.PatcherDllPath, destPath))
                {
                    messages.Add("  SpiderManPatcher.Runtime.dll already in place");
                }
                else
                {
                    try
                    {
                        File.Copy(options.PatcherDllPath, destPath, overwrite: true);
                    }
                    catch (Exception ex)
                    {
                        if (backup != null)
                        {
                            BackupManager.RestoreBackup(backup);
                        }

                        return new InstallResult
                        {
                            Success = false,
                            Error = $"Failed to copy SpiderManPatcher.Runtime.dll to game directory: {ex.Message}",
                            DetectedVersion = gameVersion,
                            Backup = backup,
                            Messages = messages
                        };
                    }

                    messages.Add("  Copied SpiderManPatcher.Runtime.dll to game directory");
                }
            }
            else
            {
                messages.Add("  Warning: patcher DLL path not provided");
            }

            // Fail loudly if the module never made it to the destination
            if (!File.Exists(destPath))
            {
                if (backup != null)
                {
                    BackupManager.RestoreBackup(backup);
                }

                return new InstallResult
                {
                    Success = false,
                    Error = "SpiderManPatcher.Runtime.dll is missing from the game directory after installation.",
                    DetectedVersion = gameVersion,
                    Backup = backup,
                    Messages = messages
                };
            }



            var stateResult = InstallStateManager.SaveOrUpdate(
                options.GameExePath,
                gameVersion,
                installOrder);
            if (stateResult.Success)
            {
                messages.Add($"  {stateResult.Messages.FirstOrDefault() ?? "Managed install state saved"}");
            }
            else
            {
                messages.Add($"  ⚠️ Warning: Failed to save managed install state: {stateResult.Error}");
            }

            return new InstallResult
            {
                Success = true,
                InstalledPatches = installOrder,
                Backup = backup,
                DetectedVersion = gameVersion,
                ConfigPath = configPath,
                Messages = messages
            };
        }
        catch (Exception ex)
        {
            // Restore backup on unexpected failure
            if (backup != null)
            {
                try
                {
                    BackupManager.RestoreBackup(backup);
                    messages.Add("Restored backup after failure");
                }
                catch
                {
                    // Ignore backup restoration errors
                }
            }

            return new InstallResult
            {
                Success = false,
                Error = $"Installation failed: {ex.Message}",
                Backup = backup,
                Messages = messages
            };
        }
    }
}
