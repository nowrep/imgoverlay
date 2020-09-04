#!/bin/sh

IMGOVERLAY_LIB_NAME="libImgoverlay.so"
if [ "$IMGOVERLAY_NODLSYM" = "1" ]; then
	IMGOVERLAY_LIB_NAME="libImgoverlay_nodlsym.so"
fi

if [ "$#" -eq 0 ]; then
	programname=`basename "$0"`
	echo "ERROR: No program supplied"
	echo
	echo "Usage: $programname <program>"
	exit 1
fi

# Execute the program under a clean environment
# pass through the overriden LD_PRELOAD environment variables
LD_PRELOAD="${LD_PRELOAD}:${IMGOVERLAY_LIB_NAME}"

exec env IMGOVERLAY=1 LD_PRELOAD="${LD_PRELOAD}" "$@"
