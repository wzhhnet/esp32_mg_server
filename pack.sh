#!/bin/sh

GZ_FILES=""
SRC_DIR="./web_root"

if [ -z "$SRC_DIR" ]; then
  echo "Usage: $0 <source_dir>"
  exit 1
fi

if [ ! -d "$SRC_DIR" ]; then
  echo "Error: $SRC_DIR is not a directory"
  exit 1
fi

for f in "$SRC_DIR"/*; do
  if [ -f "$f" ]; then
    gz="$SRC_DIR/$(basename "$f").gz"
    gzip -c "$f" > "$gz"
    GZ_FILES="$GZ_FILES $gz"
  fi
done

./tool/pack $GZ_FILES > ./main/pack_fs.c
rm -rf $GZ_FILES

