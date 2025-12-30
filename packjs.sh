#!/bin/sh

node tool/pack.js web_root/bundle.js::gzip web_root/components.js::gzip web_root/history.min.js::gzip web_root/index.html::gzip web_root/main.js::gzip web_root/tailwind.css::gzip certs/* > main/pack_fs.c
