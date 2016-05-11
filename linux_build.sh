#!/bin/sh

gcc -I. -o websocket_server main.c libserial.c linux_x64/libwebsockets.a
