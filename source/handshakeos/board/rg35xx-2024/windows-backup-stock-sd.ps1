# HandshakeOS — back up the stock RG35XX 2024 SD card on Windows.
# Run in an elevated PowerShell. This script only READS a disk you name.
# It will not write to the card. Put the stock card away when you are done
# and do all HandshakeOS experiments on a SECOND card.
#
# Usage (Administrator):
#   Get-Disk
#   # Find the SD reader's number carefully. Confirm size matches the card.
#   .\windows-backup-stock-sd.ps1 -DiskNumber 3 -OutFile "$env:USERPROFILE\Desktop\rg35xx-stock.img"

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [int] $DiskNumber,

    [Parameter(Mandatory = $true)]
    [string] $OutFile
)

$ErrorActionPreference = "Stop"

Write-Host "=== HandshakeOS stock-card backup (read only) ==="
Get-Disk | Format-Table Number, FriendlyName, Size, PartitionStyle, BusType, OperationalStatus

$disk = Get-Disk -Number $DiskNumber
if (-not $disk) {
    throw "No disk with number $DiskNumber. Run Get-Disk and try again."
}

$sizeGB = [math]::Round($disk.Size / 1GB, 2)
Write-Host ""
Write-Host "You asked to image disk $DiskNumber  ($($disk.FriendlyName), $sizeGB GB, $($disk.BusType))."
Write-Host "This is a READ. Nothing is written to the card."
Write-Host "Output: $OutFile"
Write-Host ""
$confirm = Read-Host "Type the disk number ($DiskNumber) again to start, or anything else to abort"
if ($confirm -ne "$DiskNumber") {
    Write-Host "Aborted. No file written."
    exit 1
}

$dest = $OutFile
$dir = Split-Path -Parent $dest
if ($dir -and -not (Test-Path $dir)) {
    New-Item -ItemType Directory -Path $dir | Out-Null
}

$srcPath = "\\.\PhysicalDrive$DiskNumber"
Write-Host "Reading $srcPath ..."

$src = [System.IO.File]::Open($srcPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
try {
    $dst = [System.IO.File]::Create($dest)
    try {
        $sha = [System.Security.Cryptography.SHA256]::Create()
        $buffer = New-Object byte[] (4MB)
        $copied = [int64]0
        $total = [int64]$disk.Size
        while ($copied -lt $total) {
            $toRead = [int][math]::Min($buffer.Length, $total - $copied)
            $n = $src.Read($buffer, 0, $toRead)
            if ($n -le 0) { break }
            $dst.Write($buffer, 0, $n)
            $sha.TransformBlock($buffer, 0, $n, $null, 0) | Out-Null
            $copied += $n
            if (($copied % 64MB) -lt $n) {
                $pct = [math]::Round(100.0 * $copied / $total, 1)
                Write-Host ("  {0:N0} / {1:N0} bytes  ({2}%)" -f $copied, $total, $pct)
            }
        }
        $sha.TransformFinalBlock([byte[]]::new(0), 0, 0) | Out-Null
        $hash = ($sha.Hash | ForEach-Object { $_.ToString("x2") }) -join ""
    } finally {
        $dst.Close()
    }
} finally {
    $src.Close()
}

$hashFile = "$dest.sha256"
Set-Content -Path $hashFile -Value "$hash  $(Split-Path -Leaf $dest)" -Encoding ascii
Write-Host ""
Write-Host "Wrote $dest  ($copied bytes)"
Write-Host "SHA-256 $hash"
Write-Host "Checksum file: $hashFile"
Write-Host ""
Write-Host "Next: eject the stock card, put it somewhere safe, and use a SECOND card for HandshakeOS."
Write-Host "A later pass can write a boot image with Rufus, balenaEtcher, or dd — never onto this stock card."
