#!/bin/bash

scriptdir=`dirname $BASH_SOURCE`
build_tools_fullpath=`readlink -m $scriptdir`
project_dir=`dirname $build_tools_fullpath`

cd $project_dir

if [ -d build ]; then rm -rf build; fi

mkdir -p build
cd build
cmake ..
make $1
