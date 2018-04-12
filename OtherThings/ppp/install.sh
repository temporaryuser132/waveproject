#! /bin/sh

cp bin/* /system/bin/
mkdir -p /system/config/ppp/
cp ppp/* /system/config/ppp/
cp kernel/* /system/drivers/net/if/
echo Installation finished - please reboot!
