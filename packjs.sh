#!/bin/sh

# output local tailwind css file
../tailwindcss/node_modules/.bin/tailwindcss -i ./web_root/input.css -o ./web_root/tailwind.css -c tailwind.config.js -m

# pack front-end source
node tool/pack.js web_root/bundle.js::gzip web_root/components.js::gzip web_root/history.min.js::gzip web_root/index.html::gzip web_root/main.js::gzip web_root/tailwind.css::gzip certs/* > main/pack_fs.c
