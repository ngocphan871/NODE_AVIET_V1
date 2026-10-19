# Theo doi IC sac BQ25798 qua SWD: doc ban sao thanh ghi (bien bq.reg trong RAM, firmware cap nhat moi giay)
# va in VBUS / IBUS / VBAT / IBAT / VINDPM / trang thai moi vai giay.
# Dung: powershell -File tools\bqwatch.ps1 [-Seconds 240] [-Interval 5]
param(
    [int]$Seconds = 240,
    [int]$Interval = 5,
    [string]$Elf = "",
    [string]$Cli = "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
    [string]$Nm  = "C:\ST\STM32CubeCLT_1.22.0\GNU-tools-for-STM32\bin\arm-none-eabi-nm.exe"
)

if (-not $Elf) { $Elf = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "..\build\Debug\NODE_VIET_V1.elf" }
$line = (& $Nm $Elf | Select-String ' bq$').Line
if (-not $line) { Write-Output "Khong tim thay bien bq"; exit 1 }
$addr = [Convert]::ToUInt32(($line -split ' ')[0], 16)    # bq.reg[] la truong dau tien

function U16($r, $i) { return ($r[$i] -shl 8) -bor $r[$i + 1] }
function S16($r, $i) { $v = U16 $r $i; if ($v -ge 32768) { $v -= 65536 }; return $v }

$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
    $out = & $Cli -c port=SWD mode=HOTPLUG freq=950 -r8 ("0x{0:X8}" -f $addr) 0x49 2>&1
    $r = New-Object System.Collections.Generic.List[int]
    foreach ($l in $out) {
        if ($l -match '^0x[0-9A-Fa-f]{8}\s*:\s*(.*)$') {
            foreach ($h in ($Matches[1].Trim() -split '\s+')) { if ($h -match '^[0-9A-Fa-f]{2}$') { $r.Add([Convert]::ToInt32($h, 16)) } }
        }
    }
    $ts = "{0,4:N0}s" -f ((Get-Date) - $t0).TotalSeconds
    if ($r.Count -lt 0x49) { Write-Output "$ts doc SWD loi"; Start-Sleep -Seconds $Interval; continue }
    $chg = $r[0x1C] -shr 5
    "{0} VBUS {1,5} IBUS {2,5} VBAT {3,5} IBAT {4,5} VINDPM {5,5} CHG {6} STAT0 {7:X2} MPPT {8}" -f $ts,
        (U16 $r 0x35), (S16 $r 0x31), (U16 $r 0x3B), (S16 $r 0x33), ($r[0x05] * 100), $chg, $r[0x1B], ($r[0x15] -band 1)
    Start-Sleep -Seconds $Interval
}
