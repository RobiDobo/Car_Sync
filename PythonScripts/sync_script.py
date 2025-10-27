import os
import yt_dlp
import boto3
import json
import logging
import shutil # Added for clean directory removal

COOKIES_FILE = "cookies.txt"
DOWNLOAD_ARCHIVE_FILE = "download_archive.txt" 
#PLAYLIST_URL = os.getenv("PLAYLIST_URL")
R2_ACCESS_KEY = os.environ.get('ACCESS_KEY')
R2_SECRET_KEY = os.environ.get('SECRET_KEY')
R2_BUCKET = os.environ.get('BUCKET')
R2_ACCOUNT_ID = os.environ.get('ACCOUNT_ID')
R2_ENDPOINT = f"https://{R2_ACCOUNT_ID}.r2.cloudflarestorage.com"

PLAYLISTS_JSON_FILE = "playlists.json"
OUTPUT_DIR = "downloaded_audio" # Define the OUTPUT_DIR constant

# Setup Logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

# Ensure output directory is clean
if os.path.exists(OUTPUT_DIR):
    shutil.rmtree(OUTPUT_DIR)#remove existing directory and its contents
os.makedirs(OUTPUT_DIR, exist_ok=True)

def get_playlists():
    with open(PLAYLISTS_JSON_FILE, "r", encoding="utf-8") as f:
        data = json.load(f)
    return data

def download_playlist(playlist_id):
    playlist_url = f"https://www.youtube.com/playlist?list={playlist_id}"
    ydl_opts = {
        "format": "bestaudio[ext=m4a]/bestaudio",
        "outtmpl": os.path.join(OUTPUT_DIR, "%(playlist)s", "%(playlist_index)s-%(title)s.%(ext)s"),
        "ignoreerrors": True,
        "quiet": False,
        "postprocessors": [{
            "key": "FFmpegExtractAudio",
            "preferredcodec": "mp3",
            "preferredquality": "320",
        }],
        # On GitHub Actions, ffmpeg is already installed and in PATH
        "restrictfilenames": True, #filename safety
        "cookiefile": COOKIES_FILE,
        "download_archive": DOWNLOAD_ARCHIVE_FILE, # Use the archive file
    }
    logging.info(f"Downloading playlist: {playlist_url}")
    try:
        with yt_dlp.YoutubeDL(ydl_opts) as ydl:
            ydl.download([playlist_url])
        logging.info("Download complete.")
    except Exception as e:
        logging.error(f"Error during yt-dlp download for {playlist_id}: {e}")

def upload_to_cloudflare(bucket_name, output_dir):
    logging.info("Syncing to Cloudflare R2...")
    s3 = boto3.client(
        's3',
        endpoint_url=R2_ENDPOINT,
        aws_access_key_id=R2_ACCESS_KEY,
        aws_secret_access_key=R2_SECRET_KEY
    )
    # Gather local files
    local_files = []
    for root, _, files in os.walk(OUTPUT_DIR):
        for f in files:
            if f.lower() == "desktop.ini" or f.startswith("."):
                continue
            rel_path = os.path.relpath(os.path.join(root, f), OUTPUT_DIR).replace("\\", "/")
            local_files.append(rel_path)

    # List files in R2
    r2_objects = s3.list_objects_v2(Bucket=R2_BUCKET)
    r2_files = [obj["Key"] for obj in r2_objects.get("Contents", [])]

    # Upload new files
    for file in local_files:
        if file not in r2_files:
            s3.upload_file(os.path.join(OUTPUT_DIR, file), R2_BUCKET, file)
            logging.info(f"Uploaded: {file}")

    # Delete outdated files
    for file in r2_files:
        if file not in local_files:
            s3.delete_object(Bucket=R2_BUCKET, Key=file)
            logging.info(f"Deleted from R2: {file}")

    logging.info("Sync complete.")

if __name__ == "__main__":
    playlists = get_playlists()
    if playlists:
        for item in playlists:
            playlist_id = item.get('id') # Assuming your JSON uses 'id'
            logging.info(f"Downloading {playlist_id}")
            download_playlist(playlist_id)
        upload_to_cloudflare()
    else:
        logging.warning("No playlists found in the JSON file.")