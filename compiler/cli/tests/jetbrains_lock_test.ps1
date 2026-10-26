param([string]$Tide, [string]$Plugin)
$env:TIDE_HIDDEN = '1'
$locked = [System.IO.File]::Open($Plugin, 'Open', 'Read', 'Read')
try {
    & $Tide editors --update
    exit $LASTEXITCODE
} finally {
    $locked.Dispose()
}
