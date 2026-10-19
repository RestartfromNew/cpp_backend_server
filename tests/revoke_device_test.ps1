# Destructive integration test: use a disposable active device owned by the login.
param(
    [string]$BaseUrl = 'http://localhost:8081',
    [Parameter(Mandatory = $true)][string]$DeviceId,
    [switch]$ConfirmTestDeviceRevocation
)
$ErrorActionPreference = 'Stop'
if (-not $ConfirmTestDeviceRevocation) {
    throw 'Pass -ConfirmTestDeviceRevocation only for a disposable test device.'
}
$token = Read-Host 'Access Token (not logged by this script)'
$securePassword = Read-Host 'Current account password' -AsSecureString
$password = [System.Net.NetworkCredential]::new('', $securePassword).Password
$wsUri = [UriBuilder]::new($BaseUrl)
$wsUri.Scheme = if ($wsUri.Scheme -eq 'https') { 'wss' } else { 'ws' }
$sockets = @()
$receives = @()
$cancel = [System.Threading.CancellationTokenSource]::new()
$cancel.CancelAfter(30000)
try {
    # The second connection replaces the online index. The first must still close.
    for ($i = 0; $i -lt 2; $i++) {
        $socket = [System.Net.WebSockets.ClientWebSocket]::new()
        $sockets += $socket
        $socket.Options.SetRequestHeader('Authorization', "Bearer $token")
        $socket.Options.SetRequestHeader('deviceID', $DeviceId)
        $socket.ConnectAsync($wsUri.Uri, $cancel.Token).GetAwaiter().GetResult()
        $buffer = [byte[]]::new(4096)
        $receives += $socket.ReceiveAsync([ArraySegment[byte]]::new($buffer), $cancel.Token)
    }
    $body = @{ deviceID = $DeviceId; password = $password } | ConvertTo-Json -Compress
    $requestArgs = @{
        Method = 'Post'; Uri = "$($BaseUrl.TrimEnd('/'))/revoke_device"
        Headers = @{ Authorization = "Bearer $token" }
        ContentType = 'application/json'; Body = $body; TimeoutSec = 20
    }
    $first = Invoke-RestMethod @requestArgs
    if ($first.status -ne 'revoked' -or $first.websocket_closed -ne $true -or
        $first.already_revoked -ne $false) { throw 'First revocation not confirmed.' }
    foreach ($receive in $receives) {
        $result = $receive.GetAwaiter().GetResult()
        if ($result.MessageType -ne [System.Net.WebSockets.WebSocketMessageType]::Close -or
            [int]$result.CloseStatus -ne 4001 -or $result.CloseStatusDescription -ne 'device_revoked') {
            throw 'Expected Close 4001/device_revoked on both connections.'
        }
    }
    $repeat = Invoke-RestMethod @requestArgs
    if ($repeat.status -ne 'revoked' -or $repeat.websocket_closed -ne $true -or
        $repeat.already_revoked -ne $true) { throw 'Repeated revocation not confirmed.' }
    Write-Output 'PASS: both connections closed with reason; repeat confirmed.'
} finally {
    $cancel.Cancel()
    foreach ($socket in $sockets) { $socket.Dispose() }
    $cancel.Dispose()
    Remove-Variable token, password, securePassword, body, requestArgs -ErrorAction SilentlyContinue
}
