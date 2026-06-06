param(
    [switch]$SkipToolchain,
    [switch]$SkipSdk,
    [switch]$SkipCh32Fun,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir "..")
$ToolsDir = Join-Path $RepoRoot "tools"
$ThirdPartyDir = Join-Path $RepoRoot "third_party"

$ToolchainVersion = "15.2.0-1"
$ToolchainName = "xpack-riscv-none-elf-gcc-$ToolchainVersion"
$ToolchainDir = Join-Path $ToolsDir $ToolchainName
$ToolchainZip = Join-Path $ToolsDir "$ToolchainName-win32-x64.zip"
$ToolchainUrl = "https://sourceforge.net/projects/riscv-none-elf-gcc-xpack/files/v$ToolchainVersion/$ToolchainName-win32-x64.zip/download"

$Ch32SdkDir = Join-Path $ThirdPartyDir "ch32v20x_repo"
$Ch32SdkUrl = "https://github.com/openwch/ch32v20x.git"

$Ch32FunDir = Join-Path $ToolsDir "ch32fun"
$Ch32FunUrl = "https://github.com/cnlohr/ch32fun.git"

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Test-CommandExists {
    param([string]$Name)
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Require-Command {
    param(
        [string]$Name,
        [string]$InstallHint
    )

    if (-not (Test-CommandExists $Name)) {
        throw "Missing required command '$Name'. $InstallHint"
    }
}

function Download-File {
    param(
        [string]$Url,
        [string]$OutFile
    )

    Write-Host "Downloading:"
    Write-Host "  $Url"
    Write-Host "To:"
    Write-Host "  $OutFile"

    $ProgressPreference = "SilentlyContinue"
    Invoke-WebRequest -Uri $Url -OutFile $OutFile
}

function Clone-Or-Update {
    param(
        [string]$Url,
        [string]$Directory
    )

    if (Test-Path (Join-Path $Directory ".git")) {
        Write-Host "Updating existing repo: $Directory"
        git -C $Directory pull --ff-only
        return
    }

    if (Test-Path $Directory) {
        if (-not $Force) {
            throw "Directory already exists but is not a git repo: $Directory. Remove it or rerun with -Force."
        }
        Remove-Item -LiteralPath $Directory -Recurse -Force
    }

    git clone --depth 1 $Url $Directory
}

Write-Host "MOCE SDK CH32 Windows setup"
Write-Host "Repo: $RepoRoot"

New-Item -ItemType Directory -Force -Path $ToolsDir | Out-Null
New-Item -ItemType Directory -Force -Path $ThirdPartyDir | Out-Null

Write-Step "Checking host tools"
Require-Command "git" "Install Git for Windows: https://git-scm.com/download/win"
Require-Command "python" "Install Python 3 and make sure python.exe is in PATH."
Require-Command "cmake" "Install CMake, for example: winget install Kitware.CMake"
Require-Command "ninja" "Install Ninja, for example: winget install Ninja-build.Ninja"

if (-not $SkipToolchain) {
    Write-Step "Preparing RISC-V GCC toolchain"
    $GccExe = Join-Path $ToolchainDir "bin\riscv-none-elf-gcc.exe"
    if ((Test-Path $GccExe) -and -not $Force) {
        Write-Host "Toolchain already exists: $ToolchainDir"
    } else {
        if (-not (Test-Path $ToolchainZip) -or $Force) {
            Download-File -Url $ToolchainUrl -OutFile $ToolchainZip
        }

        if (Test-Path $ToolchainDir) {
            Remove-Item -LiteralPath $ToolchainDir -Recurse -Force
        }

        Write-Host "Extracting toolchain..."
        Expand-Archive -LiteralPath $ToolchainZip -DestinationPath $ToolsDir -Force
    }
}

if (-not $SkipSdk) {
    Write-Step "Preparing WCH CH32V20x EVT SDK"
    Clone-Or-Update -Url $Ch32SdkUrl -Directory $Ch32SdkDir
}

if (-not $SkipCh32Fun) {
    Write-Step "Preparing ch32fun / minichlink"
    Clone-Or-Update -Url $Ch32FunUrl -Directory $Ch32FunDir

    $MinichlinkExe = Join-Path $Ch32FunDir "minichlink\minichlink.exe"
    if (Test-Path $MinichlinkExe) {
        Write-Host "minichlink already exists: $MinichlinkExe"
    } else {
        $MinichlinkDir = Join-Path $Ch32FunDir "minichlink"
        if (Test-Path (Join-Path $MinichlinkDir "CMakeLists.txt")) {
            Write-Host "Building minichlink..."
            cmake -S $MinichlinkDir -B (Join-Path $MinichlinkDir "build") -G Ninja
            cmake --build (Join-Path $MinichlinkDir "build")
        } else {
            Write-Warning "minichlink CMake project not found. Flashing may require building tools/ch32fun/minichlink manually."
        }
    }
}

Write-Step "Verifying compiler"
$ExpectedGcc = Join-Path $ToolchainDir "bin\riscv-none-elf-gcc.exe"
if (Test-Path $ExpectedGcc) {
    & $ExpectedGcc --version | Select-Object -First 1
} else {
    Write-Warning "Local xPack GCC was not found at $ExpectedGcc. The build will fall back to riscv-none-elf-gcc from PATH."
}

Write-Step "Setup complete"
Write-Host "Try building:"
Write-Host "  python scripts\build.py --board ch32v203g6u6 --app led_blink"
