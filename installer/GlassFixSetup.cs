using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;
using System.Windows.Forms;

[assembly: AssemblyTitle("Tablet Pro Radial Setup")]
[assembly: AssemblyProduct("Tablet Pro Radial")]
[assembly: AssemblyVersion("@APP_VERSION@.0")]
[assembly: AssemblyFileVersion("@APP_VERSION@.0")]
internal static class GlassFixSetup
{
    [STAThread]
    static int Main(string[] args)
    {
        bool verify = args.Length == 2 && args[0] == "--verify";
        int waitPid = 0;
        bool updating = args.Length == 2 && args[0] == "--wait-for-pid" && Int32.TryParse(args[1], out waitPid) && waitPid > 0;
        if (args.Length != 0 && !verify && !updating) return 2;
        string temporaryRoot = Path.GetFullPath(Path.GetTempPath());
        string stage = Path.Combine(temporaryRoot, "TabletPro-Setup-" + Guid.NewGuid().ToString("N"));
        Application.EnableVisualStyles();
        try
        {
            if (!Environment.Is64BitOperatingSystem) throw new Exception("This build requires 64-bit Windows.");
            if (updating)
            {
                Process previous = null;
                try { previous = Process.GetProcessById(waitPid); } catch (ArgumentException) { }
                if (previous != null) using (previous)
                {
                    if (!String.Equals(previous.ProcessName, "tabletpro_radial", StringComparison.OrdinalIgnoreCase))
                        throw new Exception("The update was not launched by Tablet Pro Radial.");
                    if (!previous.WaitForExit(30000)) throw new Exception("Radial is still closing. Close it, then run this installer again.");
                }
            }
            if (!verify && MessageBox.Show("Install Tablet Pro Radial @APP_VERSION@?\n\nIncludes a system tray icon, GitHub updates, and the Glass background fix.\nYour saved menus and settings will be kept.",
                "Tablet Pro Radial", MessageBoxButtons.OKCancel, MessageBoxIcon.Information) != DialogResult.OK) return 0;
            Directory.CreateDirectory(stage);
            foreach (string resource in new[] { "payload.zip", "install.ps1" })
                using (Stream input = Assembly.GetExecutingAssembly().GetManifestResourceStream(resource))
                using (FileStream output = new FileStream(Path.Combine(stage, resource), FileMode.CreateNew))
                    input.CopyTo(output);
            using (SHA256 sha = SHA256.Create())
            using (FileStream data = File.OpenRead(Path.Combine(stage, "payload.zip")))
                if (BitConverter.ToString(sha.ComputeHash(data)).Replace("-", "").ToLowerInvariant() != PayloadHash.Value)
                    throw new Exception("The download is incomplete or damaged. Download the installer again.");
            var start = new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "WindowsPowerShell", "v1.0", "powershell.exe"));
            start.Arguments = "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + Path.Combine(stage, "install.ps1") + "\"";
            if (verify) start.Arguments += " -VerifyOnly -Destination \"" + Path.GetFullPath(args[1]) + "\"";
            start.UseShellExecute = false;
            start.CreateNoWindow = true;
            start.RedirectStandardOutput = true;
            start.RedirectStandardError = true;
            using (var process = Process.Start(start))
            {
                var output = process.StandardOutput.ReadToEndAsync();
                var errors = process.StandardError.ReadToEndAsync();
                process.WaitForExit();
                if (process.ExitCode != 0) throw new Exception(errors.Result + output.Result);
            }
            if (!verify && MessageBox.Show("Tablet Pro Radial @APP_VERSION@ is installed.\n\nOpen it now? Look for its blue radial icon beside the clock (or under the hidden-icons arrow).", "Tablet Pro Radial", MessageBoxButtons.YesNo, MessageBoxIcon.Information) == DialogResult.Yes)
            {
                string installed = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", "TabletProRadial", "@APP_VERSION@");
                Process.Start(new ProcessStartInfo(Path.Combine(installed, "tabletpro_radial.exe"), "--radial") { WorkingDirectory = installed, UseShellExecute = true });
            }
            return 0;
        }
        catch (Exception error)
        {
            if (!verify) MessageBox.Show(error.Message, "Tablet Pro Radial setup could not finish", MessageBoxButtons.OK, MessageBoxIcon.Error);
            else Console.Error.WriteLine(error.ToString());
            return 1;
        }
        finally
        {
            // Only this invocation's newly allocated temporary folder is removed.
            string resolved = Path.GetFullPath(stage);
            if (resolved.StartsWith(temporaryRoot.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)
                && Path.GetFileName(resolved).StartsWith("TabletPro-Setup-", StringComparison.Ordinal)
                && Directory.Exists(resolved) && (File.GetAttributes(resolved) & FileAttributes.ReparsePoint) == 0)
            {
                try { Directory.Delete(resolved, true); } catch (IOException) { }
            }
        }
    }
}
