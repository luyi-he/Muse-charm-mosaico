param([string]$TextPath,[string]$WavePath)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Speech
$speech = New-Object System.Speech.Synthesis.SpeechSynthesizer
try {
    $voices=$speech.GetInstalledVoices() | Where-Object {$_.Enabled -and $_.VoiceInfo.Culture.Name -eq 'zh-CN'}
    if($voices){$speech.SelectVoice(($voices | Select-Object -First 1).VoiceInfo.Name)}
    $format=New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000,[System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,[System.Speech.AudioFormat.AudioChannel]::Mono)
    $speech.SetOutputToWaveFile($WavePath,$format)
    $speech.Speak([System.IO.File]::ReadAllText($TextPath,[System.Text.Encoding]::UTF8))
} finally {$speech.Dispose()}
