#!/bin/bash

scriptdir=`dirname $BASH_SOURCE`
build_tools_fullpath=`readlink -m $scriptdir`
project_dir=`dirname $build_tools_fullpath`

docker run -it --rm --name=crypto-market-data-collector-builder --mount type=bind,source=$project_dir,target=/project builder/crypto-market-data-collector-builder:1.0 bash