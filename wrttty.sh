#!/bin/bash
# simple script to test sending data to a yar tty
tty=${1:-btty}

if [[ ! -c $(realpath $tty) ]]; then
    echo "$tty does not seem to be a tty"
    exit -1
fi

stty raw isig icrnl opost; cat > $tty; stty sane; reset
