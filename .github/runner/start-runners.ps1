# Starts the two GitHub Actions runners for BlenMiner/tide-engine, hidden:
# this machine's (label tide-windows) and the one in WSL (label tide-linux).
# A shortcut in the Startup folder runs it at logon. To stop them, end the
# Runner.Listener processes (Windows) and `pkill Runner.Listener` in WSL.

# Jobs' `shell: bash` has to be Git's: System32's bash.exe is WSL's.
$env:PATH = "C:\Program Files\Git\bin;C:\Program Files\Git\usr\bin;" + $env:PATH
# Nobody watches a job: no Tide program shows a window or a dialog.
$env:TIDE_HIDDEN = "1"

if (-not (Get-Process Runner.Listener -ErrorAction SilentlyContinue)) {
    Start-Process -FilePath "$PSScriptRoot\run.cmd" -WorkingDirectory $PSScriptRoot -WindowStyle Hidden `
        -RedirectStandardOutput "$PSScriptRoot\runner.log" -RedirectStandardError "$PSScriptRoot\runner.err.log"
}
wsl.exe -d Ubuntu -- bash -lc "pgrep -x Runner.Listener > /dev/null"
if ($LASTEXITCODE -ne 0) {
    Start-Process -FilePath "wsl.exe" -WindowStyle Hidden `
        -ArgumentList '-d', 'Ubuntu', '--', 'bash', '-lc', '~/tide-runner/start.sh'
}
