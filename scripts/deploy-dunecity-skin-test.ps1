[CmdletBinding()]
param(
    [string]$RepoRoot = "",
    [string]$SourceRoot = "D:\BotServer\GitHub\Discord-AI-Bot\dune2",
    [ValidateSet("all", "current")]
    [string]$Scope = "all",
    [string]$Unit = "",
    [string]$PythonExecutable = "python",
    [string]$WindowsBuildDir = "build-windows-skins",
    [string]$AndroidNativeBuildDir = "build-android-arm64-ndk",
    [ValidateRange(1, 16)]
    [int]$WindowsBuildJobs = 8,
    [ValidateRange(1, 8)]
    [int]$AndroidBuildJobs = 4,
    [switch]$SkipWindows,
    [switch]$SkipAndroid,
    [switch]$InstallAndroid,
    [switch]$InstallAndroidIfConnected,
    [switch]$PlanOnly
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
} else {
    $RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
}
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path

if ($Scope -eq "current" -and [string]::IsNullOrWhiteSpace($Unit)) {
    throw "-Unit is required when -Scope current is selected."
}
if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot "CMakeLists.txt") -PathType Leaf)) {
    throw "DuneCity repository not found: $RepoRoot"
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Label,
        [Parameter(Mandatory = $true)]
        [scriptblock]$Command
    )
    Write-Host "[DEPLOY START] $Label"
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE."
    }
    Write-Host "[DEPLOY DONE] $Label"
}

$deployMutex = [System.Threading.Mutex]::new($false, "Local\DuneCitySkinTestDeploy")
$ownsDeployMutex = $deployMutex.WaitOne(0)
if (-not $ownsDeployMutex) {
    $deployMutex.Dispose()
    throw "Another DuneCity skin deployment/build is already running."
}

try {
    Push-Location $RepoRoot
    try {
        $syncScript = Join-Path $RepoRoot "scripts\sync-dunecity-skins.py"
        $syncArguments = @(
            "-u", $syncScript,
            "--source-root", $SourceRoot,
            "--repo-root", $RepoRoot
        )
        if ($Scope -eq "current") {
            $syncArguments += @("--unit", $Unit)
        }
        if ($PlanOnly) {
            $syncArguments += "--plan-only"
        }
        Invoke-Checked "DuneCity skin synchronization" {
            & $PythonExecutable @syncArguments
        }

        if (-not $PlanOnly -and -not $SkipWindows) {
            $windowsPath = if ([System.IO.Path]::IsPathRooted($WindowsBuildDir)) {
                $WindowsBuildDir
            } else {
                Join-Path $RepoRoot $WindowsBuildDir
            }
            if (-not (Test-Path -LiteralPath $windowsPath -PathType Container)) {
                throw "Windows build directory not found: $windowsPath"
            }
            Invoke-Checked "Windows DuneCity build" {
                & cmake --build $windowsPath --target dunecity --config Release --parallel $WindowsBuildJobs
            }
        }

        $installedAndroid = $false
        if (-not $PlanOnly -and -not $SkipAndroid) {
            if (-not $env:VCPKG_DOWNLOADS) {
                $env:VCPKG_DOWNLOADS = Join-Path $env:LOCALAPPDATA "vcpkg-downloads"
            }
            if (-not $env:VCPKG_MAX_CONCURRENCY) {
                $env:VCPKG_MAX_CONCURRENCY = "1"
            }
            $androidPackager = Join-Path $RepoRoot "scripts\package-android-apk.ps1"
            Invoke-Checked "Android native and APK build" {
                & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $androidPackager `
                    -RepoRoot $RepoRoot `
                    -NativeBuildDir $AndroidNativeBuildDir `
                    -BuildNative `
                    -NativeBuildJobs $AndroidBuildJobs `
                    -BuildApk
            }

            if ($InstallAndroid -or $InstallAndroidIfConnected) {
                $adb = Get-Command adb -ErrorAction Stop
                $connected = & $adb.Source devices |
                    Where-Object { $_ -match "\sdevice$" } |
                    Select-Object -First 1
                if (-not $connected -and $InstallAndroid) {
                    throw "Android install requested, but adb reports no authorized device."
                }
                if ($connected) {
                    $apk = Join-Path $RepoRoot "build-android-apk\app\build\outputs\apk\debug\DuneLegacy.apk"
                    if (-not (Test-Path -LiteralPath $apk -PathType Leaf)) {
                        throw "Android build completed without the expected APK: $apk"
                    }
                    Invoke-Checked "Android APK install" {
                        & $adb.Source install -r $apk
                    }
                    $installedAndroid = $true
                } else {
                    Write-Warning "No authorized Android device is connected; APK build succeeded and installation was skipped."
                }
            }
        }

        $summary = [ordered]@{
            scope = $Scope
            unit = if ($Scope -eq "current") { $Unit } else { $null }
            planOnly = [bool]$PlanOnly
            windows = -not $PlanOnly -and -not $SkipWindows
            android = -not $PlanOnly -and -not $SkipAndroid
            androidInstalled = $installedAndroid
        } | ConvertTo-Json -Compress
        Write-Host "[DEPLOY COMPLETE] $summary"
    } finally {
        Pop-Location
    }
} finally {
    if ($ownsDeployMutex) {
        [void]$deployMutex.ReleaseMutex()
    }
    $deployMutex.Dispose()
}
