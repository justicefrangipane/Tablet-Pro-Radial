using System;
using System.Diagnostics;
using System.Drawing;
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
        SetupOptions options;
        if (!SetupOptions.TryParse(args, out options)) return 2;
        string temporaryRoot = Path.GetFullPath(Path.GetTempPath());
        string stage = Path.Combine(temporaryRoot, "TabletPro-Setup-" + Guid.NewGuid().ToString("N"));
        Application.EnableVisualStyles();
        try
        {
            if (!Environment.Is64BitOperatingSystem) throw new Exception("This build requires 64-bit Windows.");
            using (var operations = new WindowsSetup(stage, options.Updating)) SetupWorkflow.Run(options, operations);
            return 0;
        }
        catch (Exception error)
        {
            if (!options.Verify) MessageBox.Show(error.Message, "Tablet Pro Radial setup could not finish", MessageBoxButtons.OK, MessageBoxIcon.Error);
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
                try { Directory.Delete(resolved, true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
            }
        }
    }
}

internal sealed class WindowsSetup : ISetupOperations, IDisposable
{
    readonly string stage;
    Form progress;
    Label status;

    internal WindowsSetup(string stage, bool updating)
    {
        this.stage = stage;
        if (!updating) return;
        progress = new Form { Text = "Updating Tablet Pro Radial", ClientSize = new Size(430, 132),
            StartPosition = FormStartPosition.CenterScreen, FormBorderStyle = FormBorderStyle.FixedDialog,
            ControlBox = false, Font = new Font("Segoe UI", 10), BackColor = Color.FromArgb(41, 33, 28), ForeColor = Color.FromArgb(255, 229, 189) };
        progress.Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
        status = new Label { Left = 22, Top = 20, Width = 386, Height = 55, Text = "Closing Radial safely…" };
        progress.Controls.Add(status);
        progress.Controls.Add(new ProgressBar { Left = 22, Top = 91, Width = 386, Height = 16, Style = ProgressBarStyle.Marquee });
        progress.Show(); Application.DoEvents();
    }
    void ShowProgress(string text) { if (status != null) { status.Text = text; Application.DoEvents(); } }
    public void WaitForExit(int pid)
    {
        Process previous = null;
        try { previous = Process.GetProcessById(pid); } catch (ArgumentException) { }
        if (previous == null) return;
        using (previous)
        {
            try
            {
                if (previous.HasExited) return;
                if (!String.Equals(previous.ProcessName, "tabletpro_radial", StringComparison.OrdinalIgnoreCase))
                    throw new Exception("The update was not launched by Tablet Pro Radial.");
            }
            catch (InvalidOperationException) { if (previous.HasExited) return; throw; }
            var elapsed = Stopwatch.StartNew();
            while (!previous.WaitForExit(100))
            {
                Application.DoEvents();
                if (elapsed.ElapsedMilliseconds >= 30000) throw new Exception("Radial is still closing. Your installed version has not been changed. Try the update again after closing Radial.");
            }
        }
    }
    public bool ConfirmInstall()
    {
        return MessageBox.Show("Install Tablet Pro Radial @APP_VERSION@?\n\nYour saved menus and settings will be kept.",
            "Tablet Pro Radial", MessageBoxButtons.OKCancel, MessageBoxIcon.Information) == DialogResult.OK;
    }
    public void Install(bool verify, string destination)
    {
        ShowProgress("Installing Radial @APP_VERSION@…\nYour menus and settings are being kept.");
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
        if (verify) start.Arguments += " -VerifyOnly -Destination \"" + Path.GetFullPath(destination) + "\"";
        start.UseShellExecute = false; start.CreateNoWindow = true;
        start.RedirectStandardOutput = true; start.RedirectStandardError = true;
        using (var process = Process.Start(start))
        {
            var output = process.StandardOutput.ReadToEndAsync();
            var errors = process.StandardError.ReadToEndAsync();
            while (!process.WaitForExit(100)) Application.DoEvents();
            if (process.ExitCode != 0) throw new Exception(errors.Result + output.Result);
        }
    }
    public bool ConfirmOpen()
    {
        return MessageBox.Show("Tablet Pro Radial @APP_VERSION@ is installed.\n\nOpen it now?", "Tablet Pro Radial",
            MessageBoxButtons.YesNo, MessageBoxIcon.Information) == DialogResult.Yes;
    }
    public void Open(string mode)
    {
        ShowProgress("Opening Radial…");
        string installed = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", "TabletProRadial", "@APP_VERSION@");
        Process.Start(new ProcessStartInfo(Path.Combine(installed, "tabletpro_radial.exe"), "--" + mode) { WorkingDirectory = installed, UseShellExecute = true });
    }
    public void Dispose() { if (progress != null) { progress.Dispose(); progress = null; status = null; } }
}
