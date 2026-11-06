# Build the RM bluetooth remote APK without Gradle:
#   aapt2 link -> base.apk, javac -> classes, d8 -> classes.dex, zipalign, apksigner.
# Toolchain: JDK + Android SDK build-tools/platforms. Override with -Jdk / -Sdk / -Bt.
param(
    [string]$Jdk = 'C:\AndroidBuild\jdk-17.0.20.1+1',
    [string]$Sdk = 'C:\AndroidBuild\sdk',
    [string]$Bt  = '34.0.0',
    [string]$Api = 'android-34',
    [string]$OutApk = ''
)

$ErrorActionPreference = 'Continue'
$root = $PSScriptRoot
$btDir = Join-Path $Sdk "build-tools\$Bt"
$ajar  = Join-Path $Sdk "platforms\$Api\android.jar"
$java  = Join-Path $Jdk 'bin\java.exe'
$javac = Join-Path $Jdk 'bin\javac.exe'
$keytool = Join-Path $Jdk 'bin\keytool.exe'
foreach ($p in @($java, $javac, $btDir, $ajar)) { if (-not (Test-Path $p)) { throw "missing: $p" } }

$build = Join-Path $root 'build'
$obj   = Join-Path $build 'obj'
$dex   = Join-Path $build 'dex'
Remove-Item $build -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $obj, $dex -Force | Out-Null

# 1) javac
$src = @(Get-ChildItem (Join-Path $root 'src') -Filter *.java -Recurse | ForEach-Object { $_.FullName })
$javacArgs = @('--release', '11', '-encoding', 'UTF-8', '-nowarn', '-cp', $ajar, '-d', $obj) + $src
& $javac @javacArgs
if ($LASTEXITCODE -ne 0) { throw 'javac failed' }

# 2) d8 -> build/dex/classes.dex
$classes = @(Get-ChildItem $obj -Filter *.class -Recurse | ForEach-Object { $_.FullName })
$d8Args = @('-cp', (Join-Path $btDir 'lib\d8.jar'), 'com.android.tools.r8.D8',
            '--release', '--lib', $ajar, '--min-api', '21', '--output', $dex) + $classes
& $java @d8Args
if ($LASTEXITCODE -ne 0) { throw 'd8 failed' }

# 3) aapt2 link -> base.apk
$baseApk = Join-Path $build 'base.apk'
& (Join-Path $btDir 'aapt2.exe') link -o $baseApk --manifest (Join-Path $root 'AndroidManifest.xml') `
    -I $ajar --min-sdk-version 21 --target-sdk-version 34 --version-code 4 --version-name 2.1
if ($LASTEXITCODE -ne 0) { throw 'aapt2 link failed' }

# 4) inject classes.dex
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::Open($baseApk, 'Update')
try {
    $old = $zip.GetEntry('classes.dex')
    if ($old) { $old.Delete() }
    [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
        $zip, (Join-Path $dex 'classes.dex'), 'classes.dex',
        [System.IO.Compression.CompressionLevel]::Optimal)
} finally { $zip.Dispose() }

# 5) zipalign
$aligned = Join-Path $build 'aligned.apk'
& (Join-Path $btDir 'zipalign.exe') -f 4 $baseApk $aligned
if ($LASTEXITCODE -ne 0) { throw 'zipalign failed' }

# 6) debug keystore + sign + verify
$ks = Join-Path $build 'debug.keystore'
& $keytool -genkeypair -keystore $ks -storepass android -keypass android -alias androiddebugkey `
    -keyalg RSA -keysize 2048 -validity 10000 -dname 'CN=RM Debug, O=RM, C=CN' *> $null
if (-not (Test-Path $ks)) { throw 'keytool failed' }

$signerJar = Join-Path $btDir 'lib\apksigner.jar'
$signed = Join-Path $build 'RM-Car-Remote.apk'
& $java -jar $signerJar sign --ks $ks --ks-pass pass:android --key-pass pass:android --out $signed $aligned
if ($LASTEXITCODE -ne 0) { throw 'apksigner failed' }
$verifyOut = & $java -jar $signerJar verify --print-certs $signed
$verifyCode = $LASTEXITCODE
$verifyOut | Select-Object -First 3
if ($verifyCode -ne 0) { throw 'verify failed' }

if ($OutApk) { Copy-Item $signed $OutApk -Force }
Write-Host "OK -> $signed"
Get-Item $signed | ForEach-Object { "size: $([math]::Round($_.Length/1KB,1)) KB" }