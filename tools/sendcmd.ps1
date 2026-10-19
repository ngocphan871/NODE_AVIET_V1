# Gui 1 lenh console vao node qua SWD (bien swd_cmd[128] trong RAM), roi in log moi.
# Word 0 (chua byte dau) ghi SAU CUNG de firmware khong doc phai lenh dang ghi do.
# Dung: powershell -File tools\sendcmd.ps1 "bq"
param(
    [Parameter(Mandatory = $true)][string]$Cmd,
    [string]$Elf = "",
    [string]$Cli = "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
    [string]$Nm  = "C:\ST\STM32CubeCLT_1.22.0\GNU-tools-for-STM32\bin\arm-none-eabi-nm.exe",
    [int]$WaitMs = 1500
)

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Elf) { $Elf = Join-Path $here "..\build\Debug\NODE_VIET_V1.elf" }
if (-not (Test-Path $Elf)) { Write-Output "Khong thay $Elf"; exit 1 }
$syms = & $Nm $Elf
$line = ($syms | Select-String ' swd_cmd$').Line
if (-not $line) { Write-Output "Khong tim thay swd_cmd trong $Elf"; exit 1 }
$addr = [Convert]::ToUInt32(($line -split ' ')[0], 16)
if ($addr -lt 0x20000000) { Write-Output "Dia chi swd_cmd sai"; exit 1 }

$bytes = [Text.Encoding]::ASCII.GetBytes($Cmd)
if ($bytes.Length -gt 126) { Write-Output "Lenh qua dai"; exit 1 }
$padded = New-Object byte[] ([Math]::Ceiling(($bytes.Length + 1) / 4) * 4)
[Array]::Copy($bytes, $padded, $bytes.Length)

$words = @()
for ($i = 0; $i -lt $padded.Length; $i += 4) { $words += [BitConverter]::ToUInt32($padded, $i) }

# Ghi cac word sau truoc, word 0 cuoi cung
for ($i = $words.Count - 1; $i -ge 0; $i--) {
    $a = "0x{0:X8}" -f ($addr + 4 * $i)
    $v = "0x{0:X8}" -f $words[$i]
    $r = & $Cli -c port=SWD mode=HOTPLUG freq=950 -w32 $a $v 2>&1
    # Word 0: firmware nhan lenh va xoa byte 0 ngay, nen buoc doc lai cua CLI bao
    # "Failed to download" du da ghi xong -> bo qua loi do o word 0.
    if (($r -join "`n") -match 'Error' -and ($i -gt 0 -or ($r -join "`n") -notmatch 'Failed to download')) {
        Write-Output "Ghi $a loi:"; $r | Select-String Error; exit 1
    }
}
Start-Sleep -Milliseconds $WaitMs
& (Join-Path $here "readlog.ps1") -Elf $Elf
