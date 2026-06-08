; NSIS installer script for MonDoc v1.0
; Requires: windeployqt output in deploy\ directory, LICENSES\ in project root
!define APP_NAME "MonDoc"
!ifndef APP_VERSION
  !define APP_VERSION "1.0.0"
!endif
!define APP_PUBLISHER "MonDoc Project"
OutFile "mondoc-setup.exe"
InstallDir "$PROGRAMFILES64\MonDoc"
Name "${APP_NAME} ${APP_VERSION}"
ShowInstDetails show
ShowUninstDetails show

Section "Install"
    SetOutPath "$INSTDIR"
    File /r "deploy\*.*"
    File /r "LICENSES"
    WriteUninstaller "$INSTDIR\uninstall.exe"
    CreateDirectory "$SMPROGRAMS\MonDoc"
    CreateShortcut "$SMPROGRAMS\MonDoc\MonDoc.lnk" "$INSTDIR\mondoc.exe"
    CreateShortcut "$DESKTOP\MonDoc.lnk" "$INSTDIR\mondoc.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MonDoc" \
        "DisplayName" "${APP_NAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MonDoc" \
        "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MonDoc" \
        "Publisher" "${APP_PUBLISHER}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MonDoc" \
        "DisplayVersion" "${APP_VERSION}"
SectionEnd

Section "Uninstall"
    Delete "$INSTDIR\uninstall.exe"
    Delete "$INSTDIR\mondoc.exe"
    Delete "$SMPROGRAMS\MonDoc\MonDoc.lnk"
    Delete "$DESKTOP\MonDoc.lnk"
    RMDir /r "$INSTDIR"
    RMDir "$SMPROGRAMS\MonDoc"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MonDoc"
SectionEnd
