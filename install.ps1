# puts BML (dinput8.dll) and the built plugins into the game, run it with Banished closed
# -Remove takes them out again, NativeMods\bml.log stays
param([string]$GameDir, [switch]$Remove)

# every Steam library folder, the game can be in any of them
function Find-Banished {
    $steam = (Get-ItemProperty -Path "HKCU:\Software\Valve\Steam" -ErrorAction SilentlyContinue).SteamPath
    if (-not $steam) { return $null }
    $libraries = @($steam)
    $vdf = Join-Path $steam "steamapps\libraryfolders.vdf"
    if (Test-Path $vdf) {
        $libraries += Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"' | ForEach-Object {
            $_.Matches[0].Groups[1].Value -replace '\\\\', '\'
        }
    }
    foreach ($library in $libraries) {
        $dir = Join-Path $library "steamapps\common\Banished"
        if (Test-Path (Join-Path $dir "Application-steam-x64.exe")) { return $dir }
    }
    return $null
}

$game = if ($GameDir) { $GameDir } else { Find-Banished }
if (-not $game -or -not (Test-Path (Join-Path $game "Application-steam-x64.exe"))) {
    throw "Banished was not found, pass the game folder with -GameDir"
}
$out = Join-Path $PSScriptRoot "out"
$mods = Join-Path $game "NativeMods"

if (Get-Process -Name "Application-steam-x64" -ErrorAction SilentlyContinue) {
    throw "Banished is running and holds its dlls open, close it first"
}

if ($Remove) {
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $game "dinput8.dll")
    Get-ChildItem $out -Filter *.dll | Where-Object Name -ne "dinput8.dll" | ForEach-Object {
        Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $mods $_.Name)
    }
    "BML removed"
    return
}

New-Item -ItemType Directory -Force $mods | Out-Null
Copy-Item -Force (Join-Path $out "dinput8.dll") $game
Get-ChildItem $out -Filter *.dll | Where-Object Name -ne "dinput8.dll" | ForEach-Object {
    Copy-Item -Force $_.FullName $mods
    "plugin $($_.Name)"
}
"BML installed, the log is $mods\bml.log"
