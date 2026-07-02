# Reproducible two-"machine" network validation for Skittermouse (spec 17).
# Runs entirely in Docker (two isolated Linux containers on one bridge network),
# exercising the SAME product logic the Windows/macOS app ships, over real TCP.
#
#   Test 1 (netpair):  ECDH numeric-comparison pairing -> both derive the same code + PSK.
#   Test 2 (netcheck): secure link (PSK) -> coordinator -> ownership switch -> forward
#                      an encrypted keystroke that the sink decrypts and injects.
#
# Usage:  pwsh tests/docker/run.ps1
$ErrorActionPreference = "Stop"
Push-Location $PSScriptRoot
try {
    Write-Host "== Building netcheck/netpair image =="
    docker compose build | Out-Null

    Write-Host "`n== Test 1: pairing (ECDH numeric comparison over TCP) =="
    $p = docker compose -f docker-compose.pair.yml up --abort-on-container-exit 2>&1
    $p | Select-String "PAIR|RESULT"
    docker compose -f docker-compose.pair.yml down -v 2>&1 | Out-Null
    $codes = [regex]::Matches($p, "CODE=(\d{6})") | ForEach-Object { $_.Groups[1].Value }
    $psks = [regex]::Matches($p, "PSK=([0-9a-f]{64})") | ForEach-Object { $_.Groups[1].Value }
    if (-not ($codes.Count -eq 2 -and $codes[0] -eq $codes[1] -and $psks[0] -eq $psks[1])) {
        throw "Pairing MISMATCH (codes=$($codes -join ','))"
    }
    Write-Host "  PASS: both nodes derived identical code=$($codes[0]) + PSK"

    Write-Host "`n== Test 2: connect + forward encrypted input over TCP =="
    docker compose up --abort-on-container-exit --exit-code-from nodeb 2>&1 | Select-String "EVENT|RESULT"
    $code = $LASTEXITCODE
    docker compose down -v 2>&1 | Out-Null
    if ($code -ne 0) { throw "Connect/forward FAILED (exit $code)" }
    Write-Host "  PASS"

    Write-Host "`n== Test 3: chunked file transfer over TCP (byte-verified) =="
    docker compose -f docker-compose.file.yml up --abort-on-container-exit --exit-code-from fnodeb 2>&1 | Select-String "SENT|RESULT"
    $fcode = $LASTEXITCODE
    docker compose -f docker-compose.file.yml down -v 2>&1 | Out-Null
    if ($fcode -ne 0) { throw "File transfer FAILED (exit $fcode)" }
    Write-Host "  PASS"

    Write-Host "`n== ALL DOCKER NETWORK TESTS PASSED =="
} finally {
    Pop-Location
}
