# remote_share_gui.ps1 - open the development GUI without WinShun or packaging.
#
# This is the pre-stable loop: build the plugin into an isolated folder, run this
# wrapper, and test the real QML window. It never opens a serial port, never
# launches a session, and never writes the repository-root binaries.
#
# The data folder is a fresh temporary directory unless -DataDir is given, so the
# user's real tio.json, logs and installed WinShun are untouched. The wrapper
# removes only a folder it created itself (unless -KeepData is used).
#
# Examples:
#   pwsh tools\remote_share_gui.ps1                 # visible window, temp data
#   pwsh tools\remote_share_gui.ps1 -Check          # hidden startup smoke test
#   pwsh tools\remote_share_gui.ps1 -English -Dark  # locale and theme
#   pwsh tools\remote_share_gui.ps1 -DataDir .\build\gui-data -KeepData
#
# The visible run is for the remote-sharing flow: enable the switch, read the
# warning, confirm, then press Launch and confirm again. The status area shows
# 127.0.0.1 for local checks; LAN clients use this PC's IPv4 address and TCP 54321.
[CmdletBinding()]
param(
    [string]$Exe = "",
    [string]$QtDir = "",
    [string]$DataDir = "",
    [switch]$Check,
    [switch]$KeepData,
    [switch]$NoShow,
    [switch]$English,
    [switch]$Dark,
    [switch]$Details,
    [switch]$PortsOpen,
    [ValidateRange(1024, 65535)]
    [int]$SharePort = 54321,
    [ValidateRange(2, 60)]
    [int]$CheckSeconds = 6,
    [string]$Auto = ""
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot

function Resolve-Exe {
    param([string]$Requested)

    $candidates = @()
    if ($Requested) {
        $candidates += $Requested
    } else {
        $candidates += (Join-Path $repo 'build\remote-share-verify\artifacts\tio.exe')
        $candidates += (Join-Path $repo 'build\msvc\artifacts\tio.exe')
        $candidates += (Join-Path $repo 'build\msvc\tio.exe')
    }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "No development build found. Build one first, for example:" + [Environment]::NewLine +
        "  cmake -S . -B build\remote-share-verify -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH=D:/tools/Qt/6.12.0/msvc2022_64 -DTIO_ARTIFACT_DIR=$repo\build\remote-share-verify\artifacts" + [Environment]::NewLine +
        "  cmake --build build\remote-share-verify --target tio TioApp"
}

function Resolve-QtDir {
    param([string]$Requested)

    $candidates = @()
    if ($Requested) {
        $candidates += $Requested
    }
    if ($env:WS_QT_DIR) {
        $candidates += $env:WS_QT_DIR
    }
    if ($env:WS_APP_DIR) {
        $candidates += $env:WS_APP_DIR
    }
    $candidates += 'C:\Program Files\WinShun'

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate 'Qt6Core.dll') -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "WinShun's Qt folder was not found. Pass -QtDir or set WS_QT_DIR to a folder containing Qt6Core.dll."
}

function Get-LanEndpoints {
    $addresses = @()
    try {
        $addresses = [System.Net.Dns]::GetHostAddresses([System.Net.Dns]::GetHostName())
    } catch {
        return @()
    }

    return @($addresses |
        Where-Object {
            $_.AddressFamily -eq [System.Net.Sockets.AddressFamily]::InterNetwork -and
            -not [System.Net.IPAddress]::IsLoopback($_) -and
            -not $_.ToString().StartsWith('169.254.')
        } |
        ForEach-Object { '{0}:{1}' -f $_.ToString(), $SharePort } |
        Sort-Object -Unique)
}

function Test-SharePort {
    param([int]$Port)
    $listener = $null
    try {
        $listener = [System.Net.Sockets.TcpListener]::new(
            [System.Net.IPAddress]::Any, $Port)
        $listener.Start()
        return $true
    } catch {
        return $false
    } finally {
        if ($listener) {
            try { $listener.Stop() } catch { }
        }
    }
}

$exe = Resolve-Exe -Requested $Exe
$qt = Resolve-QtDir -Requested $QtDir

$createdDataDir = $false
if (-not $DataDir) {
    $DataDir = Join-Path ([System.IO.Path]::GetTempPath()) (
        'winshun-tio-gui-{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss'), [guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Path $DataDir -Force | Out-Null
    $createdDataDir = $true
} else {
    $DataDir = [System.IO.Path]::GetFullPath($DataDir)
    New-Item -ItemType Directory -Path $DataDir -Force | Out-Null
}

$arguments = @(
    '--window',
    '--app-dir', $qt,
    '--data-dir', $DataDir
)
if ($NoShow -or $Check) { $arguments += '--no-show' }
if ($English) { $arguments += '--en' }
if ($Dark) { $arguments += '--dark' }
if ($Details) { $arguments += '--details' }
if ($PortsOpen) { $arguments += '--ports-open' }
if ($Auto) {
    $arguments += '--auto'
    $arguments += $Auto
}

Write-Host 'WinShun tio development GUI'
Write-Host ('  exe       : {0}' -f $exe)
Write-Host ('  Qt        : {0}' -f $qt)
Write-Host ('  dataDir   : {0}' -f $DataDir)
Write-Host ('  sharing   : tio listens on TCP {0}; toggle it in Advanced - bidirectional serial sharing' -f $SharePort)
Write-Host ('  firewall  : no rule is added; allow TCP {0} if a LAN client must connect' -f $SharePort)
Write-Host '  session   : this wrapper opens the window only; it does not launch a serial session'

$lan = @(Get-LanEndpoints)
if ($lan.Count -gt 0) {
    Write-Host ('  LAN       : {0}' -f ($lan -join ', '))
} else {
    Write-Host '  LAN       : no non-loopback IPv4 address found'
}

if (-not $Check) {
    if (Test-SharePort -Port $SharePort) {
        Write-Host ('  port {0}: free (a shared launch can bind it)' -f $SharePort)
    } else {
        Write-Host ('  port {0}: in use (a shared launch will report the conflict)' -f $SharePort)
    }
}

$checkFailed = $false
$exitCode = 0

try {
    if ($Check) {
        # ProcessStartInfo.ArgumentList keeps each argument intact, including
        # -app-dir "C:\Program Files\WinShun" (Start-Process would re-split it).
        $start = [System.Diagnostics.ProcessStartInfo]::new()
        # ArgumentList is PowerShell 7 only. Windows PowerShell 5.1 would silently
        # leave it null and fail later with "cannot call a method on a
        # null-valued expression", which looks like a window bug.
        if (-not $start.PSObject.Properties['ArgumentList']) {
            Write-Host 'FAIL -Check needs PowerShell 7+: run this script with pwsh, not powershell'
            exit 1
        }
        $start.FileName = $exe
        $start.WorkingDirectory = $repo
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        foreach ($argument in $arguments) {
            $start.ArgumentList.Add($argument)
        }

        $process = [System.Diagnostics.Process]::Start($start)

        Start-Sleep -Seconds $CheckSeconds
        $alive = -not $process.HasExited
        if (-not $alive) {
            $exitCode = $process.ExitCode
        } else {
            $process.Kill()
            $process.WaitForExit()
        }

        $stderrText = $process.StandardError.ReadToEnd()
        $stdoutText = $process.StandardOutput.ReadToEnd()
        $errors = @()
        if ($stderrText) {
            $errors = @($stderrText -split "`r?`n" |
                Where-Object {
                    $_ -match 'Unable to assign|Cannot assign|is not a type|is not installed|' +
                        'ReferenceError|TypeError|Binding loop|Non-existent attached object|' +
                        'failed to load component|did not load|tio: qml:'
                })
        }

        if (-not $alive) {
            Write-Host ('FAIL the window exited during the check (exit {0})' -f $exitCode)
            $checkFailed = $true
        } else {
            Write-Host ('PASS the window stayed up for {0}s' -f $CheckSeconds)
        }
        if ($errors.Count -gt 0) {
            Write-Host ('FAIL QML warning: {0}' -f $errors[0])
            $checkFailed = $true
        } else {
            Write-Host 'PASS no QML warning'
        }
    } else {
        Push-Location $repo
        try {
            & $exe @arguments
            $exitCode = $LASTEXITCODE
        } finally {
            Pop-Location
        }
        Write-Host ('window closed (exit {0})' -f $exitCode)
    }
} finally {
    if ($createdDataDir -and -not $KeepData) {
        Remove-Item -LiteralPath $DataDir -Recurse -Force -ErrorAction SilentlyContinue
    } elseif ($createdDataDir) {
        Write-Host ('kept data : {0}' -f $DataDir)
    }
}

if ($checkFailed) { exit 1 }
exit $exitCode
