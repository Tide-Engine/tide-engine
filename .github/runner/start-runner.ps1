# Starts this machine's GitHub Actions runner (label tide-windows), hidden. A
# shortcut in the Startup folder runs it at logon, from beside the runner. To
# stop it, end the Runner.Listener process.

# Jobs' `shell: bash` has to be Git's: System32's bash.exe is WSL's.
$env:PATH = "C:\Program Files\Git\bin;C:\Program Files\Git\usr\bin;" + $env:PATH
# Nobody watches a job: no Tide program shows a window or a dialog.
$env:TIDE_HIDDEN = "1"

if (-not (Get-Process Runner.Listener -ErrorAction SilentlyContinue)) {
    Start-Process -FilePath "$PSScriptRoot\run.cmd" -WorkingDirectory $PSScriptRoot -WindowStyle Hidden `
        -RedirectStandardOutput "$PSScriptRoot\runner.log" -RedirectStandardError "$PSScriptRoot\runner.err.log"
}
