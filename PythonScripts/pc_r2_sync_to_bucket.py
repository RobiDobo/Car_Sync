import os
import boto3
import requests

#Upload new files to R2. Delete from R2 any files that are no longer in local folder.

# CONFIG
LOCAL_FOLDER = os.getenv("LOCAL_FOLDER")
R2_BUCKET_NAME = os.getenv("R2_BUCKET_NAME")
R2_ACCOUNT_ID = os.getenv("R2_ACCOUNT_ID")
R2_ACCESS_KEY_ID = os.getenv("R2_ACCESS_KEY_ID")
R2_SECRET_ACCESS_KEY = os.getenv("R2_SECRET_ACCESS_KEY")
R2_ENDPOINT = f"https://{R2_ACCOUNT_ID}.r2.cloudflarestorage.com"

# INIT S3 CLIENT
s3 = boto3.client(
    "s3",
    endpoint_url=R2_ENDPOINT,
    aws_access_key_id=R2_ACCESS_KEY_ID,
    aws_secret_access_key=R2_SECRET_ACCESS_KEY
)

# LIST LOCAL FILES
local_files = []
for root, _, files in os.walk(LOCAL_FOLDER):
    for f in files:
        rel_path = os.path.relpath(os.path.join(root, f), LOCAL_FOLDER).replace("\\", "/")
        local_files.append(rel_path)

# LIST R2 FILES
r2_objects = s3.list_objects_v2(Bucket=R2_BUCKET_NAME)
r2_files = [obj['Key'] for obj in r2_objects.get('Contents', [])]

# UPLOAD missing files to R2
for file in local_files:
    if file not in r2_files:
        s3.upload_file(os.path.join(LOCAL_FOLDER, file), R2_BUCKET_NAME, file)
        print(f"Uploaded {file}")

# DELETE from R2 files no longer in local
for file in r2_files:
    if file not in local_files:
        s3.delete_object(Bucket=R2_BUCKET_NAME, Key=file)
        print(f"Deleted {file} from R2")

print("✅ R2 bucket synced with local folder")
