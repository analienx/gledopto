param(
    [ValidateSet('eu','weu','us','cn','in')]
    [string]$Region = 'weu',

    [string]$PcapInterface,

    [switch]$ResetCredentials,

    [switch]$Phase2Only,

    [string]$DeviceId = 'auto'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$localDir = Join-Path $repoRoot '.local'
$credentialPath = Join-Path $localDir 'tuya-credentials.clixml'
$pythonTool = Join-Path $repoRoot 'tools\tuya_glsd_migrate.py'
$venvPython = Join-Path $repoRoot '.venv-tuya\Scripts\python.exe'
$pythonExe = if (Test-Path $venvPython) { $venvPython } else { 'python' }

New-Item -ItemType Directory -Force -Path $localDir | Out-Null

function Save-TuyaCredentials {
    Write-Host ''
    Write-Host 'Enter Tuya Cloud Project credentials.' -ForegroundColor Cyan
    Write-Host 'They are stored ONLY on this Windows account/machine under .local/ using DPAPI encryption.' -ForegroundColor Yellow
    Write-Host 'They are never written to Git, GitHub, issue comments, or normal log output.' -ForegroundColor Yellow
    Write-Host ''

    $accessId = Read-Host 'Tuya Access ID'
    if ([string]::IsNullOrWhiteSpace($accessId)) {
        throw 'Tuya Access ID cannot be empty.'
    }

    $accessSecret = Read-Host 'Tuya Access Secret' -AsSecureString
    $projectId = Read-Host 'Tuya Project ID / Code (optional; press Enter to skip)'

    $record = [pscustomobject]@{
        Schema = 1
        AccessId = $accessId.Trim()
        AccessSecret = $accessSecret
        ProjectId = $projectId.Trim()
        Created = (Get-Date).ToUniversalTime().ToString('o')
    }

    $record | Export-Clixml -Path $credentialPath
    Write-Host "[OK] Encrypted credentials stored at $credentialPath" -ForegroundColor Green
}

if ($ResetCredentials -and (Test-Path $credentialPath)) {
    Remove-Item -Force $credentialPath
}

if (-not (Test-Path $credentialPath)) {
    Save-TuyaCredentials
}

$record = Import-Clixml -Path $credentialPath
if (-not $record.AccessId -or -not $record.AccessSecret) {
    throw "Credential file is incomplete: $credentialPath. Re-run with -ResetCredentials."
}

$bstr = [IntPtr]::Zero
$plainSecret = $null
$exitCode = 1
try {
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($record.AccessSecret)
    $plainSecret = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)

    $env:TUYA_ACCESS_ID = [string]$record.AccessId
    $env:TUYA_ACCESS_KEY = $plainSecret

    if ($record.ProjectId) {
        $env:TUYA_PROJECT_ID = [string]$record.ProjectId
    }

    Write-Host "[OK] Tuya credentials loaded into this child process only." -ForegroundColor Green
    Write-Host "[INFO] Region: $Region; DeviceId: $DeviceId" -ForegroundColor Cyan

    if ($Phase2Only) {
        Write-Host '[INFO] Resuming Tuya extraction only; no Z2M snapshot/reset/re-pair step will run.' -ForegroundColor Cyan
        $argsList = @(
            $pythonTool,
            'tuya-watch',
            '--region', $Region,
            '--device-id', $DeviceId,
            '--watch'
        )
    }
    else {
        $argsList = @(
            $pythonTool,
            'guided',
            '--region', $Region,
            '--device-id', $DeviceId
        )
    }

    if ($PcapInterface) {
        $argsList += @('--pcap-interface', $PcapInterface)
    }

    & $pythonExe @argsList
    $exitCode = $LASTEXITCODE
}
finally {
    if (Test-Path Env:TUYA_ACCESS_ID) { Remove-Item Env:TUYA_ACCESS_ID }
    if (Test-Path Env:TUYA_ACCESS_KEY) { Remove-Item Env:TUYA_ACCESS_KEY }
    if (Test-Path Env:TUYA_PROJECT_ID) { Remove-Item Env:TUYA_PROJECT_ID }

    $plainSecret = $null
    if ($bstr -ne [IntPtr]::Zero) {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
    }
}

exit $exitCode
