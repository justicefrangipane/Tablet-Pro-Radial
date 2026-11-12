using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Threading;

internal static class PayloadHash { internal const string Value = "test-fixture-unused"; }
internal sealed class FakeSetup : ISetupOperations
{
    internal readonly List<string> Calls = new List<string>();
    internal bool InstallAllowed = true, OpenAllowed = true;
    internal string FailAt;
    void Call(string value) { Calls.Add(value); if (FailAt == value) throw new InvalidOperationException("fixture failure"); }
    public void WaitForExit(int pid) { Call("wait:" + pid); }
    public bool ConfirmInstall() { Call("confirm-install"); return InstallAllowed; }
    public void Install(bool verify, string destination) { Call(verify ? "verify:" + destination : "install"); }
    public bool ConfirmOpen() { Call("confirm-open"); return OpenAllowed; }
    public void Open(string mode) { Call("open:" + mode); }
}
internal static class InstallerTests
{
    static int checks;
    static void Check(bool value, string message) { ++checks; if (!value) throw new Exception(message); }
    static SetupOptions Parse(params string[] args) { SetupOptions result; Check(SetupOptions.TryParse(args, out result), "valid arguments"); return result; }
    [STAThread]
    static int Main(string[] args)
    {
        if (args.Length == 1 && args[0] == "--parent-fixture") { Thread.Sleep(600); return 0; }
        try
        {
            foreach (string mode in new[] { "radial", "docked", "editor" })
            {
                var fake = new FakeSetup();
                SetupWorkflow.Run(Parse("--wait-for-pid", "123", "--relaunch-mode", mode), fake);
                Check(String.Join(",", fake.Calls) == "wait:123,install,open:" + mode, "one-click waits, installs and reopens the same mode without prompts");
            }
            var legacy = new FakeSetup(); SetupWorkflow.Run(Parse("--wait-for-pid", "123"), legacy);
            Check(String.Join(",", legacy.Calls) == "wait:123,install,open:radial", "older app handoff also installs without extra prompts");
            foreach (string failure in new[] { "wait:123", "install" })
            {
                var fake = new FakeSetup { FailAt = failure }; bool failed = false;
                try { SetupWorkflow.Run(Parse("--wait-for-pid", "123"), fake); } catch (InvalidOperationException) { failed = true; }
                Check(failed && !fake.Calls.Exists(c => c.StartsWith("open:")), "failure never launches a partial installation");
                if (failure.StartsWith("wait")) Check(!fake.Calls.Contains("install"), "a running old app prevents installation");
            }
            var manual = new FakeSetup { InstallAllowed = false }; SetupWorkflow.Run(Parse(), manual);
            Check(String.Join(",", manual.Calls) == "confirm-install", "manual cancel does not install or open");
            manual = new FakeSetup { OpenAllowed = false }; SetupWorkflow.Run(Parse(), manual);
            Check(String.Join(",", manual.Calls) == "confirm-install,install,confirm-open", "manual setup retains its open choice");
            var verify = new FakeSetup(); SetupWorkflow.Run(Parse("--verify", "fixture"), verify);
            Check(String.Join(",", verify.Calls) == "verify:fixture", "package verification cannot install or launch");
            foreach (var invalid in new[] { new[] { "--wait-for-pid", "0" }, new[] { "--wait-for-pid", "-1" },
                new[] { "--wait-for-pid", "abc" }, new[] { "--wait-for-pid", "1", "--relaunch-mode", "cmd" },
                new[] { "--wait-for-pid", "1", "--unknown", "radial" }, new[] { "--verify", "" }, new[] { "--relaunch-mode", "radial" } })
            { SetupOptions unused; Check(!SetupOptions.TryParse(invalid, out unused), "invalid install arguments refused"); }
            using (var operations = new WindowsSetup("unused", false))
            {
                bool refused = false;
                try { operations.WaitForExit(Process.GetCurrentProcess().Id); } catch (Exception) { refused = true; }
                Check(refused, "unrelated processes cannot authorize an update");
                var fixture = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "tabletpro_radial.exe");
                using (var parent = Process.Start(new ProcessStartInfo(fixture, "--parent-fixture") { UseShellExecute = false, CreateNoWindow = true }))
                {
                    Check(!parent.HasExited, "fixture parent is running");
                    operations.WaitForExit(parent.Id);
                    Check(parent.HasExited, "real native wait completes only after app shutdown");
                    operations.WaitForExit(parent.Id); // An already-closed app is also a valid handoff.
                }
            }
            Console.WriteLine(checks + " installer workflow checks passed"); return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
