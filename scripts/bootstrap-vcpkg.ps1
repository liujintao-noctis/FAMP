$ErrorActionPreference = "Stop"

$VcpkgDir = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { "C:\vcpkg" }

if (-not (Test-Path (Join-Path $VcpkgDir ".git"))) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $VcpkgDir) | Out-Null
    git clone https://github.com/microsoft/vcpkg.git $VcpkgDir
}

& (Join-Path $VcpkgDir "bootstrap-vcpkg.bat")

Write-Host "vcpkg is ready at $VcpkgDir"
Write-Host "Configure with: mkdir build; cd build; cmake .. -DCMAKE_BUILD_TYPE=Release -DVCPKG_ROOT=$VcpkgDir"
