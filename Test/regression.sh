#!/bin/bash
LC=$1
if [ -z "$LC" ]; then
    LC=./lc
fi

rm -f log
for f in chap2a.led chap2b.led chap2c.led chap2d.led chap2e.led chap2f.led chap3.led chap4a.led chap4b.led chap4c.led chap5.led chap6a.led chap6b.led chap6d.led chap6e.led chap6f.led chap7a.led chap7b.led chap7c.led chap7d.led chap8a.led; do $LC $f >> log; done
$LC chap8c.led < concordanceInput >> log
for f in chap9.led chap11.led chap14.led chap15a.led chap15b.led chap15c.led chap15d.led chap16.led; do $LC $f >> log; done
$LC chap17.led < chap17input >> log
for f in chap19a.led chap19b.led chap19c.led chap20a.led chap20b.led chap20c.led; do $LC $f >> log; done
$LC -m 500000 chap20d.led >> log
$LC chap21.led >> log

diff ref log
