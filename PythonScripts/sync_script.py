import os
import yt_dlp
import boto3
import json

#PLAYLIST_URL = os.getenv("PLAYLIST_URL")
OUTPUT_DIR = os.getenv("OUTPUT_DIR", "./downloads")
R2_BUCKET_NAME = os.getenv("R2_BUCKET_NAME")
R2_ACCOUNT_ID = os.getenv("R2_ACCOUNT_ID")
R2_ACCESS_KEY_ID = os.getenv("R2_ACCESS_KEY_ID")
R2_SECRET_ACCESS_KEY = os.getenv("R2_SECRET_ACCESS_KEY")
R2_ENDPOINT = f"https://{R2_ACCOUNT_ID}.r2.cloudflarestorage.com"

os.makedirs(OUTPUT_DIR, exist_ok=True)

def get_playlists():
    with open("playlists.json", "r", encoding="utf-8") as f:
        data = json.load(f)
    return [p["url"] for p in data.get("playlists", [])]

def download_playlist(playlist_url, output_dir):
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
        "restrictfilenames": True
    }
    print("Downloading playlist...")
    with yt_dlp.YoutubeDL(ydl_opts) as ydl:
        ydl.download([PLAYLIST_URL])
    print("Download complete.")    

def upload_to_cloudflare(bucket_name, output_dir):
    print("Syncing to Cloudflare R2...")
    s3 = boto3.client(
        's3',
        endpoint_url=R2_ENDPOINT,
        aws_access_key_id=R2_ACCESS_KEY_ID,
        aws_secret_access_key=R2_SECRET_ACCESS_KEY
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
    r2_objects = s3.list_objects_v2(Bucket=R2_BUCKET_NAME)
    r2_files = [obj["Key"] for obj in r2_objects.get("Contents", [])]

    # Upload new files
    for file in local_files:
        if file not in r2_files:
            s3.upload_file(os.path.join(OUTPUT_DIR, file), R2_BUCKET_NAME, file)
            print(f"Uploaded: {file}")

    # Delete outdated files
    for file in r2_files:
        if file not in local_files:
            s3.delete_object(Bucket=R2_BUCKET_NAME, Key=file)
            print(f"Deleted from R2: {file}")

    print("Sync complete.")

if __name__ == "__main__":
    playlists = get_playlists()
    for url in playlists:
        print(f"Downloading {url}")
        download_playlist(url)
    upload_to_cloudflare()
