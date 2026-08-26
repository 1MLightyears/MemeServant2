param(
    [string]$Executable = (Join-Path $PSScriptRoot '..\build\MemeServant2.exe'),
    [string]$Hotkey = '^%x'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ClipboardE2eNative {
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr window);
}
'@

function Wait-WithEvents([int]$Milliseconds) {
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    while ($stopwatch.ElapsedMilliseconds -lt $Milliseconds) {
        [Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 10
    }
}

$form = New-Object Windows.Forms.Form
$form.Text = 'MemeServant2 Clipboard E2E Target'
$form.ShowInTaskbar = $false
$form.Opacity = 0.01
$form.Size = New-Object Drawing.Size(240, 100)
$editor = New-Object Windows.Forms.RichTextBox
$editor.Dock = 'Fill'
$form.Controls.Add($editor)
$form.Show()

$process = $null
try {
    $process = Start-Process -FilePath (Resolve-Path -LiteralPath $Executable) `
        -WorkingDirectory (Split-Path -Parent (Resolve-Path -LiteralPath $Executable)) `
        -WindowStyle Hidden -PassThru
    Wait-WithEvents 1500

    [void][ClipboardE2eNative]::SetForegroundWindow($form.Handle)
    [void]$editor.Focus()
    [Windows.Forms.SendKeys]::SendWait($Hotkey)
    Wait-WithEvents 500
    [Windows.Forms.SendKeys]::SendWait('{ENTER}')
    Wait-WithEvents 1800

    $data = [Windows.Forms.Clipboard]::GetDataObject()
    $formats = if ($null -eq $data) { @() } else { @($data.GetFormats($false)) }
    $containsImage = [Windows.Forms.Clipboard]::ContainsImage()
    $pastedIntoRichEditor = $editor.Rtf -match '\\pict'
    [pscustomobject]@{
        ProcessStillRunning = -not $process.HasExited
        ClipboardContainsImage = $containsImage
        RichEditorReceivedImage = $pastedIntoRichEditor
        ClipboardFormats = $formats -join ', '
    } | Format-List

    if (-not $containsImage) { throw 'Windows Forms does not recognize the clipboard contents as an image.' }
    if (-not $pastedIntoRichEditor) { throw 'The automatic Ctrl+V did not paste an image into the target editor.' }
}
finally {
    $form.Close()
    if ($null -ne $process -and -not $process.HasExited) {
        $process.Kill()
        $process.WaitForExit(3000)
    }
}
