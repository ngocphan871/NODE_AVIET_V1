# Doc log printf cua node qua SWD (khong can USB-TTL).
# Firmware chep moi ky tu printf vao dbg_log[4096], dbg_log_pos = tong so byte da ghi.
# Dung: powershell -File tools\readlog.ps1 [-Elf build\Debug\NODE_VIET_V1.elf]
param(
    [string]$Elf = "",
    [string]$Cli = "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
    [string]$Nm  = "C:\ST\STM32CubeCLT_1.22.0\GNU-tools-for-STM32\bin\arm-none-eabi-nm.exe"
)

$size = 4096
if (-not $Elf) { $Elf = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "..\build\Debug\NODE_VIET_V1.elf" }
if (-not (Test-Path $Elf)) { Write-Output "Khong thay $Elf"; exit 1 }
$syms = & $Nm $Elf
$logAddr = [Convert]::ToUInt32((($syms | Select-String ' dbg_log$').Line -split ' ')[0], 16)
$posAddr = [Convert]::ToUInt32((($syms | Select-String ' dbg_log_pos$').Line -split ' ')[0], 16)

function Read-Bytes([uint32]$addr, [int]$len) {
    $out = & $Cli -c port=SWD mode=HOTPLUG freq=950 -r8 ("0x{0:X8}" -f $addr) $len 2>&1
    $bytes = New-Object System.Collections.Generic.List[byte]
    foreach ($l in $out) {
        if ($l -match '^0x[0-9A-Fa-f]{8}\s*:\s*(.*)$') {
            foreach ($h in ($Matches[1].Trim() -split '\s+')) {
                if ($h -match '^[0-9A-Fa-f]{2}$') { $bytes.Add([Convert]::ToByte($h, 16)) }
            }
        }
    }
    return ,$bytes.ToArray()
}

$p = Read-Bytes $posAddr 4
if ($p.Length -lt 4) { Write-Output "Khong doc duoc dbg_log_pos (SWD loi?)"; exit 1 }
$pos = [BitConverter]::ToUInt32($p, 0)
$buf = Read-Bytes $logAddr $size
if ($buf.Length -lt $size) { Write-Output "Doc dbg_log thieu ($($buf.Length) byte)"; exit 1 }

if ($pos -le $size) {
    $data = $buf[0..([int]$pos - 1)]
} else {
    $start = [int]($pos % $size)
    $data = $buf[$start..($size - 1)] + $buf[0..($start - 1)]
}
Write-Output "=== dbg_log_pos = $pos ==="
[Text.Encoding]::ASCII.GetString([byte[]]$data)
