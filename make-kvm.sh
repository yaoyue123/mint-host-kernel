#/bin/sh
cores=$(nproc --all)
EXTRAVERSION=""
make clean M=arch/x86/kvm/ &&
make -j $cores scripts &&
make -j $cores prepare &&
make -j $cores modules_prepare &&
cp /usr/src/linux-headers-`uname -r`/Module.symvers arch/x86/kvm/Module.symvers  &&
cp /usr/src/linux-headers-`uname -r`/Module.symvers Module.symvers  &&
cp "/boot/System.map-$(uname -r)" . 
cp "/boot/System.map-$(uname -r)" arch/x86/kvm/
touch .scmversion &&
make -j $cores modules M=arch/x86/kvm/ LOCALVERSION= || exit 1
echo "You might be asked for sudo rights, to install the kernel module"
sudo make modules_install M=arch/x86/kvm/ LOCALVERSION= || exit 1

echo "Unload old modules" 
sudo modprobe -r kvm_amd || exit 1
sudo modprobe -r kvm  || exit 1
sudo cp ./arch/x86/kvm/kvm.ko "/lib/modules/$(uname -r)/kernel/arch/x86/kvm/"
sudo cp ./arch/x86/kvm/kvm-amd.ko "/lib/modules/$(uname -r)/kernel/arch/x86/kvm/"
echo "Load new modules"
sudo modprobe kvm  || exit 1
sudo modprobe kvm-amd || exit
