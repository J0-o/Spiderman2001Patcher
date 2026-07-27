using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using KPatchCore.Models;

namespace KPatchCore.Launcher;

/// <summary>
/// Internal implementation of DLL injection for game processes using Windows API
/// </summary>
internal static class ProcessInjector
{
    /// <summary>
    /// Launches a game executable with DLL injection.
    /// Uses CREATE_SUSPENDED to inject before the game starts
    /// </summary>
    internal static LaunchResult Launch(
        string gameExePath,
        string dllPath,
        string? commandLineArgs)
    {
        try
        {
            var absGamePath = Path.GetFullPath(gameExePath);
            var absDllPath = Path.GetFullPath(dllPath);

            var si = new Win32.STARTUPINFO
            {
                cb = Marshal.SizeOf(typeof(Win32.STARTUPINFO))
            };

            var pi = new Win32.PROCESS_INFORMATION();

            var commandLine = $"\"{absGamePath}\"";
            if (!string.IsNullOrWhiteSpace(commandLineArgs))
            {
                commandLine += $" {commandLineArgs}";
            }

            // Create the process suspended
            var success = Win32.CreateProcess(
                absGamePath,
                commandLine,
                IntPtr.Zero,
                IntPtr.Zero,
                false,
                Win32.CREATE_SUSPENDED,
                IntPtr.Zero,
                Path.GetDirectoryName(absGamePath),
                ref si,
                out pi);

            if (!success)
            {
                var error = Marshal.GetLastWin32Error();
                return LaunchResult.Fail(
                    $"Failed to create process (error {error}): {gameExePath}");
            }

            try
            {
                // Inject the DLL into the suspended process
                var injectResult = InjectDllIntoProcess(pi.hProcess, absDllPath);

                if (!injectResult.Success)
                {
                    Process.GetProcessById(pi.dwProcessId).Kill();
                    return LaunchResult.Fail(
                        $"DLL injection failed: {injectResult.Error}");
                }

                // Resume the main thread
                var resumeResult = Win32.ResumeThread(pi.hThread);
                if (resumeResult == unchecked((uint)-1))
                {
                    var error = Marshal.GetLastWin32Error();
                    Process.GetProcessById(pi.dwProcessId).Kill();
                    return LaunchResult.Fail(
                        $"Failed to resume thread (error {error})");
                }

                var process = Process.GetProcessById(pi.dwProcessId);

                return LaunchResult.Ok(process);
            }
            finally
            {
                if (pi.hProcess != IntPtr.Zero) Win32.CloseHandle(pi.hProcess);
                if (pi.hThread != IntPtr.Zero) Win32.CloseHandle(pi.hThread);
            }
        }
        catch (Exception ex)
        {
            return LaunchResult.Fail($"Launch failed: {ex.Message}");
        }
    }

    /// <summary>
    /// Internal method to inject DLL into an open process handle
    /// </summary>
    private static PatchResult InjectDllIntoProcess(IntPtr hProcess, string dllPath)
    {
        try
        {
            Console.WriteLine($"[Injector] Injecting: {dllPath}");

            var hKernel32 = Win32.GetModuleHandle("kernel32.dll");
            if (hKernel32 == IntPtr.Zero)
            {
                var msg = "Failed to get kernel32.dll module handle";
                Console.WriteLine($"[Injector] ERROR: {msg}");
                return PatchResult.Fail(msg);
            }
            Console.WriteLine($"[Injector] kernel32.dll handle: 0x{hKernel32:X}");

            var pLoadLibraryA = Win32.GetProcAddress(hKernel32, "LoadLibraryA");
            if (pLoadLibraryA == IntPtr.Zero)
            {
                var msg = "Failed to get LoadLibraryA address";
                Console.WriteLine($"[Injector] ERROR: {msg}");
                return PatchResult.Fail(msg);
            }
            Console.WriteLine($"[Injector] LoadLibraryA address: 0x{pLoadLibraryA:X}");

            // Allocate memory in the target process for the DLL path
            var dllPathBytes = Encoding.ASCII.GetBytes(dllPath + '\0');
            Console.WriteLine($"[Injector] Allocating {dllPathBytes.Length} bytes in target process...");

            var pDllPath = Win32.VirtualAllocEx(
                hProcess,
                IntPtr.Zero,
                (uint)dllPathBytes.Length,
                Win32.MEM_COMMIT | Win32.MEM_RESERVE,
                Win32.PAGE_READWRITE);

            if (pDllPath == IntPtr.Zero)
            {
                var error = Marshal.GetLastWin32Error();
                var msg = $"Failed to allocate memory in target process (error {error})";
                Console.WriteLine($"[Injector] ERROR: {msg}");
                return PatchResult.Fail(msg);
            }
            Console.WriteLine($"[Injector] Allocated memory at: 0x{pDllPath:X}");

            // Write the DLL path to the allocated memory
            Console.WriteLine($"[Injector] Writing DLL path to target process memory...");
            var writeSuccess = Win32.WriteProcessMemory(
                hProcess,
                pDllPath,
                dllPathBytes,
                (uint)dllPathBytes.Length,
                out var bytesWritten);

            if (!writeSuccess || bytesWritten.ToUInt32() != dllPathBytes.Length)
            {
                var error = Marshal.GetLastWin32Error();
                var msg = $"Failed to write DLL path to target process (error {error})";
                Console.WriteLine($"[Injector] ERROR: {msg}");
                return PatchResult.Fail(msg);
            }
            Console.WriteLine($"[Injector] Wrote {bytesWritten} bytes successfully");

            // Create a remote thread that calls LoadLibraryA with the DLL path
            Console.WriteLine($"[Injector] Creating remote thread to call LoadLibraryA...");
            var hThread = Win32.CreateRemoteThread(
                hProcess,
                IntPtr.Zero,
                0,
                pLoadLibraryA,
                pDllPath,
                0,
                out var threadId);

            if (hThread == IntPtr.Zero)
            {
                var error = Marshal.GetLastWin32Error();
                var msg = $"Failed to create remote thread (error {error})";
                Console.WriteLine($"[Injector] ERROR: {msg}");
                return PatchResult.Fail(msg);
            }
            Console.WriteLine($"[Injector] Remote thread created with ID: {threadId}");

            // Wait for the thread to complete (LoadLibrary to finish)
            Console.WriteLine($"[Injector] Waiting for remote thread to complete...");
            var waitResult = Win32.WaitForSingleObject(hThread, Win32.INFINITE);
            Win32.CloseHandle(hThread);

            if (waitResult != 0) // 0 = WAIT_OBJECT_0
            {
                var msg = $"Remote thread did not complete successfully (wait result: {waitResult})";
                Console.WriteLine($"[Injector] ERROR: {msg}");
                return PatchResult.Fail(msg);
            }

            Console.WriteLine($"[Injector] SUCCESS: DLL injected successfully!");
            return PatchResult.Ok($"Successfully injected {Path.GetFileName(dllPath)}");
        }
        catch (Exception ex)
        {
            var msg = $"DLL injection failed: {ex.Message}";
            Console.WriteLine($"[Injector] EXCEPTION: {msg}");
            return PatchResult.Fail(msg);
        }
    }
}
