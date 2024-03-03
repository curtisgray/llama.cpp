param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("windows", "linux", "macos")]
    [string]$BuildPlatform,

    [Parameter(Mandatory = $false)]
    [switch]$Force
)

function Build-CMakeProject {
    param(
        [string]$platform
    )

    # Check for VCPKG_ROOT and VCPKG_INSTALLATION_ROOT environment variables. If not set, throw an error.
    if (-not $env:VCPKG_INSTALLATION_ROOT) {
        # throw "VCPKG_INSTALLATION_ROOT environment variable is not set"
        Write-Output "VCPKG_INSTALLATION_ROOT environment variable is not set"
    } else {
        Write-Output "VCPKG_INSTALLATION_ROOT: $($env:VCPKG_INSTALLATION_ROOT)"
        if (-not $env:VCPKG_ROOT) {
            Write-Output "VCPKG_ROOT is not set, setting it to VCPKG_INSTALLATION_ROOT"
            $env:VCPKG_ROOT = $env:VCPKG_INSTALLATION_ROOT
        }
        Write-Output "VCPKG_ROOT: $($env:VCPKG_ROOT)"
    }

    if ($Force) {
        Write-Output "Cleaning build directory"
        Remove-Item -Recurse -Force "./out" -ErrorAction SilentlyContinue
    }

    $presets = @($platform)
    if ($platform -eq "macos") {
        $presets += "$platform-metal"
    }
    else {
        $presets += "$platform-cublas"
    }
    
    foreach ($preset in $presets) {
        try {
            $buildOutputDir = "./out/build/$preset"
            $installDestination = "../../ux/server/wingman/$preset"
            $installDestinationBin = Join-Path $installDestination "bin"
        
            Write-Output "Building with preset: $preset"
            cmake -S . --preset=$preset
            if ($LASTEXITCODE -ne 0) {
                throw "CMake configuration failed for preset $preset" 
            }

            cmake --build $buildOutputDir --config Debug
            if ($LASTEXITCODE -ne 0) {
                throw "CMake build (Debug) failed for preset $preset" 
            }

            cmake --build $buildOutputDir --config Release
            if ($LASTEXITCODE -ne 0) {
                throw "CMake build (Release) failed for preset $preset" 
            }

            cmake --install $buildOutputDir --prefix $installDestination
            if ($LASTEXITCODE -ne 0) {
                throw "CMake install failed for preset $preset" 
            }
        
            if ($platform -eq "windows") {
                Write-Output "Copying DLLs for Windows build"
                $installSource = "$buildOutputDir/bin/Release/*.dll"
                Copy-Item -Path $installSource -Destination $installDestinationBin -Force
            }
        }
        catch {
            throw
        }
    }
}

Push-Location $PSScriptRoot

try {
    Build-CMakeProject -platform $BuildPlatform
}
catch {
    Write-Error "An error occurred during the build process: $_"
    exit 1
}
finally {
    Pop-Location
}
