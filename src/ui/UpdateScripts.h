/**
 * \file UpdateScripts.h
 * \brief Помощники, заменяющие файлы после закрытия Spotty.
 */
#pragma once

#include <QByteArray>

namespace spotty::update_scripts {

// \brief Скрипт живёт отдельно от приложения, чтобы Windows освободила его файлы.
inline const QByteArray kWindowsHelper = R"PS(param([long]$OwnerPid,[string]$Kind,[string]$Payload,[string]$Target,
          [string]$Work,[string]$ErrorTitle,[string]$ErrorText)
$ErrorActionPreference = 'Stop'
$failed = $false
$launched = $false
try {
    Wait-Process -Id $OwnerPid -ErrorAction SilentlyContinue
    if ($Kind -eq 'setup') {
        $dir = Split-Path -Parent $Target
        $arguments = @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/DIR="' + $dir + '"'))
        $installer = Start-Process -FilePath $Payload -ArgumentList $arguments -Verb RunAs -Wait -PassThru
        if ($installer.ExitCode -ne 0) { throw "Installer exit code: $($installer.ExitCode)" }
    } else {
        $root = Split-Path -Parent $Target
        $backup = Join-Path $Work 'backup'
        $applied = [Collections.Generic.List[object]]::new()
        try {
            $files = Get-ChildItem -LiteralPath $Payload -File -Recurse -Force |
                Sort-Object @{Expression = { $_.Name -ne 'spotty.exe' }}
            foreach ($file in $files) {
                $relative = $file.FullName.Substring($Payload.Length).TrimStart([char[]]@('\','/'))
                if ($relative -match '^(?i:config[\\/]|logs[\\/]|spotty-portable\.txt$)') { continue }
                $destination = Join-Path $root $relative
                $old = Join-Path $backup $relative
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
                $existed = Test-Path -LiteralPath $destination
                if ($existed) {
                    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $old) | Out-Null
                    Move-Item -LiteralPath $destination -Destination $old
                }
                $applied.Add(@{Destination=$destination; Old=$old; Existed=$existed})
                Copy-Item -LiteralPath $file.FullName -Destination $destination
            }
            $ready = Join-Path $Work 'ready'
            $started = Start-Process -FilePath $Target -ArgumentList @('--spotty-update-confirm', ('"' + $ready + '"')) -PassThru
            for ($i = 0; $i -lt 60 -and -not (Test-Path -LiteralPath $ready); ++$i) {
                if ($started.HasExited) { break }
                Start-Sleep -Seconds 1
                $started.Refresh()
            }
            if (-not (Test-Path -LiteralPath $ready)) {
                if (-not $started.HasExited) {
                    Stop-Process -Id $started.Id -Force
                    $started.WaitForExit(10000) | Out-Null
                }
                throw 'The new version did not start'
            }
            $launched = $true
        } catch {
            for ($i = $applied.Count - 1; $i -ge 0; --$i) {
                $item = $applied[$i]
                Remove-Item -LiteralPath $item.Destination -Force -ErrorAction SilentlyContinue
                if ($item.Existed) { Move-Item -LiteralPath $item.Old -Destination $item.Destination -Force }
            }
            throw
        }
    }
} catch {
    $failed = $true
    Add-Type -AssemblyName System.Windows.Forms
    [Windows.Forms.MessageBox]::Show(($ErrorText + "`n" + $_.Exception.Message + "`n" + $Work), $ErrorTitle) | Out-Null
}
if (-not $launched -and (Test-Path -LiteralPath $Target)) { Start-Process -FilePath $Target }
if (-not $failed) { Remove-Item -LiteralPath $Work -Recurse -Force -ErrorAction SilentlyContinue }
)PS";

// \brief Аргументы передаются отдельно, поэтому пути не становятся командами оболочки.
inline const QByteArray kPosixHelper = R"SH(#!/bin/sh
pid=$1
payload=$2
target=$3
work=$4
kind=$5
error_text=$6
while kill -0 "$pid" 2>/dev/null; do sleep 1; done
parent=$(dirname "$target")
new="$parent/.spotty-new-$$"
old="$parent/.spotty-old-$$"
ok=0
if [ "$kind" = macos ]; then
    /usr/bin/ditto "$payload" "$new" && mv "$target" "$old" && mv "$new" "$target" && ok=1
else
    cp "$payload" "$new" && chmod --reference="$target" "$new" && \
        mv "$target" "$old" && mv "$new" "$target" && ok=1
fi
if [ "$ok" -eq 1 ]; then
    unset APPDIR APPIMAGE LD_LIBRARY_PATH QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH
    if [ "$kind" = macos ]; then
        "$target/Contents/MacOS/spotty" --spotty-update-confirm "$work/ready" >/dev/null 2>&1 &
    else
        "$target" --spotty-update-confirm "$work/ready" >/dev/null 2>&1 &
    fi
    started=$!
    count=0
    while [ ! -f "$work/ready" ] && kill -0 "$started" 2>/dev/null && [ "$count" -lt 60 ]; do
        sleep 1
        count=$((count + 1))
    done
    if [ ! -f "$work/ready" ]; then
        kill "$started" 2>/dev/null
        wait "$started" 2>/dev/null
        mv "$target" "$new" && mv "$old" "$target"
        ok=0
    fi
fi
if [ "$ok" -ne 1 ]; then
    [ ! -e "$target" ] && [ -e "$old" ] && mv "$old" "$target"
    if [ "$kind" = macos ]; then
        /usr/bin/osascript -e 'on run argv' -e 'display dialog (item 1 of argv) buttons {"OK"} with icon caution' -e 'end run' "$error_text
$work" >/dev/null 2>&1
    fi
else
    rm -rf "$old" "$new" "$work"
fi
if [ "$ok" -ne 1 ]; then
    if [ "$kind" = macos ]; then
        /usr/bin/open "$target"
    else
        unset APPDIR APPIMAGE LD_LIBRARY_PATH QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH
        "$target" --spotty-update-error "$error_text
$work" >/dev/null 2>&1 &
    fi
fi
)SH";

inline const QByteArray kExtractZip = R"PS(param([string]$Archive,[string]$Destination)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($Archive)
try {
    $total = 0
    if ($zip.Entries.Count -gt 10000) { throw 'Archive has too many files' }
    foreach ($entry in $zip.Entries) {
        $total += $entry.Length
        if ($total -gt 2147483648) { throw 'Archive is too large' }
        $name = $entry.FullName.Replace('\','/')
        if ($name.StartsWith('/') -or $name.Contains(':') -or
            ($name.Split('/') -contains '..')) { throw 'Invalid archive path' }
    }
} finally { $zip.Dispose() }
[IO.Compression.ZipFile]::ExtractToDirectory($Archive, $Destination)
)PS";

} // namespace spotty::update_scripts
