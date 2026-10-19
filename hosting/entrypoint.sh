#!/bin/sh
set -eu
mkdir -p /data
chown node:node /data
chmod 700 /data
exec gosu node node /service/server.mjs
