[CmdletBinding()]
param([string]$Executable='.\mona-miner-prototype.exe',[ValidateRange(0,2147483647)][int]$Device=0)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$secret=$null;$process=$null;$bstr=[IntPtr]::Zero;$wire=$null;$credentials=$null
try {
 $endpoint=Read-Host 'USER pool endpoint'
 $worker=Read-Host 'USER worker'
 $secret=Read-Host 'USER mining password (not saved)' -AsSecureString
 $bstr=[Runtime.InteropServices.Marshal]::SecureStringToBSTR($secret)
 $credentials=@{endpoint=$endpoint;worker=$worker;password=[Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)}
 $wire=$credentials | ConvertTo-Json -Compress
 $si=New-Object Diagnostics.ProcessStartInfo
 $si.FileName=(Resolve-Path -LiteralPath $Executable).Path
 $si.Arguments='-a lyra2v2 --credentials-stdin --device '+[string]$Device
 $si.UseShellExecute=$false;$si.RedirectStandardInput=$true
 $process=New-Object Diagnostics.Process;$process.StartInfo=$si
 if(-not $process.Start()){throw 'START_FAILED'}
 $process.StandardInput.WriteLine($wire);$process.StandardInput.Close()
 $credentials.Clear();$wire=$null
 $process.WaitForExit();$code=$process.ExitCode
} catch { Write-Error 'MINER_START_FAILED_PRIVATE_INPUT_NOT_LOGGED';$code=2 }
finally {
 if($bstr -ne [IntPtr]::Zero){[Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)}
 if($secret){$secret.Dispose()};if($credentials){$credentials.Clear()};$wire=$null
 if($process){$process.Dispose()}
}
exit $code
