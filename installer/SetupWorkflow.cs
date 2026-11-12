using System;

internal sealed class SetupOptions
{
    internal bool Verify, Updating;
    internal int WaitPid;
    internal string Destination, RelaunchMode = "radial";

    internal static bool TryParse(string[] args, out SetupOptions options)
    {
        options = new SetupOptions();
        if (args.Length == 0) return true;
        if (args.Length == 2 && args[0] == "--verify" && !String.IsNullOrWhiteSpace(args[1]))
        { options.Verify = true; options.Destination = args[1]; return true; }
        if ((args.Length != 2 && args.Length != 4) || args[0] != "--wait-for-pid"
            || !Int32.TryParse(args[1], out options.WaitPid) || options.WaitPid <= 0) return false;
        if (args.Length == 4)
        {
            if (args[2] != "--relaunch-mode" || (args[3] != "radial" && args[3] != "docked" && args[3] != "editor")) return false;
            options.RelaunchMode = args[3];
        }
        options.Updating = true; return true;
    }
}

internal interface ISetupOperations
{
    void WaitForExit(int pid);
    bool ConfirmInstall();
    void Install(bool verify, string destination);
    bool ConfirmOpen();
    void Open(string mode);
}

internal static class SetupWorkflow
{
    internal static void Run(SetupOptions options, ISetupOperations operations)
    {
        // Clicking Install in Radial is already the user's install/open choice.
        // The original two-argument handoff from older Radial versions also works.
        if (options.Updating) operations.WaitForExit(options.WaitPid);
        if (!options.Verify && !options.Updating && !operations.ConfirmInstall()) return;
        operations.Install(options.Verify, options.Destination);
        if (!options.Verify && (options.Updating || operations.ConfirmOpen())) operations.Open(options.RelaunchMode);
    }
}
