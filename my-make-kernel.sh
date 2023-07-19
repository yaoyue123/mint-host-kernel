#!/bin/bash

run_cmd()
{
   echo "$*"

   eval "$*" || {
      echo "ERROR: $*"
      exit 1
   }
}


[ -d linux-patches ] && {

	for P in linux-patches/*.patch; do
		run_cmd patch -p1 -d linux < $P
	done
}

MAKE="make -j $(getconf _NPROCESSORS_ONLN) LOCALVERSION="
COMMIT=$(git log --format="%h" -1 HEAD)

run_cmd $MAKE distclean

run_cmd cp /boot/config-$(uname -r) .config
run_cmd ./scripts/config --set-str LOCALVERSION "-sev-step-$COMMIT"
run_cmd ./scripts/config --disable LOCALVERSION_AUTO
run_cmd ./scripts/config --disable  DEBUG_INFO
run_cmd ./scripts/config --enable  DEBUG_INFO_REDUCED
run_cmd ./scripts/config --enable  AMD_MEM_ENCRYPT
run_cmd ./scripts/config --disable AMD_MEM_ENCRYPT_ACTIVE_BY_DEFAULT
run_cmd ./scripts/config --enable  KVM_AMD_SEV
run_cmd ./scripts/config --module  CRYPTO_DEV_CCP_DD
run_cmd ./scripts/config --disable SYSTEM_TRUSTED_KEYS
run_cmd ./scripts/config --disable SYSTEM_REVOCATION_KEYS

run_cmd $MAKE olddefconfig

# Build
run_cmd $MAKE >/dev/null

run_cmd $MAKE bindeb-pkg

