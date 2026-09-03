param(
    [ValidateSet('auto','eu','weu','us','ueaz','cn','in')]
    [string]$Region = 'auto',

    [string]$PcapInterface,

    [switch]$ResetCredentials,

    [switch]$Phase2Only,

    [string]$DeviceId = 'auto'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$localDir = Join-Path $repoRoot '.local'
$credentialPath = Join-Path $localDir 'tuya-credentials.ps1'
$legacyCredentialPath = Join-Path $localDir 'tuya-credentials.clixml'
$pythonTool = Join-Path $repoRoot 'tools\tuya_glsd_migrate.py'
$venvPython = Join-Path $repoRoot '.venv-tuya\Scripts\python.exe'
$pythonExe = if (Test-Path $venvPython) { $venvPython } else { 'python' }

New-Item -ItemType Directory -Force -Path $localDir | Out-Null

function ConvertTo-PsSingleQuotedLiteral([string]$Value) {
    if ($null -eq $Value) { $Value = '' }
    return "'" + $Value.Replace("'", "''") + "'"
}

function Save-TuyaCredentialsPlain([string]$AccessId, [string]$AccessKey, [string]$ProjectId = '') {
    if ([string]::IsNullOrWhiteSpace($AccessId) -or [string]::IsNullOrWhiteSpace($AccessKey)) {
        throw 'Tuya Access ID and Access Secret cannot be empty.'
    }
    $lines = @(
        '# LOCAL/THROWAWAY credentials for this GLEDOPTO extraction only. .local/ is gitignored.',
        ('$TuyaAccessId = ' + (ConvertTo-PsSingleQuotedLiteral $AccessId.Trim())),
        ('$TuyaAccessKey = ' + (ConvertTo-PsSingleQuotedLiteral $AccessKey)),
        ('$TuyaProjectId = ' + (ConvertTo-PsSingleQuotedLiteral $ProjectId.Trim()))
    )
    [IO.File]::WriteAllLines($credentialPath, $lines, [Text.UTF8Encoding]::new($false))
    Write-Host "[OK] Plain local Tuya credentials stored at $credentialPath" -ForegroundColor Green
}

if ($ResetCredentials) {
    if (Test-Path $credentialPath) { Remove-Item -Force $credentialPath }
    if (Test-Path $legacyCredentialPath) { Remove-Item -Force $legacyCredentialPath }
}

# One-time migration from the previous DPAPI file. This avoids asking for the key again.
if (-not (Test-Path $credentialPath) -and (Test-Path $legacyCredentialPath)) {
    Write-Host '[INFO] Migrating previous encrypted Tuya credentials to disposable plaintext .local file...' -ForegroundColor Cyan
    $legacy = Import-Clixml -Path $legacyCredentialPath
    $bstr = [IntPtr]::Zero
    try {
        $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($legacy.AccessSecret)
        $legacySecret = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
        Save-TuyaCredentialsPlain -AccessId ([string]$legacy.AccessId) -AccessKey $legacySecret -ProjectId ([string]$legacy.ProjectId)
    }
    finally {
        $legacySecret = $null
        if ($bstr -ne [IntPtr]::Zero) {
            [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
        }
    }
    Remove-Item -Force $legacyCredentialPath
    Write-Host '[OK] Legacy DPAPI credential file removed after migration.' -ForegroundColor Green
}

if (-not (Test-Path $credentialPath)) {
    Write-Host ''
    Write-Host 'Enter the throwaway Tuya Cloud Project credentials once.' -ForegroundColor Cyan
    Write-Host 'They will be hardcoded only in .local/tuya-credentials.ps1 (gitignored).' -ForegroundColor Yellow
    $accessId = Read-Host 'Tuya Access ID'
    $accessKey = Read-Host 'Tuya Access Secret'
    $projectId = Read-Host 'Tuya Project ID / Code (optional; press Enter to skip)'
    Save-TuyaCredentialsPlain -AccessId $accessId -AccessKey $accessKey -ProjectId $projectId
}

. $credentialPath
if ([string]::IsNullOrWhiteSpace([string]$TuyaAccessId) -or [string]::IsNullOrWhiteSpace([string]$TuyaAccessKey)) {
    throw "Credential file is incomplete: $credentialPath. Re-run with -ResetCredentials."
}

$exitCode = 1
try {
    $env:TUYA_ACCESS_ID = [string]$TuyaAccessId
    $env:TUYA_ACCESS_KEY = [string]$TuyaAccessKey
    if (-not [string]::IsNullOrWhiteSpace([string]$TuyaProjectId)) {
        $env:TUYA_PROJECT_ID = [string]$TuyaProjectId
    }

    Write-Host '[OK] Local Tuya credentials loaded.' -ForegroundColor Green
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
}
exit $exitCode
