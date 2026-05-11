param(
  [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'

$TargetRate = 16000
$Volume = 30
$TrimPadMs = 28
$TargetPeak = 0.82

$Assets = @(
  @{ Name = 'r99_draw';      Path = 'gun_sound\r99\qiechur99.wav' },
  @{ Name = 'r99_reload';    Path = 'gun_sound\r99\r99reload.wav' },
  @{ Name = 'heping_draw';   Path = 'gun_sound\heping\qiechuheping.wav' },
  @{ Name = 'heping_reload'; Path = 'gun_sound\heping\hepingreload.wav' },
  @{ Name = 'fuzhu_draw';    Path = 'gun_sound\fuzhu\qiechufuzhu.wav' },
  @{ Name = 'fuzhu_reload';  Path = 'gun_sound\fuzhu\fuzhureload.wav' },
  @{ Name = 'kuwu_draw';     Path = 'kuwu_sound\qiechukuwu.wav'; Peak = 0.72 }
)

function Read-U32LE([byte[]]$Bytes, [int]$Offset) {
  return [BitConverter]::ToUInt32($Bytes, $Offset)
}

function Read-U16LE([byte[]]$Bytes, [int]$Offset) {
  return [BitConverter]::ToUInt16($Bytes, $Offset)
}

function Read-WavMonoSamples([string]$Path) {
  [byte[]]$bytes = [IO.File]::ReadAllBytes($Path)
  if ($bytes.Length -lt 44) {
    throw "WAV too short: $Path"
  }
  $riff = [Text.Encoding]::ASCII.GetString($bytes, 0, 4)
  $wave = [Text.Encoding]::ASCII.GetString($bytes, 8, 4)
  if ($riff -ne 'RIFF' -or $wave -ne 'WAVE') {
    throw "Not a RIFF/WAVE file: $Path"
  }

  $fmtOffset = -1
  $fmtSize = 0
  $dataOffset = -1
  $dataSize = 0
  $pos = 12
  while ($pos + 8 -le $bytes.Length) {
    $id = [Text.Encoding]::ASCII.GetString($bytes, $pos, 4)
    $size = [int](Read-U32LE $bytes ($pos + 4))
    $chunkData = $pos + 8
    if ($id -eq 'fmt ') {
      $fmtOffset = $chunkData
      $fmtSize = $size
    } elseif ($id -eq 'data') {
      $dataOffset = $chunkData
      $dataSize = $size
    }
    $pos = $chunkData + $size
    if (($size % 2) -ne 0) {
      $pos++
    }
  }

  if ($fmtOffset -lt 0 -or $dataOffset -lt 0) {
    throw "Missing fmt/data chunk: $Path"
  }
  if ($fmtSize -lt 16) {
    throw "Unsupported fmt chunk: $Path"
  }

  $audioFormat = Read-U16LE $bytes $fmtOffset
  $channels = Read-U16LE $bytes ($fmtOffset + 2)
  $sampleRate = [int](Read-U32LE $bytes ($fmtOffset + 4))
  $blockAlign = Read-U16LE $bytes ($fmtOffset + 12)
  $bitsPerSample = Read-U16LE $bytes ($fmtOffset + 14)

  if ($audioFormat -ne 1) {
    throw "Only PCM WAV is supported: $Path"
  }
  if ($channels -lt 1) {
    throw "Invalid channel count: $Path"
  }
  if ($bitsPerSample -ne 8 -and $bitsPerSample -ne 16 -and $bitsPerSample -ne 24 -and $bitsPerSample -ne 32) {
    throw "Unsupported bit depth $bitsPerSample`: $Path"
  }

  $bytesPerSample = [int]($bitsPerSample / 8)
  $frames = [int]($dataSize / $blockAlign)
  [double[]]$samples = New-Object double[] $frames

  for ($i = 0; $i -lt $frames; $i++) {
    $sum = 0.0
    $frameOffset = $dataOffset + ($i * $blockAlign)
    for ($ch = 0; $ch -lt $channels; $ch++) {
      $off = $frameOffset + ($ch * $bytesPerSample)
      if ($bitsPerSample -eq 8) {
        $v = ([int]$bytes[$off] - 128) / 128.0
      } elseif ($bitsPerSample -eq 16) {
        $v = [BitConverter]::ToInt16($bytes, $off) / 32768.0
      } elseif ($bitsPerSample -eq 24) {
        $raw = ([int]$bytes[$off]) -bor (([int]$bytes[$off + 1]) -shl 8) -bor (([int]$bytes[$off + 2]) -shl 16)
        if (($raw -band 0x800000) -ne 0) {
          $raw = $raw -bor -16777216
        }
        $v = $raw / 8388608.0
      } else {
        $v = [BitConverter]::ToInt32($bytes, $off) / 2147483648.0
      }
      $sum += $v
    }
    $samples[$i] = $sum / $channels
  }

  return [pscustomobject]@{
    Samples = $samples
    Rate = $sampleRate
    Frames = $frames
    Channels = $channels
    Bits = $bitsPerSample
  }
}

function Convert-ToPcm8([double[]]$Samples, [int]$SourceRate, [int]$TargetRate, [string]$Name, [double]$PeakTarget) {
  $peak = 0.0
  foreach ($s in $Samples) {
    $a = [Math]::Abs($s)
    if ($a -gt $peak) {
      $peak = $a
    }
  }
  $threshold = [Math]::Max(0.0015, [Math]::Min(0.010, $peak * 0.035))
  $first = 0
  while ($first -lt $Samples.Length -and [Math]::Abs($Samples[$first]) -lt $threshold) {
    $first++
  }
  $last = $Samples.Length - 1
  while ($last -gt $first -and [Math]::Abs($Samples[$last]) -lt $threshold) {
    $last--
  }
  if ($first -ge $Samples.Length) {
    $first = 0
    $last = $Samples.Length - 1
  }

  $pad = [int][Math]::Round($SourceRate * $TrimPadMs / 1000.0)
  $first = [Math]::Max(0, $first - $pad)
  $last = [Math]::Min($Samples.Length - 1, $last + $pad)
  $trimmedFrames = $last - $first + 1
  $outLen = [int][Math]::Max(1, [Math]::Round($trimmedFrames * $TargetRate / [double]$SourceRate))
  [byte[]]$out = New-Object byte[] $outLen

  $gain = 1.0
  if ($peak -gt 0.0001) {
    $gain = $PeakTarget / $peak
  }
  if ($gain -gt 28.0) {
    $gain = 28.0
  }

  for ($i = 0; $i -lt $outLen; $i++) {
    $srcPos = $first + ($i * $SourceRate / [double]$TargetRate)
    $i0 = [int][Math]::Floor($srcPos)
    if ($i0 -lt $first) {
      $i0 = $first
    }
    if ($i0 -gt $last) {
      $i0 = $last
    }
    $i1 = [Math]::Min($last, $i0 + 1)
    $frac = $srcPos - $i0
    $v = ($Samples[$i0] * (1.0 - $frac) + $Samples[$i1] * $frac) * $gain
    if ($v -gt 1.0) {
      $v = 1.0
    } elseif ($v -lt -1.0) {
      $v = -1.0
    }
    $b = [int][Math]::Round(128.0 + ($v * 127.0))
    if ($b -lt 0) {
      $b = 0
    } elseif ($b -gt 255) {
      $b = 255
    }
    $out[$i] = [byte]$b
  }

  return [pscustomobject]@{
    Bytes = $out
    First = $first
    Last = $last
    OriginalFrames = $Samples.Length
    Threshold = $threshold
    Gain = $gain
  }
}

function Format-CArray([string]$ArrayName, [byte[]]$Bytes) {
  $sb = New-Object Text.StringBuilder
  [void]$sb.AppendLine("const uint8_t $ArrayName`[] = {")
  for ($i = 0; $i -lt $Bytes.Length; $i += 24) {
    $count = [Math]::Min(24, $Bytes.Length - $i)
    $line = New-Object string[] $count
    for ($j = 0; $j -lt $count; $j++) {
      $line[$j] = [string]$Bytes[$i + $j]
    }
    [void]$sb.AppendLine("    " + (($line -join ', ') + ','))
  }
  [void]$sb.AppendLine("};")
  return $sb.ToString()
}

$outPath = Join-Path $Root 'Audio\WeaponActionSfx8.generated.inc'
$sb = New-Object Text.StringBuilder
[void]$sb.AppendLine("/* Auto-generated by tools/GenerateWeaponActionSfx8.ps1.")
[void]$sb.AppendLine(" * Source WAVs are trimmed, mixed to mono, resampled to 16 kHz,")
[void]$sb.AppendLine(" * and stored as unsigned 8-bit PCM for Audio_Play().")
[void]$sb.AppendLine(" */")
[void]$sb.AppendLine("")

$total = 0
foreach ($asset in $Assets) {
  $src = Join-Path $Root $asset.Path
  $wav = Read-WavMonoSamples $src
  $peakTarget = if ($asset.ContainsKey('Peak')) { [double]$asset.Peak } else { $TargetPeak }
  $pcm = Convert-ToPcm8 $wav.Samples $wav.Rate $TargetRate $asset.Name $peakTarget
  $arrayName = "$($asset.Name)_sfx8_pcm8"
  $soundName = "$($asset.Name)_sfx8"
  [void]$sb.AppendLine("/* $($asset.Path) -> $soundName, $($pcm.Bytes.Length) samples */")
  [void]$sb.Append((Format-CArray $arrayName $pcm.Bytes))
  [void]$sb.AppendLine("const Audio_Sound_t $soundName = { .samples = $arrayName, .length = $($pcm.Bytes.Length), .volume = $Volume };")
  [void]$sb.AppendLine("")
  $total += $pcm.Bytes.Length
  $trimMs = [Math]::Round((($pcm.OriginalFrames - ($pcm.Last - $pcm.First + 1)) * 1000.0) / $wav.Rate, 1)
  Write-Host ("{0,-14} {1,6} samples  src {2} Hz/{3}ch/{4}bit  gain {5,5:n1}x  trimmed {6} ms" -f $asset.Name, $pcm.Bytes.Length, $wav.Rate, $wav.Channels, $wav.Bits, $pcm.Gain, $trimMs)
}

$encoding = New-Object Text.UTF8Encoding $false
[IO.File]::WriteAllText($outPath, $sb.ToString(), $encoding)
Write-Host "Generated $outPath ($total bytes of PCM data)"
