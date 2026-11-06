param([string]$Compiler = 'gcc')
$ErrorActionPreference = 'Stop'
$projectRoot = Join-Path $PSScriptRoot '../firmware/Mecanum-Car'
$outputDir = Join-Path ([System.IO.Path]::GetTempPath()) ('rm-car-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outputDir | Out-Null
Push-Location $projectRoot
try {
    $cases = @(
        @{ Name = 'chassis'; Source = 'Core/Src/rm_chassis.c'; Test = 'tests/chassis_follow_test.c'; Extra = @('-lm') },
        @{ Name = 'vision'; Source = 'Core/Src/rm_vision.c'; Test = 'tests/vision_test.c'; Extra = @() },
        @{ Name = 'servo_bus'; Source = 'Core/Src/rm_servo.c'; Test = 'tests/servo_bus_test.c'; Extra = @('-include', 'tests/servo_host_stubs.h') },
        @{ Name = 'shooter_pwm'; Source = 'Core/Src/rm_shooter.c'; Test = 'tests/shooter_pwm_test.c'; Extra = @('-include', 'tests/servo_host_stubs.h') }
    )
    foreach ($case in $cases) {
        $exePath = Join-Path $outputDir ($case.Name + '.exe')
        & $Compiler -std=c99 -Wall -Wextra -Werror -DRM_HOST_TEST -I Core/Inc $case.Source $case.Test @($case.Extra) -o $exePath
        if ($LASTEXITCODE -ne 0) { throw "Compile failed: $($case.Name)" }
        & $exePath
        if ($LASTEXITCODE -ne 0) { throw "Test failed: $($case.Name)" }
    }
    Write-Host "All firmware host tests passed. Binaries: $outputDir"
}
finally { Pop-Location }
