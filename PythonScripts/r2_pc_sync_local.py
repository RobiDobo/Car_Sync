import os
import boto3

#Goal: Local folder becomes identical to R2.
#Download new files from R2. Delete local files not in R2.
#Do not touch R2 bucket.

LOCAL_FOLDER = os.getenv("LOCAL_FOLDER")
R2_BUCKET_NAME = os.getenv("R2_BUCKET_NAME")
R2_ACCOUNT_ID = os.getenv("R2_ACCOUNT_ID")
R2_ACCESS_KEY_ID = os.getenv("R2_ACCESS_KEY_ID")
R2_SECRET_ACCESS_KEY = os.getenv("R2_SECRET_ACCESS_KEY")
R2_ENDPOINT = f"https://{R2_ACCOUNT_ID}.r2.cloudflarestorage.com"


s3 = boto3.client(
    "s3",
    endpoint_url=R2_ENDPOINT,
    aws_access_key_id=R2_ACCESS_KEY_ID,
    aws_secret_access_key=R2_SECRET_ACCESS_KEY
)

# LIST R2 FILES
r2_objects = s3.list_objects_v2(Bucket=R2_BUCKET_NAME)
r2_files = [obj['Key'] for obj in r2_objects.get('Contents', [])]

# LIST LOCAL FILES
local_files = []
for root, _, files in os.walk(LOCAL_FOLDER):
    for f in files:
        rel_path = os.path.relpath(os.path.join(root, f), LOCAL_FOLDER).replace("\\", "/")
        local_files.append(rel_path)

# DOWNLOAD missing files
for file in r2_files:
    if file not in local_files:
        local_path = os.path.join(LOCAL_FOLDER, file.replace("/", os.sep))
        os.makedirs(os.path.dirname(local_path), exist_ok=True)
        s3.download_file(R2_BUCKET_NAME, file, local_path)
        print(f"Downloaded {file}")

# DELETE local files not in R2
for file in local_files:
    if file not in r2_files:
        os.remove(os.path.join(LOCAL_FOLDER, file.replace("/", os.sep)))
        print(f"Deleted local file {file}")

print("✅ Local folder synced with R2 bucket")
