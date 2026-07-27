using KPatchCore.Applicators;

namespace KPatchCore.Managers;

/// <summary>
/// Headless patch discovery, installation, and launch facade.
/// </summary>
public sealed class PatchOrchestrator
{
    private readonly PatchRepository _repository;
    private readonly PatchApplicator _applicator;

    public PatchOrchestrator(string patchesDirectory)
    {
        _repository = new PatchRepository(patchesDirectory);
        _applicator = new PatchApplicator(_repository);
        _repository.ScanPatches();
    }

    public PatchApplicator.InstallResult InstallPatches(
        string gameExePath,
        IEnumerable<string> patchIds,
        bool createBackup = true,
        string? patcherDllPath = null)
    {
        return _applicator.InstallPatches(new PatchApplicator.InstallOptions
        {
            GameExePath = gameExePath,
            PatchIds = patchIds.ToList(),
            CreateBackup = createBackup,
            PatcherDllPath = patcherDllPath
        });
    }

    public IReadOnlyDictionary<string, PatchRepository.PatchEntry> GetAvailablePatches() =>
        _repository.GetAllPatches();

    public KPatchCore.Models.PatchResult RescanPatches() =>
        _repository.ScanPatches();
}
