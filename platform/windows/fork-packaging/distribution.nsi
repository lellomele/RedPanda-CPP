Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "WinVer.nsh"
!include "x64.nsh"
Name "Red Panda C++"
Caption "Red Panda C++"
OutFile "${OUTPUT_FILE}"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 32
ShowInstDetails show
ShowUninstDetails show
SetOverwrite on
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\RedPandaCPP-Mele-Fork"
!define MUI_ICON "${APP_ICON}"
!define MUI_UNICON "${APP_ICON}"
!define MUI_ABORTWARNING
!define MUI_LANGDLL_ALWAYSSHOW
!define MUI_CUSTOMFUNCTION_ABORT CleanupIncomplete
!define MUI_FINISHPAGE_RUN "$INSTDIR\RedPandaIDE.exe"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
Var TestMode
Var OwnTarget
Var Installed
!ifdef PORTABLE_PACKAGE
 InstallDir "$DESKTOP\RedPandaCPP-Portable"
!else
 InstallDir "$LOCALAPPDATA\Programs\RedPandaCPP-Mele"
!endif
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${PAYLOAD}\LICENSE"
!ifndef PORTABLE_PACKAGE
 !insertmacro MUI_PAGE_COMPONENTS
!endif
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_PAGE_CUSTOMFUNCTION_PRE FinishPagePre
!insertmacro MUI_PAGE_FINISH
!ifndef PORTABLE_PACKAGE
 !insertmacro MUI_UNPAGE_CONFIRM
 !insertmacro MUI_UNPAGE_INSTFILES
!endif
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "Italian"
LangString MainName ${LANG_ENGLISH} "Red Panda C++"
LangString MainName ${LANG_SIMPCHINESE} "Red Panda C++"
LangString MainName ${LANG_ITALIAN} "Red Panda C++"
LangString DesktopName ${LANG_ENGLISH} "Desktop shortcut"
LangString DesktopName ${LANG_SIMPCHINESE} "桌面快捷方式"
LangString DesktopName ${LANG_ITALIAN} "Collegamento sul desktop"
LangString NonEmpty ${LANG_ENGLISH} "Choose an empty folder. To update an installed copy, uninstall it first: personal settings are preserved."
LangString NonEmpty ${LANG_SIMPCHINESE} "请选择一个空文件夹。更新已安装的副本之前，请先卸载旧版本：个人设置将被保留。"
LangString NonEmpty ${LANG_ITALIAN} "Scegli una cartella vuota. Per aggiornare una copia installata, disinstallala prima: le preferenze personali vengono conservate."
LangString AlreadyInstalled ${LANG_ENGLISH} "This fork is already installed. Uninstall it before installing a new version."
LangString AlreadyInstalled ${LANG_SIMPCHINESE} "此分支版本已安装。请先卸载，然后再安装新版本。"
LangString AlreadyInstalled ${LANG_ITALIAN} "Questo fork è già installato. Disinstallalo prima di installare una nuova versione."
LangString CopyFailed ${LANG_ENGLISH} "The package could not be written completely. No existing installation has been overwritten. Choose a writable folder and retry."
LangString CopyFailed ${LANG_SIMPCHINESE} "无法完整写入安装包。现有安装未被覆盖。请选择一个可写的文件夹，然后重试。"
LangString CopyFailed ${LANG_ITALIAN} "Non è stato possibile scrivere l’intero pacchetto. Nessuna installazione esistente è stata sovrascritta. Scegli una cartella scrivibile e riprova."
!ifndef APP_VERSION_NUMERIC
 !searchparse "${APP_VERSION}" "" APP_VERSION_NUMERIC "-rmele"
!endif
VIProductVersion "${APP_VERSION_NUMERIC}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "Red Panda C++"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "Red Panda C++ Windows distribution"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion" "${APP_VERSION_NUMERIC}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalCopyright" "Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com); upstream © 2021-2026 Roy Qu"

Function .onInit
 StrCpy $TestMode 0
 StrCpy $OwnTarget 0
 StrCpy $Installed 0
 ${GetParameters} $0
 ClearErrors
 ${GetOptions} $0 "/TESTMODE" $1
 ${IfNot} ${Errors}
  StrCpy $TestMode 1
 ${EndIf}
 ${IfNot} ${RunningX64}
  MessageBox MB_ICONSTOP "Windows x64 is required."
  Abort
 ${EndIf}
 ${IfNot} ${AtLeastWin10}
  MessageBox MB_ICONSTOP "Windows 10 or later is required."
  Abort
 ${EndIf}
 SetShellVarContext current
 StrCpy $LANGUAGE ${LANG_ENGLISH}
 IfSilent languageSelected
 !insertmacro MUI_LANGDLL_DISPLAY
 languageSelected:
 !ifndef PORTABLE_PACKAGE
 ${If} $TestMode == 0
  ReadRegStr $0 HKCU "${UNINSTALL_KEY}" "InstallLocation"
  ${If} $0 != ""
   IfFileExists "$0\RedPandaIDE.exe" 0 +3
   MessageBox MB_ICONINFORMATION "$(AlreadyInstalled)"
   Abort
  ${EndIf}
 ${EndIf}
 !endif
FunctionEnd

Function FinishPagePre
 ${If} $TestMode == 1
  Abort
 ${EndIf}
FunctionEnd

Function CleanupIncomplete
 ${If} $OwnTarget == 1
 ${AndIf} $Installed == 0
  Delete "$INSTDIR\app\install-language.ini"
  !include "${DELETE_MANIFEST}"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
 ${EndIf}
FunctionEnd

Function .onInstFailed
 Call CleanupIncomplete
FunctionEnd


Section "$(MainName)" Main
 SectionIn RO
 ${DirState} "$INSTDIR" $0
 ${If} $0 == 1
  MessageBox MB_ICONSTOP "$(NonEmpty)"
  Abort
 ${EndIf}
 StrCpy $OwnTarget 1
 ClearErrors
 SetOutPath "$INSTDIR"
 File /r "${PAYLOAD}\*"
 ${If} ${Errors}
  Call CleanupIncomplete
  MessageBox MB_ICONSTOP "$(CopyFailed)"
  SetErrorLevel 1
  Abort
 ${EndIf}
 ClearErrors
 ${If} $LANGUAGE == ${LANG_ITALIAN}
  WriteINIStr "$INSTDIR\app\install-language.ini" "Environment" "language" "it_IT"
 ${ElseIf} $LANGUAGE == ${LANG_SIMPCHINESE}
  WriteINIStr "$INSTDIR\app\install-language.ini" "Environment" "language" "zh_CN"
 ${Else}
  WriteINIStr "$INSTDIR\app\install-language.ini" "Environment" "language" "en"
 ${EndIf}
 ${If} ${Errors}
  Call CleanupIncomplete
  MessageBox MB_ICONSTOP "$(CopyFailed)"
  SetErrorLevel 1
  Abort
 ${EndIf}
 !ifndef PORTABLE_PACKAGE
 ClearErrors
 WriteUninstaller "$INSTDIR\uninstall.exe"
 ${If} ${Errors}
  Call CleanupIncomplete
  MessageBox MB_ICONSTOP "$(CopyFailed)"
  SetErrorLevel 1
  Abort
 ${EndIf}
 ${If} $TestMode == 0
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "Red Panda C++ (fork Mele, x64)"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "Ing. prof. Raffaele Mele (fork)"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\RedPandaIDE.exe"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '$\"$INSTDIR\uninstall.exe$\"'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '$\"$INSTDIR\uninstall.exe$\" /S'
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
  SetOutPath "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\Red Panda C++ - Mele"
  CreateShortcut "$SMPROGRAMS\Red Panda C++ - Mele\Red Panda C++.lnk" "$INSTDIR\RedPandaIDE.exe"
  CreateShortcut "$SMPROGRAMS\Red Panda C++ - Mele\Disinstalla.lnk" "$INSTDIR\uninstall.exe"
 ${EndIf}
 !endif
 StrCpy $Installed 1
SectionEnd

!ifndef PORTABLE_PACKAGE
Section /o "$(DesktopName)" Desktop
 ${If} $TestMode == 0
  SetOutPath "$INSTDIR"
  CreateShortcut "$DESKTOP\Red Panda C++ - Mele.lnk" "$INSTDIR\RedPandaIDE.exe"
 ${EndIf}
SectionEnd

Function un.onInit
 SetShellVarContext current
 StrCpy $TestMode 0
 ${GetParameters} $0
 ClearErrors
 ${GetOptions} $0 "/TESTMODE" $1
 ${IfNot} ${Errors}
  StrCpy $TestMode 1
 ${EndIf}
FunctionEnd

Section "Uninstall"
 Delete "$INSTDIR\app\install-language.ini"
 !include "${DELETE_MANIFEST}"
 Delete "$INSTDIR\uninstall.exe"
 RMDir "$INSTDIR"
 ${If} $TestMode == 0
  Delete "$DESKTOP\Red Panda C++ - Mele.lnk"
  Delete "$SMPROGRAMS\Red Panda C++ - Mele\Red Panda C++.lnk"
  Delete "$SMPROGRAMS\Red Panda C++ - Mele\Disinstalla.lnk"
  RMDir "$SMPROGRAMS\Red Panda C++ - Mele"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
 ${EndIf}
SectionEnd
!endif
