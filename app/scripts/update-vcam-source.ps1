# Development helper (run elevated): installs a freshly built media source DLL over the registered
# one in %ProgramData%\DJI VCam and restarts the camera services so they load it.
param([Parameter(Mandatory = $true)][string]$SourceDll)
$ErrorActionPreference = 'Stop'
$folder = Join-Path $env:ProgramData 'DJI VCam'
$target = Join-Path $folder 'djivcam-source.dll'
New-Item -ItemType Directory -Force $folder | Out-Null
Stop-Service FrameServerMonitor, FrameServer -Force -ErrorAction SilentlyContinue
if (Test-Path $target) { Move-Item -Force $target "$target.old" -ErrorAction SilentlyContinue }  # a loaded DLL can be renamed
Copy-Item -Force $SourceDll $target
& regsvr32.exe /s $target
Remove-Item -Force "$target.old" -ErrorAction SilentlyContinue
Start-Service FrameServer -ErrorAction SilentlyContinue
