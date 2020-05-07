#!/bin/bash

# builds an executable version of dicom.cpp

BUILDDIR=./build
mkdir -p $BUILDDIR

cmake -Bbuild -DCMAKE_BUILD_TYPE:STRING=Debug -DITK_DIR:PATH=/home/forrestli/ITK/ITK-Release -GNinja
ninja -Cbuild
