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
$env:CIETO_EXEC = $cietoExec
$cietoArgs = $env:CIETO_ARGS
$compareArgs = $env:CIETO_COMPARE_ARGS

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

function Invoke-CietoScript {
    param(
        [string]$TestName,
        [string]$ArgsText,
        [string]$StdoutPath,
        [string]$StderrPath
    )

    $cmdExec = $env:ComSpec
    if ([string]::IsNullOrWhiteSpace($cmdExec)) {
        $cmdExec = "cmd.exe"
    }

    $argText = ""
    if (-not [string]::IsNullOrWhiteSpace($ArgsText)) {
        $argText = " $ArgsText"
    }

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $cmdExec
    $startInfo.Arguments = '/d /s /c ""{0}"{1} "{2}" > "{3}" 2> "{4}""' -f `
        $cietoExec, `
        $argText, `
        $TestName, `
        $StdoutPath, `
        $StderrPath
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
    }

    # Wait once more so redirected output is completely flushed.
    $process.WaitForExit()

    return @{
        Finished = $finished
        ExitCode = $process.ExitCode
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
if (-not [string]::IsNullOrWhiteSpace($cietoArgs)) {
    Write-Host "CIETO_ARGS: $cietoArgs"
}
if (-not [string]::IsNullOrWhiteSpace($compareArgs)) {
    Write-Host "CIETO_COMPARE_ARGS: $compareArgs"
}
Write-Host "TIMEOUT_SEC: $timeoutSec"
Write-Host ""

foreach ($test in $testFiles) {
    Write-Host ("Running {0,-35} " -f $test.Name) -NoNewline

    $temporaryBase = Join-Path `
        ([System.IO.Path]::GetTempPath()) `
        ("cieto-test-{0}-{1}" -f $PID, [Guid]::NewGuid().ToString("N"))

    $stdoutFile = "$temporaryBase.stdout.log"
    $stderrFile = "$temporaryBase.stderr.log"
    $compareStdoutFile = "$temporaryBase.compare.stdout.log"
    $compareStderrFile = "$temporaryBase.compare.stderr.log"

    try {
        $run = Invoke-CietoScript `
            -TestName $test.Name `
            -ArgsText $cietoArgs `
            -StdoutPath $stdoutFile `
            -StderrPath $stderrFile

        if (-not $run.Finished) {
            Write-Host "[TIMEOUT]"
            Write-Host "Timed out after $timeoutSec seconds."

            Show-CapturedOutput `
                -StdoutPath $stdoutFile `
                -StderrPath $stderrFile

            $timeouts++
            $failed++
            continue
        }

        if (-not [string]::IsNullOrWhiteSpace($compareArgs)) {
            $compareRun = Invoke-CietoScript `
                -TestName $test.Name `
                -ArgsText $compareArgs `
                -StdoutPath $compareStdoutFile `
                -StderrPath $compareStderrFile

            if (-not $compareRun.Finished) {
                Write-Host "[TIMEOUT]"
                Write-Host "Compare run timed out after $timeoutSec seconds."

                Show-CapturedOutput `
                    -StdoutPath $compareStdoutFile `
                    -StderrPath $compareStderrFile

                $timeouts++
                $failed++
                continue
            }

            $sameExit = $run.ExitCode -eq $compareRun.ExitCode
            $sameStdout = (Get-Content -LiteralPath $stdoutFile -Raw) -eq `
                (Get-Content -LiteralPath $compareStdoutFile -Raw)
            $sameStderr = (Get-Content -LiteralPath $stderrFile -Raw) -eq `
                (Get-Content -LiteralPath $compareStderrFile -Raw)

            if (-not ($sameExit -and $sameStdout -and $sameStderr)) {
                Write-Host "[DIFF]"
                Write-Host "Exit: base=$($run.ExitCode), compare=$($compareRun.ExitCode)"
                Write-Host "Base output:"
                Show-CapturedOutput `
                    -StdoutPath $stdoutFile `
                    -StderrPath $stderrFile
                Write-Host "Compare output:"
                Show-CapturedOutput `
                    -StdoutPath $compareStdoutFile `
                    -StderrPath $compareStderrFile

                $failed++
                continue
            }
        }

        $exitCode = $run.ExitCode

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
            -LiteralPath $stdoutFile, $stderrFile, $compareStdoutFile, $compareStderrFile `
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
