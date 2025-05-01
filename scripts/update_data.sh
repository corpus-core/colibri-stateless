#!/bin/sh
ssh cc@31.220.90.180 "cd /home/cc/fetch && node fetch_all.mjs"

rsync -avz --size-only --progress cc@31.220.90.180:/home/cc/fetch/data/1 ../.data

