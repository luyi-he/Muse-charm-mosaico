$ErrorActionPreference='Stop'
$repo=Resolve-Path (Join-Path $PSScriptRoot '../../..')
$venv=Join-Path $repo '.tools/bridge-env'
python -m venv $venv
if($LASTEXITCODE -ne 0){throw 'Python 3.11 or newer is required.'}
$python=Join-Path $venv 'Scripts/python.exe'
& $python -m pip install --upgrade pip
& $python -m pip install -r (Join-Path $PSScriptRoot 'requirements.txt')
if($LASTEXITCODE -ne 0){throw 'Bridge dependency installation failed.'}
& $python -c "from faster_whisper import WhisperModel; WhisperModel('base', device='cpu', compute_type='int8')"
if($LASTEXITCODE -ne 0){throw 'Speech model download failed.'}
Write-Host 'Ready. Run Start-Bridge.ps1 and open http://127.0.0.1:8765.'
