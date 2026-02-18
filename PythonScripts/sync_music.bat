@echo off
:: Change directory to where the script actually lives
:: now sees the secrets.env file and playlists.json without needing to hardcode paths
cd /d "%~dp0"
:: always pull latest playlists.json because it's updated with the mobile app
::run git config credential.helper store and manual git pull once when using the script for the first time to store credentials, then this will work without prompting for username/password
echo Syncing playlists from GitHub...
:: This pulls the latest playlists.json and code changes
git pull origin main
echo [Checking for Updates...]
:: Optional: Keep yt-dlp updated automatically
python -m pip install -U yt-dlp

echo [Starting Music Sync...]
:: Run the script
python sync_script.py

echo [Sync Finished]
:: 'pause' keeps the window open so you can see errors. 
:: Remove 'pause' once you're sure it works perfectly.
pause