param(
    [Parameter(Mandatory=$true)]
    [ValidateSet("windows", "linux", "macos")]
    [string]$BuildPlatform,

    [Parameter(Mandatory=$false)]
    [switch]$Force
)

function Build-CMakeProject {
    param(
        [string]$platform
    )

    if ($Force) {
        # Clean the build directory
        Write-Output "Cleaning build directory"
        Remove-Item -Recurse -Force out
    }

    $presets = @($platform)
    if ($platform -eq "macos") {
        $presets += "$platform-metal"
    } else {
        $presets += "$platform-cublas"
    }
    
    foreach ($preset in $presets) {
        $buildOutputDir = "./out/build/$preset"
        $installDestination = "../../ux/server/wingman/$preset/bin"
        
        Write-Output "Building with preset: $preset"
        cmake -S . --preset=$preset
        cmake --build $buildOutputDir --config Debug
        cmake --build $buildOutputDir --config Release
        cmake --install $installDestination
        
        # Copy all DLLs to the bin folder for Windows builds only
        if ($platform -eq "windows") {
            Write-Output "Copying DLLs for Windows build"
            $installSource = "$buildOutputDir/bin/Release/*.dll"
            Copy-Item -Path $installSource -Destination $installDestination -Force
        }
    }
}

Push-Location $PSScriptRoot

# Call the function for both presets
Build-CMakeProject -platform $BuildPlatform

Pop-Location
