Unicode true
!include "MUI2.nsh"

!ifndef APP_VERSION
  !define APP_VERSION "1.0.0"
!endif

; PORTABLE_DIR (folder with the built binaries) and DIST_DIR (where the
; installer .exe is written) are meant to be passed in as absolute paths
; via /DPORTABLE_DIR=... and /DDIST_DIR=... on the makensis command line.
; This avoids depending on makensis's "change directory to the .nsi file's
; folder" default behavior, which is easy to get wrong when the script
; lives in a subdirectory. The fallbacks below only matter if you compile
; this script by hand (e.g. double-clicking it) without passing /D values.
!ifndef PORTABLE_DIR
  !define PORTABLE_DIR "${__FILEDIR__}\..\dist\portable"
!endif
!ifndef DIST_DIR
  !define DIST_DIR "${__FILEDIR__}\..\dist"
!endif

Name "FPS Overlay ${APP_VERSION}"
OutFile "${DIST_DIR}\FPSOverlay_Setup.exe"
InstallDir "$PROGRAMFILES64\FPS Overlay"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Russian"

Section "FPS Overlay" SEC_MAIN
  SetOutPath "$INSTDIR"
  File /r "${PORTABLE_DIR}\*.*"

  CreateDirectory "$SMPROGRAMS\FPS Overlay"
  CreateShortcut "$SMPROGRAMS\FPS Overlay\FPS Overlay.lnk" "$INSTDIR\FpsOverlay.exe" "" "$INSTDIR\FpsOverlay.exe"
  CreateShortcut "$DESKTOP\FPS Overlay.lnk" "$INSTDIR\FpsOverlay.exe" "" "$INSTDIR\FpsOverlay.exe"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\FPSOverlay" "DisplayName" "FPS Overlay"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\FPSOverlay" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\FPSOverlay" "DisplayVersion" "${APP_VERSION}"
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\FPS Overlay.lnk"
  Delete "$SMPROGRAMS\FPS Overlay\FPS Overlay.lnk"
  RMDir "$SMPROGRAMS\FPS Overlay"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\FPSOverlay"
  Delete "$INSTDIR\FpsOverlay.exe"
  Delete "$INSTDIR\PresentMon.exe"
  Delete "$INSTDIR\assets_overlay.png"
  Delete "$INSTDIR\README.md"
  ; Keep config.ini and user-added files on uninstall by design.
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd
