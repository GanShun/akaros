#!/bin/bash
set -e
(cd $AKAROS_ROOT && make xcc-upgrade-from-scratch &> /dev/null)
