param(
    [Parameter(Mandatory=$true)]
    [string]$InputWav,

    [Parameter(Mandatory=$true)]
    [string]$OutputHeader,

    [Parameter(Mandatory=$true)]
    [string]$OutputSource,

    [Parameter(Mandatory=$true)]
    [string]$SymbolBase,

    [int]$TargetRate = 8000,
    [ValidateRange(0,100)]
    [int]$Volume = 70
)

$ErrorActionPreference = 'Stop'

function Read-WavPcm([string]$path) {
    $br = [System.IO.BinaryReader]::new([System.IO.File]::OpenRead($path))
    try {
        $riff = [Text.Encoding]::ASCII.GetString($br.ReadBytes(4))
        [void]$br.ReadUInt32()
        $wave = [Text.Encoding]::ASCII.GetString($br.ReadBytes(4))
        if ($riff -ne 'RIFF' -or $wave -ne 'WAVE') {
            throw "Not a WAV file: $path"
        }

        $audioFormat = 0
        $channels = 0
        $sampleRate = 0
        $bits = 0
        $dataOffset = 0
        $dataSize = 0

        while ($br.BaseStream.Position -lt $br.BaseStream.Length) {
            $cid = [Text.Encoding]::ASCII.GetString($br.ReadBytes(4))
            if ($cid.Length -lt 4) { break }

            $csize = $br.ReadUInt32()
            $start = $br.BaseStream.Position

            if ($cid -eq 'fmt ') {
                $audioFormat = $br.ReadUInt16()
                $channels = $br.ReadUInt16()
                $sampleRate = $br.ReadUInt32()
                [void]$br.ReadUInt32()
                [void]$br.ReadUInt16()
                $bits = $br.ReadUInt16()
            }
            elseif ($cid -eq 'data') {
                $dataOffset = $br.BaseStream.Position
                $dataSize = $csize
                break
            }

            $br.BaseStream.Position = $start + $csize
            if (($csize % 2) -eq 1) {
                $br.BaseStream.Position += 1
            }
        }

        if ($audioFormat -ne 1) { throw "Only PCM WAV supported. audioFormat=$audioFormat" }
        if ($channels -lt 1 -or $channels -gt 2) { throw "Only mono/stereo supported. channels=$channels" }
        if ($bits -ne 8 -and $bits -ne 16) { throw "Only 8/16-bit WAV supported. bits=$bits" }
        if ($dataSize -le 0) { throw "No data chunk found" }

        $bytesPerSample = [int]($bits / 8)
        $frameSize = $bytesPerSample * $channels
        $srcFrames = [int]($dataSize / $frameSize)

        $br.BaseStream.Position = $dataOffset
        $src = New-Object 'double[]' $srcFrames

        for ($i = 0; $i -lt $srcFrames; $i++) {
            $sum = 0.0
            for ($ch = 0; $ch -lt $channels; $ch++) {
                if ($bits -eq 16) {
                    $v = [double]$br.ReadInt16() / 32768.0
                } else {
                    $u = $br.ReadByte()
                    $v = ([double]$u - 128.0) / 128.0
                }
                $sum += $v
            }
            $src[$i] = $sum / $channels
        }

        return [PSCustomObject]@{
            Samples = $src
            SampleRate = $sampleRate
            Channels = $channels
            BitsPerSample = $bits
        }
    }
    finally {
        $br.Close()
    }
}

function Resample-Linear([double[]]$src, [int]$srcRate, [int]$dstRate) {
    if ($srcRate -le 0 -or $dstRate -le 0) { throw "Invalid sample rate" }
    if ($src.Length -lt 2) { throw "Input too short" }

    $dstFrames = [int][Math]::Floor($src.Length * $dstRate / [double]$srcRate)
    if ($dstFrames -le 0) { throw "Output length is zero" }

    $dst = New-Object 'UInt16[]' $dstFrames

    for ($i = 0; $i -lt $dstFrames; $i++) {
        $pos = $i * ($srcRate / [double]$dstRate)
        $i0 = [int][Math]::Floor($pos)
        if ($i0 -ge ($src.Length - 1)) { $i0 = $src.Length - 2 }
        if ($i0 -lt 0) { $i0 = 0 }

        $frac = $pos - $i0
        $s = $src[$i0] * (1.0 - $frac) + $src[$i0 + 1] * $frac
        if ($s -gt 1.0) { $s = 1.0 }
        if ($s -lt -1.0) { $s = -1.0 }

        $u12 = [int][Math]::Round(($s * 2047.0) + 2048.0)
        if ($u12 -lt 0) { $u12 = 0 }
        if ($u12 -gt 4095) { $u12 = 4095 }

        $dst[$i] = [uint16]$u12
    }

    return $dst
}

function Write-Header([string]$path, [string]$arrayName, [string]$songName) {
    $text = @"
#pragma once
#include <stdint.h>
#include "Audio.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const uint16_t $arrayName[];
extern const Audio_Sound12_t $songName;

#ifdef __cplusplus
}
#endif
"@
    [System.IO.File]::WriteAllText($path, $text, [Text.Encoding]::ASCII)
}

function Write-Source([string]$path, [string]$headerName, [string]$arrayName, [string]$songName, [uint16[]]$samples, [int]$volume) {
    $sw = [System.IO.StreamWriter]::new($path, $false, [Text.Encoding]::ASCII)
    try {
        $sw.WriteLine("#include `"$headerName`"")
        $sw.WriteLine()
        $sw.WriteLine("const uint16_t $arrayName[] = {")

        $perLine = 16
        for ($i = 0; $i -lt $samples.Length; $i += $perLine) {
            $end = [Math]::Min($i + $perLine - 1, $samples.Length - 1)
            $nums = for ($j = $i; $j -le $end; $j++) { $samples[$j].ToString() }
            $line = "    " + ($nums -join ", ")
            if ($end -lt ($samples.Length - 1)) { $line += "," }
            $sw.WriteLine($line)
        }

        $sw.WriteLine("};")
        $sw.WriteLine()
        $sw.WriteLine("const Audio_Sound12_t $songName = { .samples = $arrayName, .length = $($samples.Length), .volume = $volume };")
    }
    finally {
        $sw.Close()
    }
}

$wav = Read-WavPcm -path $InputWav
$samples12 = Resample-Linear -src $wav.Samples -srcRate $wav.SampleRate -dstRate $TargetRate

$arrayName = "${SymbolBase}_12bit"
$songName = $SymbolBase
$headerName = [System.IO.Path]::GetFileName($OutputHeader)

Write-Header -path $OutputHeader -arrayName $arrayName -songName $songName
Write-Source -path $OutputSource -headerName $headerName -arrayName $arrayName -songName $songName -samples $samples12 -volume $Volume

Write-Host "Generated: $OutputHeader"
Write-Host "Generated: $OutputSource"
Write-Host "Input: rate=$($wav.SampleRate), bits=$($wav.BitsPerSample), ch=$($wav.Channels)"
Write-Host "Output: rate=$TargetRate, samples=$($samples12.Length), volume=$Volume"
