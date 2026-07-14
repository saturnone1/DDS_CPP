param(
    [Parameter(Mandatory = $true)] [string] $CppCli,
    [Parameter(Mandatory = $true)] [string] $Dotnet,
    [Parameter(Mandatory = $true)] [string] $CSharpProject,
    [Parameter(Mandatory = $true)] [string] $Config
)

$ErrorActionPreference = 'Stop'

function Quote-Argument([string] $Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Assert-Received([string] $Text, [string] $Direction) {
    foreach ($expected in @('TimeTickInformation', 'TCC', 'TDS')) {
        if ($Text -notmatch [regex]::Escape($expected)) {
            throw "$Direction did not receive expected value '$expected'."
        }
    }
}

function Start-Subscriber(
    [string] $Kind,
    [string] $CSharpCli,
    [string] $OutputPath,
    [string] $ErrorPath
) {
    $common = "subscribe TimeTickInformation --config $(Quote-Argument $Config) --count 1 --timeout-ms 20000"
    if ($Kind -eq 'csharp') {
        return Start-Process -FilePath $Dotnet `
            -ArgumentList "$(Quote-Argument $CSharpCli) $common" `
            -RedirectStandardOutput $OutputPath `
            -RedirectStandardError $ErrorPath `
            -WindowStyle Hidden `
            -PassThru
    }

    return Start-Process -FilePath $CppCli `
        -ArgumentList $common `
        -RedirectStandardOutput $OutputPath `
        -RedirectStandardError $ErrorPath `
        -WindowStyle Hidden `
        -PassThru
}

function Invoke-Publisher([string] $Kind, [string] $CSharpCli) {
    if ($Kind -eq 'csharp') {
        & $Dotnet $CSharpCli publish TimeTickInformation --config $Config --repeat 20 --interval-ms 300
    }
    else {
        & $CppCli publish TimeTickInformation --config $Config --repeat 20 --interval-ms 300
    }

    if ($LASTEXITCODE -ne 0) {
        throw "$Kind publisher exited with code $LASTEXITCODE."
    }
}

function Invoke-Direction(
    [int] $Domain,
    [string] $Subscriber,
    [string] $Publisher,
    [string] $CSharpCli,
    [string] $TempDirectory
) {
    $env:DDS_DOMAIN_ID = [string] $Domain
    $name = "$Subscriber-from-$Publisher"
    $outputPath = Join-Path $TempDirectory "$name.out.txt"
    $errorPath = Join-Path $TempDirectory "$name.err.txt"
    $process = Start-Subscriber $Subscriber $CSharpCli $outputPath $errorPath

    try {
        Start-Sleep -Seconds 2
        Invoke-Publisher $Publisher $CSharpCli

        if (-not $process.WaitForExit(25000)) {
            $process.Kill()
            throw "$name subscriber timed out."
        }
        $process.WaitForExit()

        $output = Get-Content -LiteralPath $outputPath -Raw -ErrorAction SilentlyContinue
        $errors = Get-Content -LiteralPath $errorPath -Raw -ErrorAction SilentlyContinue
        if ($process.ExitCode -ne 0) {
            throw "$name subscriber exited with code $($process.ExitCode).`n$output`n$errors"
        }

        Assert-Received $output $name
        Write-Host "PASS $name on domain $Domain"
    }
    finally {
        if (-not $process.HasExited) {
            $process.Kill()
        }
        $process.Dispose()
    }
}

foreach ($path in @($CppCli, $Dotnet, $CSharpProject, $Config)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required interop input does not exist: $path"
    }
}

& $Dotnet build $CSharpProject -c Release --nologo
if ($LASTEXITCODE -ne 0) {
    throw "C# CLI build exited with code $LASTEXITCODE."
}

$projectDirectory = Split-Path -Parent $CSharpProject
$csharpCli = Get-ChildItem -LiteralPath (Join-Path $projectDirectory 'bin\Release') `
    -Recurse -Filter ddsclient.dll |
    Sort-Object LastWriteTimeUtc -Descending |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $csharpCli) {
    throw 'The built C# ddsclient.dll was not found.'
}

$tempDirectory = Join-Path ([IO.Path]::GetTempPath()) ("dds-interop-" + [System.Guid]::NewGuid())
$previousDomain = $env:DDS_DOMAIN_ID
New-Item -ItemType Directory -Path $tempDirectory | Out-Null

try {
    $baseDomain = 80 + ([System.Diagnostics.Process]::GetCurrentProcess().Id % 100)
    Invoke-Direction $baseDomain 'csharp' 'cpp' $csharpCli $tempDirectory
    Invoke-Direction ($baseDomain + 1) 'cpp' 'csharp' $csharpCli $tempDirectory
}
finally {
    $env:DDS_DOMAIN_ID = $previousDomain
    Remove-Item -LiteralPath $tempDirectory -Recurse -Force -ErrorAction SilentlyContinue
}
