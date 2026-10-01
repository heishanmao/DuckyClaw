<#
.SYNOPSIS
  TuyaOpenClaw Windows build wrapper.

  Windows-specific fixes handled automatically:
   1. Short physical path. The SDK-pinned ninja (1.11.1.4) cannot run commands
      longer than 32767 chars and the T5AI AP build emits ~40K compile commands
      from long absolute paths, while newer ninja binaries are blocked by this
      machine's Application Control policy. The project therefore lives at the
      short path C:\T5 (the original long path remains as a junction). If this
      script is invoked through the long junction path, a SUBST drive is used
      for the same effect.
   2. tos.py cross-drive `cd` fix: os.system("cd <path> && ...") cannot switch
      drive letters; the wrapper re-applies a `cd /d` patch to
      TuyaOpen/tools/cli_command/util.py whenever it is missing.
   3. TuyaOpen environment setup (uv / venv / toolchain) via export.ps1.

.USAGE
  .\build_win.ps1                   # build
  .\build_win.ps1 flash -p COM9     # build + flash to a given port
  .\build_win.ps1 monitor           # serial monitor (logs at 460800)
  .\build_win.ps1 clean             # tos.py clean
#>
param([string]$Action = "build", [Parameter(ValueFromRemainingArguments = $true)][string[]]$ExtraArgs)

# NOTE: keep default $ErrorActionPreference (Continue). export.ps1 writes
# native stderr lines that must NOT become terminating errors when redirected.

$app = Split-Path -Parent $MyInvocation.MyCommand.Path
$projRoot = Split-Path -Parent $app
$sdk = Join-Path $app "TuyaOpen"
$sysPython = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $sysPython) { $sysPython = "python" }

# --- 1) Short-path build via SUBST (keep ninja command lines < 32767) --------
$buildDir = $app
$substLetter = $null
$substCreated = $false
if ($app.Length -ge 55) {
    foreach ($letter in @('T','U','V','W','X','Y','Z')) {
        if (-not (Test-Path "${letter}:\")) { $substLetter = $letter; break }
    }
    if ($substLetter) {
        subst ${substLetter}: $projRoot
        $buildDir = "${substLetter}:\" + (Split-Path -Leaf $app)
        $substCreated = $true
        Write-Host "[build_win] building from short path: $buildDir (subst $substLetter -> $projRoot)"
    } else {
        Write-Host "[build_win] WARNING: no free drive letter for SUBST; long command lines may fail"
    }
}

try {
    # --- 2) TuyaOpen environment (uv sync --frozen, tos.py prepare) ----------
    . (Join-Path $sdk "export.ps1") | Out-Null

    # --- 2.5) Windows cross-drive `cd` fix for tos.py (idempotent patch) ------
    # tos.py runs subprocesses via os.system("cd <path> && ..."); plain `cd`
    # cannot switch drive letters, which breaks SUBST short-drive builds.
    # The patch is re-applied whenever it is missing (e.g. after submodule
    # re-checkout). It lives in the SDK submodule and is intentionally not
    # committed upstream.
    $utilPy = Join-Path $sdk "tools\cli_command\util.py"
    if (Test-Path $utilPy) {
        $raw = [IO.File]::ReadAllText($utilPy)
        if (-not $raw.Contains("TuyaOpenClaw win32 fix")) {
            $old = "        ret = os.system(cmd)"
            $new = "        if sys.platform == 'win32':`n            # TuyaOpenClaw win32 fix: cd /d so SUBST short-drive builds work`n            if cmd.startswith('cd ') and not cmd.startswith('cd /d '):`n                cmd = 'cd /d ' + cmd[3:]`n        ret = os.system(cmd)"
            if ($raw.Contains($old)) {
                $raw = $raw.Replace($old, $new)
                [IO.File]::WriteAllText($utilPy, $raw, (New-Object Text.UTF8Encoding($false)))
                Write-Host "[build_win] patched util.py do_subprocess (cd /d)"
            } else {
                Write-Host "[build_win] WARNING: util.py anchor not found; cd /d patch skipped"
            }
        }
    }

    # --- 3) Run tos.py --------------------------------------------------------
    # Invoke tos.py via the SHORT path: tos.py derives the SDK root from
    # sys.argv[0], so passing T:\...\TuyaOpen\tos.py makes the whole platform
    # build (where the 40K command lines live) use short paths too.
    New-Item -ItemType Directory -Force -Path (Join-Path $buildDir "TuyaOpen\.cache") | Out-Null
    New-Item -ItemType File -Force -Path (Join-Path $buildDir "TuyaOpen\.cache\.dont_prompt_update_platform") | Out-Null
    Push-Location $buildDir
    $tosArgs = @((Join-Path $buildDir "TuyaOpen\tos.py"), $Action) + $ExtraArgs
    & $env:OPEN_SDK_PYTHON $tosArgs
    $code = $LASTEXITCODE
    Pop-Location
    Write-Host "[build_win] tos.py $Action exit=$code"
    exit $code
}
finally {
    if ($substCreated) { subst ${substLetter}: /D }
}
