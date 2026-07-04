# Build Python executable with PyInstaller
# Output: standalone EXE with fast startup (onedir mode)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$OutputDir = Join-Path $ProjectRoot "dist"

# Clean old build
Remove-Item -Recurse -Force "$ProjectRoot\build", "$ProjectRoot\dist", "$ProjectRoot\*.spec" -ErrorAction SilentlyContinue

# Build with PyInstaller - onedir for fast startup
pyinstaller --noconfirm `
    --onedir `
    --windowed `
    --name "MecchaChameleonTools" `
    --icon NUL `
    --add-data "$ProjectRoot\meccha_chameleon_tools\meccha-camouflage.exe;meccha_chameleon_tools" `
    --add-data "$ProjectRoot\meccha_chameleon_tools\meccha-xenos-injector.exe;meccha_chameleon_tools" `
    --add-data "$ProjectRoot\meccha_chameleon_tools\esp_config.json;meccha_chameleon_tools" `
    --exclude-module tkinter `
    --exclude-module matplotlib `
    --exclude-module PIL `
    --exclude-module scipy `
    --exclude-module numpy `
    --exclude-module pandas `
    --exclude-module notebook `
    --exclude-module h5py `
    --exclude-module sqlalchemy `
    --exclude-module boto3 `
    --exclude-module botocore `
    --hidden-import PyQt5.sip `
    --hidden-import PyQt5.QtCore `
    --hidden-import PyQt5.QtGui `
    --hidden-import PyQt5.QtWidgets `
    "$ProjectRoot\meccha_chameleon_tools\__main__.py"

if ($LASTEXITCODE -eq 0) {
    Write-Output "`nBuild OK: $OutputDir\MecchaChameleonTools\"
    $totalSize = (Get-ChildItem -Recurse "$OutputDir\MecchaChameleonTools" | Measure-Object -Property Length -Sum).Sum
    Write-Output "Total size: $([math]::Round($totalSize / 1MB, 1)) MB"
} else {
    Write-Error "Build failed"
    exit 1
}
