#!/bin/bash

CACHE_DIR=`dirname $0`/../.memcached

if [ ! -d $CACHE_DIR ]; then
    mkdir -p $CACHE_DIR
fi

nohup memcached -m 100 -o ext_path=$CACHE_DIR/extstore:10G > $CACHE_DIR/memcached.log 2>&1 &

echo $! > $CACHE_DIR/memcached.pid

echo "Memcached started with PID $!"
