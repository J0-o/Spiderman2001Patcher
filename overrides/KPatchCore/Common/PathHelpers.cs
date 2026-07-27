namespace KPatchCore.Common;

/// <summary>
/// Path manipulation helpers with additional safety checks
/// </summary>
public static class PathHelpers
{
    /// <summary>
    /// Ensures a directory exists, creating it if necessary
    /// </summary>
    public static void EnsureDirectoryExists(string directoryPath)
    {
        if (!Directory.Exists(directoryPath))
        {
            Directory.CreateDirectory(directoryPath);
        }
    }

    /// <summary>
    /// Gets a unique backup filename for a file
    /// Example: "swkotor.exe" -> "swkotor.exe.backup.20241016_153045"
    /// </summary>
    public static string GetBackupPath(string originalPath)
    {
        var timestamp = DateTime.Now.ToString("yyyyMMdd_HHmmss");
        return $"{originalPath}.backup.{timestamp}";
    }

    /// <summary>
    /// Gets the most recent backup file for a given original file
    /// Returns null if no backup exists
    /// </summary>
    public static string? FindLatestBackup(string originalPath)
    {
        var directory = Path.GetDirectoryName(originalPath);
        var fileName = Path.GetFileName(originalPath);

        if (directory == null)
            return null;

        var pattern = $"{fileName}.backup.*";
        var backups = Directory.GetFiles(directory, pattern)
            .Where(f => !f.EndsWith(".json", StringComparison.OrdinalIgnoreCase)) // Exclude metadata files
            .OrderByDescending(f => File.GetCreationTime(f))
            .FirstOrDefault();

        return backups;
    }

    /// <summary>
    /// Determines whether two paths refer to the same file/directory location.
    /// Comparison is done on the fully-resolved paths, is case-insensitive
    /// (matching Windows and Wine filesystem semantics), and ignores a trailing
    /// directory separator. This is a path-string comparison, not a
    /// hardlink/symlink identity check.
    /// Returns false if either path is null or empty.
    /// </summary>
    public static bool SamePath(string? a, string? b)
    {
        if (string.IsNullOrEmpty(a) || string.IsNullOrEmpty(b))
            return false;

        static string Normalize(string p) =>
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(p));

        return string.Equals(Normalize(a), Normalize(b), StringComparison.OrdinalIgnoreCase);
    }

}
