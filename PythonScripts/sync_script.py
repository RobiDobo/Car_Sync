import os
import yt_dlp
import boto3
import json
import logging
import shutil # Added for clean directory removal
from dotenv import load_dotenv
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ENV_PATH = os.path.join(SCRIPT_DIR, "secrets.env")
load_dotenv(ENV_PATH) #load the secrets
#COOKIES_FILE = "cookies.txt"
DOWNLOAD_ARCHIVE_FILE = "download_archive.txt" 
#PLAYLIST_URL = os.getenv("PLAYLIST_URL")
LOCAL_FOLDER =os.environ.get('LOCAL_FOLDER')
R2_ACCESS_KEY = os.environ.get('R2_ACCESS_KEY_ID')
R2_SECRET_KEY = os.environ.get('R2_SECRET_ACCESS_KEY')
R2_BUCKET = os.environ.get('R2_BUCKET_NAME')
R2_ACCOUNT_ID = os.environ.get('R2_ACCOUNT_ID')
R2_ENDPOINT = f"https://{R2_ACCOUNT_ID}.r2.cloudflarestorage.com"

PLAYLISTS_JSON_FILE = "playlists.json"
#OUTPUT_DIR = "downloaded_audio"

# Setup Logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

if not os.path.exists(LOCAL_FOLDER):
    logging.info(f"Creating local folder: {LOCAL_FOLDER}")
    os.makedirs(LOCAL_FOLDER, exist_ok=True)
else:
    # We found the folder! We DON'T delete it. 
    # yt-dlp will later see existing files and skip them automatically.
    logging.info(f"Local folder found, ready to sync: {LOCAL_FOLDER}")

def get_playlists():
    with open(PLAYLISTS_JSON_FILE, "r", encoding="utf-8") as f:
        data = json.load(f)
    return data

def download_playlist(playlist_id):
    playlist_url = f"https://www.youtube.com/playlist?list={playlist_id}"
    ydl_opts = {
        "format": "bestaudio/best",
        "outtmpl": os.path.join(LOCAL_FOLDER, "%(playlist)s", "%(playlist_index)s-%(title)s.%(ext)s"),
        "ignoreerrors": True,
        "quiet": False,
        "postprocessors": [{
            "key": "FFmpegExtractAudio",
            "preferredcodec": "mp3",
            "preferredquality": "320",
        }],
        # On GitHub Actions, ffmpeg is already installed and in PATH
        "restrictfilenames": True, #filename safety
        'ffmpeg_location': r'D:\Downloads\stuff\ffmpeg-8.0-full_build\bin\ffmpeg.exe',
        #"sleep_requests": 3, # Sleep X seconds before each HTTP request
        #"sleep_interval": 5, # Sleep Y seconds between downloading videos
        "download_archive": DOWNLOAD_ARCHIVE_FILE # keep track of downloads to avoid reDownloading
        #"cookiefile":
    }
    logging.info(f"Downloading playlist: {playlist_url}")
    try:
        with yt_dlp.YoutubeDL(ydl_opts) as ydl:
            ydl.download([playlist_url])
        logging.info("Download complete.")
    except Exception as e:
        logging.error(f"Error during yt-dlp download for {playlist_id}: {e}")

def upload_to_cloudflare():#weird to call the variables aws but that's from real examples
    logging.info("Syncing to Cloudflare R2...")
    r2 = boto3.client(
        's3',
        endpoint_url=R2_ENDPOINT,
        aws_access_key_id=R2_ACCESS_KEY,
        aws_secret_access_key=R2_SECRET_KEY
    )
    # Gather local files
    local_files = []
    for root, _, files in os.walk(LOCAL_FOLDER):
        for f in files:
            if f.lower() == "desktop.ini" or f.startswith("."):
                continue
            rel_path = os.path.relpath(os.path.join(root, f), LOCAL_FOLDER).replace("\\", "/")
            local_files.append(rel_path)

    # List files in R2
    r2_objects = r2.list_objects_v2(Bucket=R2_BUCKET)
    r2_files = [obj["Key"] for obj in r2_objects.get("Contents", [])]

    # Upload new files
    for file in local_files:
        if file not in r2_files:
            r2.upload_file(os.path.join(LOCAL_FOLDER, file), R2_BUCKET, file)
            logging.info(f"Uploaded: {file}")

    # Delete outdated files
    for file in r2_files:
        if file not in local_files:
            r2.delete_object(Bucket=R2_BUCKET, Key=file)
            logging.info(f"Deleted from R2: {file}")

    logging.info("Sync complete.")

if __name__ == "__main__":
    playlists = get_playlists()
    if playlists:
        for item in playlists:
            playlist_id = item.get('id') 
            logging.info(f"Downloading {playlist_id}")
            download_playlist(playlist_id)
            logging.info(f"Downloaded {playlist_id}")
        upload_to_cloudflare()
    else:
        logging.warning("No playlists found in the JSON file.")