# Load .env file and start auction server
$envPath = Join-Path $PSScriptRoot ".env"
if (Test-Path $envPath) {
    Get-Content $envPath | ForEach-Object {
        if ($_ -match "^([^#=]+)=(.*)$") {
            $key = $matches[1].Trim()
            $value = $matches[2].Trim()
            [Environment]::SetEnvironmentVariable($key, $value, "Process")
        }
    }
    Write-Host "Loaded environment from .env"
} else {
    Write-Warning ".env file not found"
}

Write-Host "Starting auction server with AUCTION_EMAIL_API_TOKEN..."
& "$PSScriptRoot\build\debug\auction_server.exe" $args
