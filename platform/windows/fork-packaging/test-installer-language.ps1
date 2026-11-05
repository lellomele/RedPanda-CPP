# Run only inside a disposable Windows Sandbox, with Setup.exe in C:\Input.
# Map the script/package read-only and an empty result folder to C:\Results.
# Invoke once per fresh Sandbox with -Language en, zh_CN or it_IT.
param([Parameter(Mandatory=$true)][ValidateSet('en','zh_CN','it_IT')][string]$Language,
      [Parameter(Mandatory=$true)][switch]$ConfirmSandbox)
if (!$ConfirmSandbox -or $env:USERNAME -ne 'WDAGUtilityAccount') {
    throw 'This test installs an application and must run in a disposable Windows Sandbox.'
}
if (Test-Path C:\LanguageTest) { throw 'The test destination must not exist.' }
$version=[Diagnostics.FileVersionInfo]::GetVersionInfo('C:\Input\Setup.exe').FileVersion
if ($version -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw 'Invalid installer file version.' }
$targetLanguage=$Language
$ErrorActionPreference = 'Stop'
$PSDefaultParameterValues['Set-Content:Encoding']='UTF8'
$ProgressPreference = 'SilentlyContinue'
$out = 'C:\Results'
Start-Transcript -Path "$out\probe.log"
try {
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing, System.Windows.Forms
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class Input {
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint x,uint y,uint d,UIntPtr e);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd,int cmd);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] public static extern bool WritePrivateProfileString(string group,string key,string value,string path);
}
'@
function Shot($name) {
    $b = [Windows.Forms.Screen]::PrimaryScreen.Bounds
    $im = New-Object Drawing.Bitmap $b.Width,$b.Height
    $g = [Drawing.Graphics]::FromImage($im)
    try {
        # The Sandbox display can still be attaching when its first window appears.
        for($attempt=0;$attempt -lt 20;$attempt++){
            try { $g.CopyFromScreen($b.Location,[Drawing.Point]::Empty,$b.Size); break }
            catch {
                if($attempt -eq 19){
                    # An unattached virtual display must not disable mandatory UIA checks.
                    $script:captureWarnings += [ordered]@{image=$name;reason=$_.Exception.Message}
                    $script:captureWarnings | ConvertTo-Json -Depth 3 | Set-Content "$out\capture-warnings.json"
                    return
                }
                Start-Sleep -Milliseconds 500
            }
        }
        $im.Save("$out\$name.png")
    } finally {$g.Dispose();$im.Dispose()}
}
function Root {
    $script:process.Refresh()
    if ($script:process.HasExited) { throw "IDE exited with code $($script:process.ExitCode)" }
    $condition=New-Object Windows.Automation.PropertyCondition ([Windows.Automation.AutomationElement]::ProcessIdProperty),$script:process.Id
    [Windows.Automation.AutomationElement]::RootElement.FindAll([Windows.Automation.TreeScope]::Children,$condition) |
        Where-Object {$_.Current.Name -and !$_.Current.IsOffscreen} |
        Sort-Object @{Expression={$_.Current.Name -match '3\.5\.4\.\d+'};Descending=$true} | Select-Object -First 1
}
function Elements {
    $r=Root
    if($r){$r.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition) | ForEach-Object {$_}}
}
function Click($element) {
    $invoke=$null
    if($element.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$invoke)){
        $invoke.Invoke()
        return
    }
    $box=$element.Current.BoundingRectangle
    if ($box.IsEmpty) { throw 'Target not visible' }
    [Input]::SetCursorPos([int]($box.X+$box.Width/2),[int]($box.Y+$box.Height/2)) | Out-Null
    [Input]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
    [Input]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
}
function WaitTitle($text) {
    for($i=0;$i -lt 100;$i++){
        $r=Root
        if($r -and ([string]$r.Current.Name).Contains($text)){return $r}
        Start-Sleep -Milliseconds 200
    }
    throw "Window title missing $text"
}
function Dump($name){
    @(Elements | ForEach-Object {[ordered]@{kind=$_.Current.ControlType.ProgrammaticName;name=$_.Current.Name;visible=!$_.Current.IsOffscreen}}) | ConvertTo-Json -Depth 4 | Set-Content "$out\$name.json"
    Shot $name
}
function Check($name, [bool]$passed){
    $script:checks += [ordered]@{name=$name;passed=$passed}
    $script:checks | ConvertTo-Json -Depth 4 | Set-Content "$out\checks.json"
    if(!$passed){throw "Check failed: $name"}
}
function StartIDE($label,$expected){
    $script:process=Start-Process C:\LanguageTest\app\RedPandaIDE-core.exe -PassThru
    for($i=0;$i -lt 150;$i++){
        $r=Root
        if($r -and $r.Current.Name.Contains($version)){break}
        if($r){
            $button=Elements | Where-Object {$_.Current.ControlType.ProgrammaticName -eq 'ControlType.Button' -and !$_.Current.IsOffscreen -and $_.Current.Name.Replace('&','') -match '^(No|否|OK|Ok|确定|确认)(\(|$)'} | Select-Object -First 1
            if($button){Click $button}
        }
        Start-Sleep -Milliseconds 350
    }
    $r=WaitTitle $version
    [Input]::ShowWindow([IntPtr]$r.Current.NativeWindowHandle,3) | Out-Null
    Start-Sleep -Seconds 2
    Dump $label
    $menus=@(Elements | Where-Object {$_.Current.ControlType.ProgrammaticName -eq 'ControlType.MenuItem' -and !$_.Current.IsOffscreen} | ForEach-Object {$_.Current.Name.Replace('&','')})
    Check "$label uses $expected menu" (@($menus | Where-Object {$_.StartsWith($expected)}).Count -gt 0)
    $r.GetCurrentPattern([Windows.Automation.WindowPattern]::Pattern).Close()
    Check "$label closes cleanly" ($script:process.WaitForExit(15000))
}
$script:checks=@()
$script:captureWarnings=@()
[ordered]@{culture=(Get-Culture).Name;uiCulture=(Get-UICulture).Name;chosen=$targetLanguage;version=$version} | ConvertTo-Json | Set-Content "$out\locale.json"
(Get-FileHash C:\Input\Setup.exe).Hash | Set-Content "$out\setup-sha256.txt"
$script:process=Start-Process C:\Input\Setup.exe -ArgumentList '/D=C:\LanguageTest' -PassThru
for($i=0;$i -lt 100;$i++){if(Root){break};Start-Sleep -Milliseconds 200}
Dump '01-language-picker'
$combo=Elements | Where-Object {$_.Current.ControlType.ProgrammaticName -eq 'ControlType.ComboBox' -and !$_.Current.IsOffscreen} | Select-Object -First 1
Check 'Installer explicitly offers language selection' ($null -ne $combo)
Check 'English is the preselected installer language' ($combo.Current.Name -eq 'English')
$items=@($combo.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition) | Where-Object {$_.Current.ControlType.ProgrammaticName -eq 'ControlType.ListItem'} | ForEach-Object {$_.Current.Name})
Check 'Installer retains English and Simplified Chinese and adds Italian' ($items.Count -eq 3 -and 'English' -in $items -and 'Italiano' -in $items -and @($items | Where-Object {$_ -match '中文|简体'}).Count -eq 1)
$chosen=if($targetLanguage -eq 'it_IT'){'Italiano'}elseif($targetLanguage -eq 'zh_CN'){@($items | Where-Object {$_ -match '中文|简体'})[0]}else{'English'}
$index=[Array]::IndexOf($items,$chosen)
$combo.SetFocus()
[Windows.Forms.SendKeys]::SendWait('{HOME}')
for($i=0;$i -lt $index;$i++){[Windows.Forms.SendKeys]::SendWait('{DOWN}')}
Start-Sleep -Milliseconds 300
Check 'Chosen language is selected' ($combo.Current.Name -eq $chosen)
Dump '02-chosen-language'
[Windows.Forms.SendKeys]::SendWait('{ENTER}')
Start-Sleep -Milliseconds 600
Dump '03-installer-welcome'
$texts=@(Elements | ForEach-Object {$_.Current.Name}) -join ' '
Check 'Installer welcome follows selected language' ($(if($targetLanguage -eq 'it_IT'){$texts -match 'Benvenut|Avanti'}elseif($targetLanguage -eq 'zh_CN'){$texts -match '欢迎|下一步'}else{$texts -match 'Welcome|Next'}))
foreach($step in 1..12){
    $deadline=(Get-Date).AddSeconds(60)
    $button=$null
    do {
        $script:process.Refresh()
        if($script:process.HasExited){break}
        # NSIS uses control ID 1 for Next, Accept, Install and Finish. A title-bar
        # Close button is never a wizard action, including during extraction.
        $button=Elements | Where-Object {$_.Current.ControlType.ProgrammaticName -eq 'ControlType.Button' -and $_.Current.AutomationId -eq '1' -and $_.Current.IsEnabled -and !$_.Current.IsOffscreen} | Select-Object -First 1
        if($button){break}
        Start-Sleep -Milliseconds 250
    } while((Get-Date) -lt $deadline)
    if($script:process.HasExited){break}
    if(!$button){Dump "unexpected-$step";throw "No enabled wizard action at page $step"}
    Click $button
    Start-Sleep -Milliseconds 750
    $script:process.Refresh()
    if($script:process.HasExited){break}
}
if(!$script:process.HasExited){$null=$script:process.WaitForExit(5000)}
Check 'Installer exits successfully' ($script:process.HasExited -and $script:process.ExitCode -eq 0)
Check 'Core was installed' (Test-Path C:\LanguageTest\app\RedPandaIDE-core.exe)
$seed=Get-Content C:\LanguageTest\app\install-language.ini -Raw
Check 'Package contains exact selected language seed' ($seed -match ('(?m)^language='+$targetLanguage+'\s*$'))
$expected=if($targetLanguage -eq 'it_IT'){'Modifica'}elseif($targetLanguage -eq 'zh_CN'){'编辑'}else{'Edit'}
StartIDE '04-first-launch' $expected
# The saved user preference must win after a package language changes.
$other=if($targetLanguage -eq 'it_IT'){'en'}else{'it_IT'}
Check 'Switch package language for precedence test' ([Input]::WritePrivateProfileString('Environment','language',$other,'C:\LanguageTest\app\install-language.ini'))
StartIDE '05-existing-preference-retained' $expected
$uninstaller=Start-Process C:\LanguageTest\uninstall.exe -ArgumentList '/S' -PassThru
Check 'Uninstaller exits successfully' ($uninstaller.WaitForExit(15000) -and $uninstaller.ExitCode -eq 0)
Start-Sleep -Seconds 2
Check 'Uninstaller removes installer language seed' (!(Test-Path C:\LanguageTest\app\install-language.ini))
Set-Content "$out\done.txt" 'Complete'
} catch {
    $_ | Out-String | Set-Content "$out\error.txt"
    try { Shot 'error' } catch {}
    throw
} finally { Stop-Transcript }
