#!/usr/bin/env bash
#
# This script uploads an archive to S3. It uses s3cmd, you can install it by
# using apt-get install `s3cmd`.
#
# You should define two environment variable for the S3 upload to work:
#   - S3_ACCESS_KEY
#   - S3_SECRET_KEY
#
# You can configure the bucket by using the variables:
#   - S3_BUCKET (default 'buildpacks-repository')
#   - S3_REGION (default 'eu-central-1')
#
# If you need some credentials, go here:
# https://console.aws.amazon.com/iam/home?region=eu-central-1#/users
#
# For the authorization, you need to click "Attach existing policies directly"
# and chose `put-buildpacks-repository`.

set -e
if [ -n "$BUILDPACK_DEBUG" ] || [ -n "$DEBUG" ]; then
  set -x
fi

print_usage() {
  echo "You have to set S3_ACCESS_KEY and S3_SECRET_KEY variables, then:"
  echo "$0 <path_in_bucket> <file to upload>"
  echo
  echo "Example: S3_ACCESS_KEY=<s3_access_key> S3_SECRET_KEY=<s3_secret_key> $0 graphicsmagick/scalingo-20/graphicsmagick-1.36.0.tar.gz ./graphicsmagick-1.36.0.tar.gz"
}

s3_path="$1"
archive_name="$2"
S3_BUCKET="${S3_BUCKET:-buildpacks-repository}"
S3_REGION="${S3_REGION:-eu-central-1}"

if [ -z "$archive_name" -o -z "$s3_path" ]; then
  print_usage
  exit 1
fi

if [[ $# -gt 2 ]]; then
  print_usage
  exit 1
fi

if [ "$S3_ACCESS_KEY" == "" ]; then
  print_usage
  echo "must set S3_ACCESS_KEY"
  exit 1
fi

if [ "$S3_SECRET_KEY" == "" ]; then
  print_usage
  echo "must set S3_SECRET_KEY"
  exit 1
fi

cur_dir=$(cd $(dirname $0) && pwd)
cd $cur_dir

which s3cmd >/dev/null || echo "s3cmd is not available in your PATH" >&2 || echo "Archive not uploaded to S3" >&2 || exit -1
s3cmd_cmd="s3cmd"

echo "---> Uploading $archive_name to S3 (${S3_BUCKET}/${s3_path})"
echo ""

$s3cmd_cmd --region $S3_REGION --access_key $S3_ACCESS_KEY --secret_key $S3_SECRET_KEY \
  --quiet --acl-public put $archive_name "s3://${S3_BUCKET}/${s3_path}"
if [[ $? -ne 0 ]]; then
  echo "Error uploading the archive to S3" >&2
  exit -1
fi

echo "---> Upload done"
echo ""
