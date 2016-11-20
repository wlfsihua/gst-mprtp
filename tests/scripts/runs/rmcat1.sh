#!/bin/bash
programname=$0

NSSND="ns_snd"
NSRCV="ns_rcv"
LOGSDIR="temp"
SCRIPTSDIR="scripts"
CONFDIR=$SCRIPTSDIR"/configs"
TEMPDIR=$SCRIPTSDIR"/temp"
SYNCTOUCHFILE="triggered_stat"

mkdir $LOGSDIR
rm $LOGSDIR/*
rm $SYNCTOUCHFILE

#setup defaults
DURATION=120
OWD_SND=50
OWD_RCV=10


sudo ip netns exec ns_mid tc qdisc change dev veth2 root handle 1: netem delay "$OWD_SND"ms 30ms
sudo ip netns exec ns_mid tc qdisc change dev veth1 root handle 1: netem delay "$OWD_RCV"ms 30ms

echo "ntrt -c$CONFDIR/ntrt_snd_meas.ini -m$CONFDIR/ntrt_rmcat1.cmds -t120 " > $LOGSDIR"/ntrt.sh"
chmod 777 $LOGSDIR"/ntrt.sh"


#Lets Rock
touch $SYNCTOUCHFILE

sudo ip netns exec ns_snd $LOGSDIR"/ntrt.sh" 


