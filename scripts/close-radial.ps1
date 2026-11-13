#requires -Version 5.1
[CmdletBinding(SupportsShouldProcess=$true,ConfirmImpact='Low')]
param([int]$ProcessId=0)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs\TabletProRadial'))+'\'
$all=@(Get-CimInstance Win32_Process -Filter "Name='tabletpro_radial.exe'")
$main=@($all | Where-Object { $_.CommandLine -notmatch '--pen-helper' -and (!$ProcessId -or $_.ProcessId -eq $ProcessId) })
if(!$main.Count){Write-Output 'Radial is already closed.';return}
if($main.Count -ne 1){throw 'Multiple Radial instances found. Specify the intended -ProcessId.'}
$target=$main[0]
$path=[IO.Path]::GetFullPath($target.ExecutablePath)
if(!$path.StartsWith($root,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($path) -ne 'tabletpro_radial.exe'){
    throw 'Refusing to close a process outside this user''s Radial installation.'
}
if(!$PSCmdlet.ShouldProcess("Radial PID $($target.ProcessId)",'Request graceful shutdown')){return}
if(-not ('RadialMaintenance.RestartManager' -as [type])){
Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using FILETIME = System.Runtime.InteropServices.ComTypes.FILETIME;
using System.Text;
namespace RadialMaintenance {
 public static class RestartManager {
  [StructLayout(LayoutKind.Sequential)] struct UniqueProcess { public uint Pid; public FILETIME Started; }
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] struct ProcessInfo {
   public UniqueProcess Process;
   [MarshalAs(UnmanagedType.ByValTStr,SizeConst=256)] public string AppName;
   [MarshalAs(UnmanagedType.ByValTStr,SizeConst=64)] public string ServiceName;
   public int AppType; public uint Status,SessionId;
   [MarshalAs(UnmanagedType.Bool)] public bool Restartable;
  }
  [DllImport("rstrtmgr.dll",CharSet=CharSet.Unicode)] static extern int RmStartSession(out uint session,uint flags,StringBuilder key);
  [DllImport("rstrtmgr.dll")] static extern int RmEndSession(uint session);
  [DllImport("rstrtmgr.dll",CharSet=CharSet.Unicode)] static extern int RmRegisterResources(uint session,uint files,string[] paths,uint apps,UniqueProcess[] processes,uint services,string[] names);
  [DllImport("rstrtmgr.dll",CharSet=CharSet.Unicode)] static extern int RmGetList(uint session,out uint needed,ref uint count,[In,Out] ProcessInfo[] apps,out uint reboot);
  [DllImport("rstrtmgr.dll")] static extern int RmShutdown(uint session,uint flags,IntPtr callback);
  [DllImport("kernel32.dll",SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)] static extern bool GetProcessTimes(IntPtr process,out FILETIME creation,out FILETIME exit,out FILETIME kernel,out FILETIME user);
  static void Check(int code,string step) { if(code!=0)throw new InvalidOperationException(step+" failed (Windows error "+code+"). No forced termination was requested."); }
  public static void Close(int pid,string expectedPath) {
   using(var process=Process.GetProcessById(pid)) {
    if(!String.Equals(process.MainModule.FileName,expectedPath,StringComparison.OrdinalIgnoreCase))throw new InvalidOperationException("Process identity changed.");
    FILETIME creation,exit,kernel,user;
    if(!GetProcessTimes(process.Handle,out creation,out exit,out kernel,out user))throw new InvalidOperationException("Cannot identify process start time.");
    var target=new UniqueProcess { Pid=(uint)pid,Started=creation };
    uint session;Check(RmStartSession(out session,0,new StringBuilder(33)),"Start shutdown session");
    try {
     // Register only this process, never shared files or services.
     Check(RmRegisterResources(session,0,null,1,new[]{target},0,null),"Register Radial");
     uint needed,count=0,reboot;
     int result=RmGetList(session,out needed,ref count,null,out reboot);
     if(result!=0&&result!=234)Check(result,"Inspect shutdown scope");
     if(needed>0) {
      var affected=new ProcessInfo[needed];count=needed;
      Check(RmGetList(session,out needed,ref count,affected,out reboot),"Inspect shutdown scope");
      if(reboot!=0||count!=1||affected[0].Process.Pid!=target.Pid||affected[0].Process.Started.dwLowDateTime!=creation.dwLowDateTime||affected[0].Process.Started.dwHighDateTime!=creation.dwHighDateTime)
       throw new InvalidOperationException("Shutdown scope did not match only the intended Radial process.");
      // Zero flags lets the app exit normally and refuse shutdown; never force.
      Check(RmShutdown(session,0,IntPtr.Zero),"Close Radial");
     }
     if(!process.WaitForExit(8000))throw new InvalidOperationException("Radial has not finished closing. Installation must wait.");
    } finally { RmEndSession(session); }
   }
  }
 }
}
'@
}
[RadialMaintenance.RestartManager]::Close([int]$target.ProcessId,$path)
# The normal app-exit handler releases modifiers and stops the pen listeners.
$limit=[Diagnostics.Stopwatch]::StartNew()
do {
    $remaining=@(Get-CimInstance Win32_Process -Filter "Name='tabletpro_radial.exe'" | Where-Object { $_.ExecutablePath -eq $path })
    if(!$remaining.Count){break}
    Start-Sleep -Milliseconds 200
} while($limit.ElapsedMilliseconds -lt 8000)
if($remaining.Count){throw 'A Radial pen listener is still closing; installation must wait.'}
Write-Output "Closed Radial PID $($target.ProcessId) and its pen listeners normally."
