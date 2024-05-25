param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("windows", "windows-cublas", "linux", "linux-cublas", "macos", "macos-metal")]
    [string]$BuildPlatform,

    [Parameter(Mandatory = $false)]
    [string]$Destination  = "../../ux/server/wingman",

    [Parameter(Mandatory = $false)]
    [switch]$Force
)

function Build-CMakeProject {
    param(
        [string]$platform,
        [string]$destination
    )

    # Check for VCPKG_ROOT and VCPKG_INSTALLATION_ROOT environment variables. If not set, throw an error.
    if (-not $env:VCPKG_INSTALLATION_ROOT) {
        # throw "VCPKG_INSTALLATION_ROOT environment variable is not set"
        Write-Output "VCPKG_INSTALLATION_ROOT environment variable is not set"
        if (-not $env:VCPKG_ROOT) {
            throw "VCPKG_ROOT environment variable is not set"
        }
    } else {
        Write-Output "VCPKG_INSTALLATION_ROOT: $($env:VCPKG_INSTALLATION_ROOT)"
        if (-not $env:VCPKG_ROOT) {
            Write-Output "VCPKG_ROOT is not set, setting it to VCPKG_INSTALLATION_ROOT"
            $env:VCPKG_ROOT = $env:VCPKG_INSTALLATION_ROOT
        }
        Write-Output "VCPKG_ROOT: $($env:VCPKG_ROOT)"
    }

    $presets = @("$platform")
    if ($platform -eq "macos" -or $platform -eq "macos-metal" -or $platform -eq "linux-cublas" -or $platform -eq "windows-cublas") {
        # $presets += "$platform-metal"
    }
    else {
        $presets += "$platform-cublas"
    }
    
    foreach ($preset in $presets) {
        try {
            if ($Force) {
                Write-Output "Cleaning build directory..."
                Remove-Item -Recurse -Force "./out/build/$preset" -ErrorAction SilentlyContinue
            }

            Write-Output "Building with preset: $preset"
            cmake -S . --preset=$preset | Tee-Object -Variable cmakeOutput
            if ($LASTEXITCODE -ne 0) {
                throw "CMake configuration failed for preset $preset"
            }

            # Extract build output directory from CMake output
            $buildOutputDir = ($cmakeOutput | Select-String "-- Build files have been written to: (.*)" | Out-String).Trim()
            $buildOutputDir = $buildOutputDir -replace "-- Build files have been written to: ", ""
            Write-Output "Configuration is complete."
            Write-Output "Build output directory: $buildOutputDir"

            $installDestination = Join-Path $destination $preset
            if ($Force) {
                Write-Output "Cleaning install destination directory..."
                Remove-Item -Recurse -Force $installDestination -ErrorAction SilentlyContinue
            }

            cmake --build $buildOutputDir --config Release | Tee-Object -Variable buildOutput
            if ($LASTEXITCODE -ne 0) {
                throw "CMake build (Release) failed for preset $preset"
            }

            cmake --install $buildOutputDir --prefix $installDestination | Tee-Object -Variable installOutput
            if ($LASTEXITCODE -ne 0) {
                throw "CMake install failed for preset $preset"
            }
        }
        catch {
            throw
        }
    }
}

Push-Location $PSScriptRoot

try {
    Build-CMakeProject -platform $BuildPlatform -destination $Destination
}
catch {
    Write-Error "An error occurred during the build process: $_"
    exit 1
}
finally {
    Pop-Location
}
