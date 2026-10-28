param()

# Get the root path
$Kestrel_ROOT = Split-Path -Parent $MyInvocation.MyCommand.Path

function Kestrel_Setup {
    Write-Host "Setting up the KestrelSimulator environment..."

    $msys64Path = "C:\msys64"
    $msys2DownloadUrl = "https://github.com/msys2/msys2-installer/releases/download/2024-01-13/msys2-x86_64-20240113.exe"
    $msys2InstallerPath = "msys2-installer.exe"

    if (-not (Test-Path -Path $msys64Path)) {
        if (-not (Test-Path -Path $msys2InstallerPath)) {
            Write-Host "Downloading MSYS2..."
            $ProgressPreference = 'SilentlyContinue'
            Invoke-WebRequest `
                -Uri $msys2DownloadUrl `
                -OutFile $msys2InstallerPath
            $ProgressPreference = 'Continue'
            Write-Host "MSYS2 Downloaded"
        }

        # Install MSYS2 silently
        Write-Host "Installing MSYS2..."
        Start-Process -FilePath $msys2InstallerPath -ArgumentList @(
            "install",
            "--confirm-command",
            "--accept-messages",
            "--root", $msys64Path
        ) -Wait -NoNewWindow
        Remove-Item -Path $msys2InstallerPath -Force
        Write-Host "MSYS2 Installation completed"
    } else {
        Write-Host "MSYS2 already installed at $msys64Path"
    }

    # Update MSYS2 and install necessary packages
    Write-Host "Updating MSYS2 and installing required packages..."
    
    # First update the package database and core system
    & "$msys64Path\usr\bin\bash.exe" -lc "pacman -Syu --noconfirm"
    
    # Install development packages
    & "$msys64Path\usr\bin\bash.exe" -lc "pacman -S --noconfirm base-devel mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-gdb"
    
    # Clean package cache
    & "$msys64Path\usr\bin\bash.exe" -lc "pacman -Scc --noconfirm"
    
    Write-Host "Environment setup completed!"
}
function Kestrel_Clean {
    Write-Host "Cleaning build artifacts..."
    if (Test-Path "$Kestrel_ROOT\Build") {
        Remove-Item -Recurse -Force "$Kestrel_ROOT\Build"
    }
    if (Test-Path "$Kestrel_ROOT\Binary") {
        Remove-Item -Recurse -Force "$Kestrel_ROOT\Binary"
    }
    Write-Host "Clean complete"
}

function Kestrel_Build {
    param([string]$Mode)
    # Release is CMake's RelWithDebInfo: -O2, with symbols kept so gdb still reads it.
    $buildType = if ($Mode -eq "Release") { "RelWithDebInfo" } else { "Debug" }
    Write-Host "Building KestrelSimulator ($Mode)..."
    $currentLocation = Get-Location

    try {
        New-Item -ItemType Directory -Force -Path "$Kestrel_ROOT\Build" | Out-Null
        New-Item -ItemType Directory -Force -Path "$Kestrel_ROOT\Binary" | Out-Null

        Set-Location "$Kestrel_ROOT"

        # Use Ninja generator with MinGW and override shell
        $env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
        $env:ComSpec = "C:\Windows\System32\cmd.exe"
        cmake -G "Ninja" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_MAKE_PROGRAM=ninja "-DCMAKE_BUILD_TYPE=$buildType" "-DCMAKE_EXE_LINKER_FLAGS=-static -static-libgcc -static-libstdc++" -B Build
        if ($LASTEXITCODE -eq 0) {
            cmake --build Build
        }
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    finally {
        Set-Location $currentLocation
    }
}

function Kestrel_Test {
    & "$Kestrel_ROOT\Binary\KestrelSimulator.exe" --self-test
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Kestrel_Run {
    param([string[]]$ExeArgs)
    # No arguments opens the editor; --Run-args pass straight through (e.g. --run COMPONENT).
    $exe = "$Kestrel_ROOT\Binary\KestrelSimulator.exe"
    if ($ExeArgs.Count -gt 0) {
        Start-Process -FilePath $exe -ArgumentList $ExeArgs -WorkingDirectory "$Kestrel_ROOT\Binary" -NoNewWindow
    } else {
        Start-Process -FilePath $exe -WorkingDirectory "$Kestrel_ROOT\Binary" -NoNewWindow
    }
}

# Parse command-line arguments
$shouldSetup = $false
$shouldClean = $false
$shouldBuild = $false
$shouldRun = $false
$shouldTest = $false
$modeArg = "Debug"
$runArgs = @()

foreach ($arg in $args) {
    switch -Regex ($arg) {
        "^--[Ss]etup$" { $shouldSetup = $true }
        "^--[Cc]lean$" { $shouldClean = $true }
        "^--[Bb]uild$" { $shouldBuild = $true }
        "^--[Rr]un$" { $shouldRun = $true }
        "^--[Tt]est$" { $shouldTest = $true }
        "^--[Mm]ode=([Dd]ebug|[Rr]elease)$" { $modeArg = (Get-Culture).TextInfo.ToTitleCase($matches[1].ToLower()) }
        "^--[Aa]rgs=(.+)$" { $runArgs = $matches[1] -split ' ' }
        "^--[Hh]elp$" {
            Write-Host "Usage: .\kestrel.ps1 [--Setup] [--Clean] [--Build] [--Mode=Debug|Release] [--Test] [--Run] [--Args=""...""] [--Help]"
            Write-Host "  --Setup: Install MSYS2 and the MinGW toolchain (gcc, cmake, ninja, gdb)"
            Write-Host "  --Clean: Remove Build\ and Binary\"
            Write-Host "  --Build: Build Binary\KestrelSimulator.exe"
            Write-Host "  --Mode=Debug|Release: Build type (default Debug); Release is optimized (-O2) with symbols"
            Write-Host "  --Test: Run the simulator's self test"
            Write-Host "  --Run: Start the simulator (the editor, unless --Args say otherwise)"
            Write-Host "  --Args=""..."": Arguments for --Run, e.g. --Args=""--run MyBoard --program prog.bin"""
            Write-Host "  --Help: Show this help message"
            Write-Host ""
            Write-Host "Operations are executed in order: Setup -> Clean -> Build -> Test -> Run"
            exit 0
        }
        default {
            Write-Warning "Unknown argument: $arg"
            Write-Host "Use --Help for usage information"
        }
    }
}

if ($shouldSetup) { Kestrel_Setup }
if ($shouldClean) { Kestrel_Clean }
if ($shouldBuild) { Kestrel_Build -Mode $modeArg }
if ($shouldTest) { Kestrel_Test }
if ($shouldRun) { Kestrel_Run -ExeArgs $runArgs }

if ($args.Count -eq 0) {
    Write-Host "No arguments provided. Use --Help for usage information."
    Write-Host "Available options: --Setup, --Clean, --Build, --Test, --Run"
}
