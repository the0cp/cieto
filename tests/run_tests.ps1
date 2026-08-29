Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$timeoutSec = 5

if (-not [string]::IsNullOrWhiteSpace($env:TIMEOUT_SEC)) {
    $timeoutSec = [int]$env:TIMEOUT_SEC
}

$cietoExec = $env:CIETO_EXEC

if ([string]::IsNullOrWhiteSpace($cietoExec)) {
    $candidates = @(
        (Join-Path $PSScriptRoot "..\build\debug\cieto.exe"),
        (Join-Path $PSScriptRoot "..\build\release\cieto.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            $cietoExec = $candidate
            break
        }
    }
}

if ([string]::IsNullOrWhiteSpace($cietoExec) -or
    -not (Test-Path -LiteralPath $cietoExec)) {
    Write-Error @"
Cieto executable was not found.

Set CIETO_EXEC explicitly, for example:

    `$env:CIETO_EXEC = "..\build\debug\cieto.exe"
    .\run_tests.ps1
"@
    exit 1
}

$cietoExec = (Resolve-Path -LiteralPath $cietoExec).Path

function Show-CapturedOutput {
    param(
        [string]$StdoutPath,
        [string]$StderrPath
    )

    $lines = @()

    if (Test-Path -LiteralPath $StdoutPath) {
        $lines += @(Get-Content -LiteralPath $StdoutPath)
    }

    if (Test-Path -LiteralPath $StderrPath) {
        $lines += @(Get-Content -LiteralPath $StderrPath)
    }

    if ($lines.Count -eq 0) {
        Write-Host "No output captured."
        return
    }

    $lines |
        Select-Object -First 120 |
        ForEach-Object {
            Write-Host $_
        }
}

$testFiles = Get-ChildItem `
    -LiteralPath $PSScriptRoot `
    -Filter "test_*.cies" |
    Where-Object {
        -not $_.PSIsContainer
    } |
    Sort-Object Name

$passed = 0
$failed = 0
$timeouts = 0

Write-Host "Starting tests..."
Write-Host "CIETO_EXEC: $cietoExec"
Write-Host "TIMEOUT_SEC: $timeoutSec"
Write-Host ""

foreach ($test in $testFiles) {
    Write-Host ("Running {0,-35} " -f $test.Name) -NoNewline

    $temporaryBase = Join-Path `
        ([System.IO.Path]::GetTempPath()) `
        ("cieto-test-{0}-{1}" -f $PID, [Guid]::NewGuid().ToString("N"))

    $stdoutFile = "$temporaryBase.stdout.log"
    $stderrFile = "$temporaryBase.stderr.log"

    try {
        $cmdExec = $env:ComSpec
        if ([string]::IsNullOrWhiteSpace($cmdExec)) {
            $cmdExec = "cmd.exe"
        }

        $startInfo = New-Object System.Diagnostics.ProcessStartInfo
        $startInfo.FileName = $cmdExec
        $startInfo.Arguments = '/d /s /c ""{0}" "{1}" > "{2}" 2> "{3}""' -f `
            $cietoExec, `
            $test.Name, `
            $stdoutFile, `
            $stderrFile
        $startInfo.WorkingDirectory = $PSScriptRoot
        $startInfo.UseShellExecute = $false
        $startInfo.CreateNoWindow = $true

        $process = New-Object System.Diagnostics.Process
        $process.StartInfo = $startInfo

        [void]$process.Start()

        $finished = $process.WaitForExit($timeoutSec * 1000)

        if (-not $finished) {
            try {
                $process.Kill()
            }
            catch {
                # The process may already have exited.
            }

            $process.WaitForExit()

            Write-Host "[TIMEOUT]"
            Write-Host "Timed out after $timeoutSec seconds."

            Show-CapturedOutput `
                -StdoutPath $stdoutFile `
                -StderrPath $stderrFile

            $timeouts++
            $failed++
            continue
        }

        # Wait once more so redirected output is completely flushed.
        $process.WaitForExit()

        $exitCode = $process.ExitCode

        if ($exitCode -eq 0) {
            Write-Host "[PASS]"
            $passed++
        }
        elseif ($exitCode -lt 0) {
            Write-Host "[CRASH: exit $exitCode]"

            Show-CapturedOutput `
                -StdoutPath $stdoutFile `
                -StderrPath $stderrFile

            $failed++
        }
        else {
            Write-Host "[FAIL: exit $exitCode]"

            Show-CapturedOutput `
                -StdoutPath $stdoutFile `
                -StderrPath $stderrFile

            $failed++
        }
    }
    catch {
        Write-Host "[ERROR]"
        Write-Host $_.Exception.Message
        $failed++
    }
    finally {
        Remove-Item `
            -LiteralPath $stdoutFile, $stderrFile `
            -Force `
            -ErrorAction SilentlyContinue
    }
}

Write-Host ""
Write-Host "========================================"
Write-Host "Summary: $passed Passed, $failed Failed ($timeouts Timeouts)"
Write-Host "========================================"

if ($failed -eq 0) {
    exit 0
}

exit 1
