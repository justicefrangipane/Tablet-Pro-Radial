Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"

!define APP_VERSION "0.3.0"
!define APP_NAME "Tablet Pro Radial"
!define MENU_FOLDER "Tablet Pro Radial 0.3"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\TabletProRadial-${APP_VERSION}"
!ifndef PAYLOAD_DIR
  !error "Use build-installer.py to supply the verified payload and file manifests."
!endif

Name "${APP_NAME} 0.3"
OutFile "${OUTPUT_EXE}"
InstallDir "$LOCALAPPDATA\Programs\TabletProRadial\${APP_VERSION}"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 32
SetDatablockOptimize on
ShowInstDetails show
ShowUninstDetails show
BrandingText "Tablet Pro Radial - development preview"
VIProductVersion "0.3.0.0"
VIAddVersionKey "ProductName" "${APP_NAME}"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
VIAddVersionKey "FileVersion" "${APP_VERSION}"
VIAddVersionKey "FileDescription" "Tablet Pro Radial per-user installer"
VIAddVersionKey "LegalCopyright" "Tablet Pro"

!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Install Tablet Pro Radial 0.3"
!define MUI_WELCOMEPAGE_TEXT "Install the new radial menu, docked rail, and editor for your Windows account.$\r$\n$\r$\nClose Tablet Pro Studio and any running Tablet Pro Radial copies before continuing.$\r$\n$\r$\nYour saved settings and profiles will be kept."
!define MUI_FINISHPAGE_RUN "$INSTDIR\tabletpro_radial.exe"
!define MUI_FINISHPAGE_RUN_PARAMETERS "--radial"
!define MUI_FINISHPAGE_RUN_TEXT "Open Tablet Pro Radial"
!define MUI_FINISHPAGE_TEXT "Tablet Pro Radial is installed.$\r$\n$\r$\nOpen it from the desktop shortcut, or choose Radial menu, Docked rail, or Editor in the Tablet Pro Radial 0.3 Start menu folder.$\r$\n$\r$\nPen button setup is available in the editor."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  SetShellVarContext current
  ${IfNot} ${IsNativeAMD64}
    MessageBox MB_OK|MB_ICONSTOP "This build requires Windows 10 or later on an Intel or AMD 64-bit PC."
    Abort
  ${EndIf}
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_OK|MB_ICONSTOP "This build requires Windows 10 or later."
    Abort
  ${EndIf}
  ; No directory page or arbitrary /D destination: this version owns one folder.
  StrCpy $INSTDIR "$LOCALAPPDATA\Programs\TabletProRadial\${APP_VERSION}"
  IfFileExists "$INSTDIR\tabletpro_radial.exe" 0 ready
  ClearErrors
  FileOpen $0 "$INSTDIR\tabletpro_radial.exe" a
  IfErrors app_busy
  FileClose $0
  Goto ready
  app_busy:
    MessageBox MB_OK|MB_ICONSTOP "Close Tablet Pro Radial, then run this installer again. The existing application is in use or cannot be updated."
    Abort
  ready:
FunctionEnd

Section "Install"
  SetShellVarContext current
  SetOverwrite try
  ClearErrors
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  IfErrors install_failed
  ; Explicit File commands, each checked for extraction errors.
  !include "${GENERATED_DIR}/install-files.nsh"

  SetOutPath "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\${MENU_FOLDER}"
  CreateShortCut "$SMPROGRAMS\${MENU_FOLDER}\Radial menu.lnk" "$INSTDIR\tabletpro_radial.exe" "--radial" "$INSTDIR\tabletpro_radial.exe" 0 SW_SHOWNORMAL "" "Open the radial menu"
  CreateShortCut "$SMPROGRAMS\${MENU_FOLDER}\Docked rail.lnk" "$INSTDIR\tabletpro_radial.exe" "--docked" "$INSTDIR\tabletpro_radial.exe" 0 SW_SHOWNORMAL "" "Open the docked radial rail"
  CreateShortCut "$SMPROGRAMS\${MENU_FOLDER}\Editor.lnk" "$INSTDIR\tabletpro_radial.exe" "--editor" "$INSTDIR\tabletpro_radial.exe" 0 SW_SHOWNORMAL "" "Edit the menu and pen button settings"
  CreateShortCut "$DESKTOP\${MENU_FOLDER}.lnk" "$INSTDIR\tabletpro_radial.exe" "--radial" "$INSTDIR\tabletpro_radial.exe" 0 SW_SHOWNORMAL "" "Open Tablet Pro Radial"
  IfErrors install_failed

  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME} 0.3"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "Tablet Pro"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\tabletpro_radial.exe"
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" ${PAYLOAD_KIB}
  IfErrors install_failed
  Goto install_done
  install_failed:
    MessageBox MB_OK|MB_ICONSTOP "Installation could not finish. Close Tablet Pro Radial, check that you have free disk space, and run this installer again. No administrator setup or reboot is required."
    SetErrorLevel 2
    Abort
  install_done:
SectionEnd

Function un.onInit
  SetShellVarContext current
  ; Refuse deletion if the uninstaller has been moved outside its owned folder.
  StrCmp $INSTDIR "$LOCALAPPDATA\Programs\TabletProRadial\${APP_VERSION}" location_ok
    MessageBox MB_OK|MB_ICONSTOP "Run the uninstaller from the original Tablet Pro Radial installation folder."
    Abort
  location_ok:
  IfFileExists "$INSTDIR\tabletpro_radial.exe" 0 ready
  ClearErrors
  FileOpen $0 "$INSTDIR\tabletpro_radial.exe" a
  IfErrors app_busy
  FileClose $0
  Goto ready
  app_busy:
    MessageBox MB_OK|MB_ICONSTOP "Close Tablet Pro Radial before uninstalling it."
    Abort
  ready:
FunctionEnd

Section "Uninstall"
  SetShellVarContext current
  ClearErrors
  ; Only packaged files are deleted. Profiles (*.radial.json) are preserved.
  ; Directory removal is empty-only, never recursive. User-added files survive.
  !include "${GENERATED_DIR}/uninstall-files.nsh"
  Delete "$DESKTOP\${MENU_FOLDER}.lnk"
  Delete "$SMPROGRAMS\${MENU_FOLDER}\Radial menu.lnk"
  Delete "$SMPROGRAMS\${MENU_FOLDER}\Docked rail.lnk"
  Delete "$SMPROGRAMS\${MENU_FOLDER}\Editor.lnk"
  RMDir "$SMPROGRAMS\${MENU_FOLDER}"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  ; Never touch QSettings, AppConfigLocation, Documents, or user exports.
SectionEnd
