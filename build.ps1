$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force build | Out-Null
    & cl /nologo /EHsc /std:c++17 /utf-8 /O2 /MT native/main.cpp /Fe:build/WinMTR-Comfort.exe /Fo:build/main.obj /link /SUBSYSTEM:WINDOWS comctl32.lib ws2_32.lib iphlpapi.lib comdlg32.lib gdi32.lib user32.lib shell32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Application compilation failed' }
    foreach ($test in @('history_test', 'session_test')) {
        & cl /nologo /EHsc /std:c++17 /utf-8 /O2 /MT "tests/$test.cpp" "/Fe:build/$test.exe" "/Fo:build/$test.obj"
        if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $test" }
        & "./build/$test.exe" "build/$test.data"
        if ($LASTEXITCODE -ne 0) { throw "Test failed: $test" }
    }
} finally { Pop-Location }
