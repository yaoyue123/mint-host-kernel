#/bin/sh
cores=$(nproc --all)
EXTRAVERSION=""
MODPATH="drivers/crypto/ccp"
make clean M="$MODPATH" &&
make -j $cores scripts &&
make -j $cores prepare &&
make -j $cores modules_prepare &&
cp /usr/src/linux-headers-`uname -r`/Module.symvers "$MODPATH"/Module.symvers  &&
cp /usr/src/linux-headers-`uname -r`/Module.symvers Module.symvers  &&
chown valentin:valentin "$MODPATH"/Module.symvers
cp "/boot/System.map-$(uname -r)" . 
cp "/boot/System.map-$(uname -r)" "$MODPATH"
touch .scmversion &&
make -j $cores modules M="$MODPATH" LOCALVERSION= &&
make modules_install M="$MODPATH" LOCALVERSION= 

echo "Installing module file"
cp ./drivers/crypto/ccp/ccp.ko "/lib/modules/$(uname -r)/kernel/drivers/crypto/ccp/ccp.ko"
cp ./drivers/crypto/ccp/ccp-crypto.ko "/lib/modules/$(uname -r)/kernel/drivers/crypto/ccp/ccp-crypto.ko"
