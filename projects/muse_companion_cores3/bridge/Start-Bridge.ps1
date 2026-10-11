param([string]$Port='COM4')
$ErrorActionPreference='Stop'
$repo=Resolve-Path (Join-Path $PSScriptRoot '../../..')
$python=Join-Path $repo '.tools/bridge-env/Scripts/python.exe'
if(!(Test-Path -LiteralPath $python)){throw 'Run Setup-Bridge.ps1 first.'}
& $python -u (Join-Path $PSScriptRoot 'bridge.py') --port $Port
