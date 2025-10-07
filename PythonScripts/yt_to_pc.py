import yt_dlp
import os

output_dir = "C:\\Users\\robi\\Music"
os.makedirs(output_dir, exist_ok=True)
playlist_url = "https://www.youtube.com/playlist?list=PLMtGZoTcqIJTFZJ_HcxjxJFGL9OxalUBE"
# yt-dlp options
ydl_opts = {
    'format': 'bestaudio[ext=m4a]/bestaudio',
    'outtmpl': os.path.join(output_dir,'%(playlist)s', '%(playlist_index)s-%(title)s.%(ext)s'),
    'ignoreerrors': True,
    'quiet': False,
    'postprocessors': [{
        'key': 'FFmpegExtractAudio',
        'preferredcodec': 'mp3',
        'preferredquality': '320',
    }],
    #ffmpeg.exe location ca sa rulezi din visual studio, altfel daca e in path merge din cmd
    'ffmpeg_location': r'D:\Downloads\stuff\ffmpeg-8.0-full_build\bin\ffmpeg.exe',
    'restrictfilenames': True
}
with yt_dlp.YoutubeDL(ydl_opts) as ydl:
    ydl.download([playlist_url])

print("done")
