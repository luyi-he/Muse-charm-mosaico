param([string]$TextPath,[string]$WavePath,[string]$Voice='Microsoft Huihui',[switch]$ListVoices)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null=[Windows.Media.SpeechSynthesis.SpeechSynthesizer,Windows.Media.SpeechSynthesis,ContentType=WindowsRuntime]
$voices=@([Windows.Media.SpeechSynthesis.SpeechSynthesizer]::AllVoices | Where-Object {$_.Language -eq 'zh-CN'})
if($ListVoices){ConvertTo-Json -InputObject @($voices | ForEach-Object {$_.DisplayName}) -Compress;exit}
function Await-Result($Operation,$ResultType){
    $method=[System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {$_.Name -eq 'AsTask' -and $_.IsGenericMethod -and $_.GetGenericArguments().Count -eq 1 -and $_.GetParameters().Count -eq 1} | Select-Object -First 1
    $task=$method.MakeGenericMethod($ResultType).Invoke($null,@($Operation))
    $task.GetAwaiter().GetResult()
}
$speech=New-Object Windows.Media.SpeechSynthesis.SpeechSynthesizer
$stream=$null;$reader=$null
try {
    $selected=$voices | Where-Object {$_.DisplayName -eq $Voice} | Select-Object -First 1
    if(-not $selected){throw 'Requested Chinese voice is not installed'}
    $speech.Voice=$selected
    $stream=Await-Result ($speech.SynthesizeTextToStreamAsync([System.IO.File]::ReadAllText($TextPath,[System.Text.Encoding]::UTF8))) ([Windows.Media.SpeechSynthesis.SpeechSynthesisStream])
    $null=[Windows.Storage.Streams.DataReader,Windows.Storage.Streams,ContentType=WindowsRuntime]
    $reader=New-Object Windows.Storage.Streams.DataReader($stream)
    $null=Await-Result ($reader.LoadAsync([uint32]$stream.Size)) ([uint32])
    $bytes=New-Object byte[] ([int]$stream.Size)
    $reader.ReadBytes($bytes)
    [System.IO.File]::WriteAllBytes($WavePath,$bytes)
} finally {
    if($reader){$reader.Dispose()}
    if($stream){$stream.Dispose()}
    $speech.Dispose()
}
