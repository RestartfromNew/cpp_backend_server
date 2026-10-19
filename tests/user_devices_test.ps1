#requires -Version 5.1
<#
Run from PowerShell:
  .\tests\user_devices_test.ps1
  .\tests\user_devices_test.ps1 -FriendUserId 'BOB_USER_UUID' -SessionDeviceId 'ALICE_ACTIVE_DEVICE_UUID'
Default server: localhost:8082. Prompts for Alice's password (not saved).
Or supply -AccessToken and -UserId to skip login.
No device registration/revocation, prekey consumption, envelope insertion or ACK writes.
WebSocket tests deliberately submit mismatched device IDs and expect rejection.
Does NOT test actual encryption, successful delivery, offline recovery or revoke races.
#>
[CmdletBinding()]
param(
    [string]$BaseUrl = 'http://localhost:8082',
    [string]$Email = 'alice_chat01@example.com',
    [string]$AccessToken = '',
    [string]$UserId = '',
    [string]$FriendUserId = '',
    [string]$SessionDeviceId = ''
)

$ErrorActionPreference = 'Stop'
$script:passed = 0
$script:failed = 0
$script:skipped = 0
$client = $null
$ws = $null
$cts = $null
$exitCode = 1

function Assert-Test([bool]$Condition, [string]$Name) {
    if ($Condition) {
        $script:passed++
        Write-Host "[PASS] $Name" -ForegroundColor Green
    } else {
        $script:failed++
        Write-Host "[FAIL] $Name" -ForegroundColor Red
    }
}

function Skip-Test([string]$Name) {
    $script:skipped++
    Write-Host "[SKIP] $Name" -ForegroundColor Yellow
}

function Invoke-Api([string]$Path, [object]$Body, [bool]$Authenticate = $true) {
    $request = [System.Net.Http.HttpRequestMessage]::new(
        [System.Net.Http.HttpMethod]::Post, "$BaseUrl$Path")
    $response = $null
    try {
        if ($Authenticate) {
            $request.Headers.Authorization = [System.Net.Http.Headers.AuthenticationHeaderValue]::new('Bearer', $AccessToken)
        }
        $json = ConvertTo-Json -InputObject $Body -Depth 10 -Compress
        $request.Content = [System.Net.Http.StringContent]::new($json, [System.Text.Encoding]::UTF8, 'application/json')
        $response = $client.SendAsync($request).GetAwaiter().GetResult()
        $text = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        $parsed = $null
        try { $parsed = ConvertFrom-Json -InputObject $text } catch { }
        # Do not log response bodies: login responses contain credentials.
        return [pscustomobject]@{ Status = [int]$response.StatusCode; Body = $parsed }
    } finally {
        if ($null -ne $response) { $response.Dispose() }
        $request.Dispose()
    }
}

function Test-Error([string]$Path, [object]$Body, [int]$Status, [string]$Code, [string]$Name) {
    $result = Invoke-Api $Path $Body
    Assert-Test ($result.Status -eq $Status -and $result.Body.error.code -eq $Code) "$Name (HTTP $($result.Status), code=$($result.Body.error.code))"
}

function Test-DeviceList([object]$Result, [string]$Target, [bool]$Own, [string]$Name) {
    Assert-Test ($Result.Status -eq 200) "$Name HTTP 200"
    if ($Result.Status -ne 200) { return }
    $body = $Result.Body
    Assert-Test ($body.target_user_id -eq $Target -and $body.friend_id -eq $Target) "$Name target IDs"
    Assert-Test ($body.is_own_device -is [bool] -and $body.is_own_device -eq $Own) "$Name ownership flag"
    Assert-Test ($body.devices -is [System.Array]) "$Name devices is an array"
    $ids = @{}
    foreach ($device in $body.devices) {
        $parsedId = [Guid]::Empty
        $validId = [Guid]::TryParse([string]$device.device_id, [ref]$parsedId) -and $parsedId -ne [Guid]::Empty
        Assert-Test ($validId -and -not $ids.ContainsKey([string]$device.device_id)) "$Name unique device UUID"
        $ids[[string]$device.device_id] = $true
        Assert-Test ($device.user_id -eq $Target -and $device.status -ceq 'active') "$Name device ownership and active status"
        Assert-Test ($device.is_own_device -is [bool] -and $device.is_own_device -eq $Own) "$Name item ownership flag"
        Assert-Test ($device.identity_public_key_hex -cmatch '^[0-9a-f]{64}$' -and -not [string]::IsNullOrWhiteSpace($device.protocol_suite)) "$Name key metadata"
    }
}

try {
    Add-Type -AssemblyName System.Net.Http
    $BaseUrl = $BaseUrl.TrimEnd('/')
    $client = [System.Net.Http.HttpClient]::new()
    $client.Timeout = [TimeSpan]::FromSeconds(15)
    if ([string]::IsNullOrWhiteSpace($AccessToken)) {
        $secret = Read-Host "Password for $Email" -AsSecureString
        $credential = [System.Net.NetworkCredential]::new('', $secret)
        $loginBody = @{ email = $Email; password = $credential.Password }
        $login = Invoke-Api '/login_by_email' $loginBody $false
        $loginBody.Clear()
        $credential = $null
        $secret.Dispose()
        if ($login.Status -ne 200 -or [string]::IsNullOrWhiteSpace($login.Body.login_ticket)) {
            throw "Password login failed: HTTP $($login.Status), code=$($login.Body.code)"
        }
        if ([string]::IsNullOrWhiteSpace($SessionDeviceId)) {
            $SessionDeviceId = Read-Host 'Existing active device UUID for this account'
        }
        $verified = Invoke-Api '/device_verification' @{
            login_ticket = $login.Body.login_ticket
            device_id = $SessionDeviceId
        } $false
        $login = $null
        if ($verified.Status -ne 200 -or [string]::IsNullOrWhiteSpace($verified.Body.access_token)) {
            throw "Device login failed: HTTP $($verified.Status), code=$($verified.Body.error.code)"
        }
        $AccessToken = $verified.Body.access_token
        $UserId = $verified.Body.id
        $verified = $null
    }
    $parsedUserId = [Guid]::Empty
    if (-not [Guid]::TryParse($UserId, [ref]$parsedUserId) -or $parsedUserId -eq [Guid]::Empty) {
        throw 'A valid UserId is required (login response id, or -UserId with -AccessToken).'
    }
    $UserId = $parsedUserId.ToString()
    $own = Invoke-Api '/user_devices' @{}
    Test-DeviceList $own $UserId $true 'Own/default'
    $explicit = Invoke-Api '/user_devices' @{ target_user_id = $UserId }
    Test-DeviceList $explicit $UserId $true 'Own/explicit'
    $alias = Invoke-Api '/friend_devices' @{ friend_id = $UserId }
    Test-DeviceList $alias $UserId $true 'Legacy route'
    $both = Invoke-Api '/user_devices' @{ target_user_id = $UserId; friend_id = $UserId }
    Test-DeviceList $both $UserId $true 'Matching aliases'

    $other = [Guid]::NewGuid().ToString()
    Test-Error '/user_devices' @{ target_user_id = $UserId; friend_id = $other } 400 'conflicting_target_user_id' 'Conflicting aliases'
    foreach ($invalid in @('invalid-uuid', '00000000-0000-0000-0000-000000000000', 123)) {
        Test-Error '/user_devices' @{ target_user_id = $invalid } 400 'invalid_target_user_id' 'Invalid target'
    }
    Test-Error '/user_devices' @{ target_user_id = $other } 403 'not_friends' 'Unknown target denied'
    Test-Error '/friend_devices' @{} 400 'invalid_friend_id' 'Legacy route requires target'
    $anonymous = Invoke-Api '/user_devices' @{} $false
    Assert-Test ($anonymous.Status -eq 401) 'Missing token rejected'

    if ($FriendUserId) {
        if ([Guid]$FriendUserId -eq [Guid]$UserId) { throw 'FriendUserId must differ from UserId.' }
        $friend = Invoke-Api '/user_devices' @{ target_user_id = $FriendUserId }
        Test-DeviceList $friend $FriendUserId $false 'Friend'
    } else { Skip-Test 'Friend list: provide -FriendUserId for an existing friend.' }

    # Read-only key check: an active device need not have a current/unexpired bundle.
    $devices = @($own.Body.devices)
    if ($own.Status -eq 200 -and $devices.Count -gt 0) {
        $bundle = Invoke-Api '/get_device_key_bundle' @{ device_id = $devices[0].device_id }
        if ($bundle.Status -eq 404 -and $bundle.Body.error.code -eq 'key_bundle_unavailable') {
            Skip-Test 'Bundle unavailable: prepare an active device with a current unexpired bundle.'
        } else {
            Assert-Test ($bundle.Status -eq 200 -and $bundle.Body.target_user_id -eq $UserId -and
                $bundle.Body.is_own_device -eq $true -and $bundle.Body.device_id -eq $devices[0].device_id) 'Own bundle defaults to authenticated user'
        }
    } else { Skip-Test 'Own bundle: no active device available.' }

    if ($SessionDeviceId) {
        if ($own.Status -ne 200 -or $SessionDeviceId -notin @($devices | ForEach-Object { $_.device_id })) {
            throw 'SessionDeviceId must be one of your listed active devices.'
        }
        $ws = [System.Net.WebSockets.ClientWebSocket]::new()
        $ws.Options.SetRequestHeader('Authorization', "Bearer $AccessToken")
        $ws.Options.SetRequestHeader('deviceID', $SessionDeviceId)
        $address = [UriBuilder]::new($BaseUrl)
        $address.Scheme = if ($address.Scheme -eq 'https') { 'wss' } else { 'ws' }
        $cts = [System.Threading.CancellationTokenSource]::new()
        $cts.CancelAfter(45000)
        $ws.ConnectAsync($address.Uri, $cts.Token).GetAwaiter().GetResult()
        foreach ($type in @('chat.newTextMessage', 'chat.sync.envelope', 'chat.envelopes.ack')) {
            $payload = @{ type = $type; authorization = "Bearer $AccessToken" }
            $field = if ($type -eq 'chat.newTextMessage') { 'sender_device_id' } else { 'device_id' }
            $payload[$field] = $other
            $expectedType = if ($type -eq 'chat.newTextMessage') { 'newTextMessage.result' } else { "$type.result" }
            $bytes = [System.Text.Encoding]::UTF8.GetBytes(($payload | ConvertTo-Json -Compress))
            $ws.SendAsync([ArraySegment[byte]]::new($bytes), [System.Net.WebSockets.WebSocketMessageType]::Text, $true, $cts.Token).GetAwaiter().GetResult()
            do {
                $memory = [System.IO.MemoryStream]::new()
                try {
                    do {
                        $buffer = [byte[]]::new(4096)
                        $received = $ws.ReceiveAsync([ArraySegment[byte]]::new($buffer), $cts.Token).GetAwaiter().GetResult()
                        if ($received.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close) { throw 'WebSocket closed before expected result.' }
                        $memory.Write($buffer, 0, $received.Count)
                        if ($memory.Length -gt 1048576) { throw 'Unexpected large WebSocket message.' }
                    } while (-not $received.EndOfMessage)
                    $reply = [System.Text.Encoding]::UTF8.GetString($memory.ToArray()) | ConvertFrom-Json
                } finally { $memory.Dispose() }
                # Existing online pushes may arrive before the operation result.
            } while ($reply.type -eq 'Online_Sync' -or $reply.type -eq 'online_sync')
            Assert-Test ($reply.type -eq $expectedType -and $reply.success -eq $false -and
                $reply.error.code -eq 'session_device_mismatch') "$type rejects another device identity"
        }
    } else { Skip-Test 'WebSocket binding: provide -SessionDeviceId to enable.' }
    $exitCode = if ($script:failed -eq 0) { 0 } else { 1 }
} catch {
    $script:failed++
    Write-Host "[ERROR] $($_.Exception.Message)" -ForegroundColor Red
} finally {
    if ($null -ne $cts) { $cts.Cancel() }
    if ($null -ne $ws) { $ws.Abort(); $ws.Dispose() }
    if ($null -ne $cts) { $cts.Dispose() }
    if ($null -ne $client) { $client.Dispose() }
    $AccessToken = $null
    $login = $null
    if ($null -ne $loginBody) { $loginBody.Clear() }
    $credential = $null
    Write-Host "Summary: PASS=$script:passed FAIL=$script:failed SKIP=$script:skipped"
    Write-Host 'Skipped cases are NOT verified. No encryption/delivery success test was performed.'
}
exit $exitCode
