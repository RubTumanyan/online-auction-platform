@echo off
REM Load .env file and start auction server

if exist .env (
    for /f "usebackq delims=" %%a in (".env") do (
        set "%%a"
    )
) else (
    echo Warning: .env file not found
)

echo Starting auction server with AUCTION_EMAIL_API_TOKEN...
build\debug\auction_server.exe %*
