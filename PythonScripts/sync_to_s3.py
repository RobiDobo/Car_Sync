import os
import yt_dlp
import boto3
import json
import logging
import shutil
from dotenv import load_dotenv
import subprocess

load_dotenv("secrets.env")
# --- Configuration ---
BROWSER_TO_USE = "edge",
DOWNLOAD_ARCHIVE_FILE = "download_archive.txt" 
PLAYLISTS_JSON_FILE  = "playlists.json"
OUTPUT_DIR = "downloaded_audio"

LOCAL_FOLDER = os.getenv("LOCAL_FOLDER")
# AWS S3 Configurationm 
AWS_ACCESS_KEY = os.getenv('AWS_ACCESS_KEY_ID')
AWS_SECRET_KEY = os.getenv('AWS_SECRET_ACCESS_KEY')
AWS_BUCKET = os.getenv('AWS_BUCKET_NAME')
AWS_REGION = os.getenv('AWS_REGION', 'eu-north-1') 

# Setup Logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
print(LOCAL_FOLDER)
print(AWS_ACCESS_KEY)
print(AWS_SECRET_KEY)
print(AWS_BUCKET)
print(AWS_REGION)

# Ensure output directory exists
os.makedirs(LOCAL_FOLDER, exist_ok=True)

def get_playlists():
    # Check if the file exists
    if not os.path.exists(PLAYLISTS_JSON_FILE):
        logging.error(f"{PLAYLISTS_JSON_FILE} not found! Please create it.")
        return []
    
    with open(PLAYLISTS_JSON_FILE, "r", encoding="utf-8") as f:
        data = json.load(f)
    return data


def download_playlist(playlist_id, playlist_title):
    playlist_url = f"https://www.youtube.com/playlist?list={playlist_id}"
    ydl_opts = {
        "format": "bestaudio[ext=m4a]/bestaudio",
        "outtmpl": os.path.join(LOCAL_FOLDER, "%(playlist)s", "%(playlist_index)s-%(title)s.%(ext)s"),
        "ignoreerrors": True,
        "quiet": False,
        "postprocessors": [{
            "key": "FFmpegExtractAudio",
            "preferredcodec": "mp3",
            "preferredquality": "320",
        }],
        'ffmpeg_location': r'D:\Downloads\stuff\ffmpeg-8.0-full_build\bin\ffmpeg.exe',
        'restrictfilenames': True,
        #"download_archive": DOWNLOAD_ARCHIVE_FILE
        "cookiesfrombrowser": ("chrome", )
    }
    logging.info(f"Downloading playlist: {playlist_title}")
    try:
        with yt_dlp.YoutubeDL(ydl_opts) as ydl:
            ydl.download([playlist_url])
        logging.info("Download complete.")
    except Exception as e:
        logging.error(f"Error during yt-dlp download for {playlist_title}: {e}")

def sync_to_s3():
    logging.info("Syncing to AWS S3...")
    
    s3 = boto3.client(
        's3',
        region_name=AWS_REGION,
        aws_access_key_id=AWS_ACCESS_KEY,
        aws_secret_access_key=AWS_SECRET_KEY
    )

    # 1. Gather local files
    local_files = []
    for root, _, files in os.walk(LOCAL_FOLDER):
        for f in files:
            # Skip system files and the archive file
            if f.lower() in ["desktop.ini", "index.html", DOWNLOAD_ARCHIVE_FILE] or f.startswith("."):
                continue
            
            # Create relative path (e.g., "MyPlaylist/01-Song.mp3")
            rel_path = os.path.relpath(os.path.join(root, f), LOCAL_FOLDER).replace("\\", "/")
            local_files.append(rel_path)

    # 2. Gather S3 files (With Pagination)
    s3_files = []
    paginator = s3.get_paginator('list_objects_v2')
    try:
        for page in paginator.paginate(Bucket=AWS_BUCKET):
            if 'Contents' in page:
                for obj in page['Contents']:
                    s3_files.append(obj['Key'])
    except Exception as e:
        logging.error(f"Error listing S3 objects: {e}")
        return

    # 3. Upload new music files
    for file in local_files:
        if file not in s3_files:
            local_file_path = os.path.join(LOCAL_FOLDER, file)
            logging.info(f"Uploading: {file}")
            try:
                # Set ContentType to audio/mpeg so browsers play it instead of downloading
                s3.upload_file(
                    local_file_path, 
                    AWS_BUCKET, 
                    file, 
                    ExtraArgs={'ContentType': 'audio/mpeg'}
                )
            except Exception as e:
                logging.error(f"Failed to upload {file}: {e}")

    # 4. Delete outdated files from S3 (excluding index.html)
    for file in s3_files:
        if file not in local_files and file != "index.html":
            logging.info(f"Deleting from S3 (not found locally): {file}")
            try:
                s3.delete_object(Bucket=AWS_BUCKET, Key=file)
            except Exception as e:
                logging.error(f"Failed to delete {file}: {e}")

    # 5. Generate and Upload index.html (JSON List)
    logging.info("Generating index.html (JSON file list)...")
    
    # Create the JSON list of files
    json_content = json.dumps(local_files, indent=2)
    
    try:
        s3.put_object(
            Bucket=AWS_BUCKET,
            Key='index.html',
            Body=json_content,
            ContentType='application/json', # Treat as JSON
            CacheControl='max-age=0, no-cache' 
        )
        logging.info("Successfully updated index.html on S3")
    except Exception as e:
        logging.error(f"Failed to upload index.html: {e}")

    logging.info("Sync complete.")

if __name__ == "__main__":
    # 2. Download from YouTube
    playlists = get_playlists()
    if playlists:
        for item in playlists:
            playlist_title= item.get('title')
            playlist_id = item.get('id') 
            if playlist_id:
                logging.info(f"Processing {playlist_id}")
                download_playlist(playlist_id, playlist_title)
        
        # 3. Sync to S3 and Update Index
        sync_to_s3()
    else:
        logging.warning("No playlists found (or playlists.json is empty).")