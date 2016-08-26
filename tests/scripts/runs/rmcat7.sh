#!/bin/bash
programname=$0

NSSND="ns_snd"
NSRCV="ns_rcv"
SENDER="sender"
RECEIVER="receiver"
LOGSDIR="logs"
REPORTSDIR="reports"
SCRIPTSDIR="scripts"
TEMPDIR=$SCRIPTSDIR"/temp"
EVALDIR=$SCRIPTSDIR"/evals"
CONFDIR=$SCRIPTSDIR"/configs"
RUNDIR=$SCRIPTSDIR"/runs"

rm -R -f $LOGSDIR/*

#setup defaults
DURATION=310
OWD_SND=100
OWD_RCV=100

  rm $TEMPDIR/peer1/*
  rm $TEMPDIR/peer2/*
  rm $TEMPDIR/peer3/*
  rm $TEMPDIR/peer4/*
  rm $TEMPDIR/peer5/*

while [[ $# -gt 1 ]]
do
key="$1"
case $key in
    -s|--owdsnd)
    OWD_SND="$2"
    shift # past argument
    ;;
    -r|--owdrcv)
    OWD_RCV="$2"
    shift # past argument
    ;;
    *)
            # unknown option
    ;;
esac
shift # past argument or value
done



  sudo ip netns exec ns_mid tc qdisc change dev veth2 root handle 1: netem delay "$OWD_SND"ms
  sudo ip netns exec ns_mid tc qdisc change dev veth1 root handle 1: netem delay "$OWD_RCV"ms
  
  PEER1_SND="$TEMPDIR/sender_1.sh"
  PEER1_SND_EMBED="$TEMPDIR/sender_1_cerbero.sh"
  
  echo "ntrt -c$CONFDIR/ntrt_snd_meas.ini -m$CONFDIR/ntrt_rmcat7.cmds -t$DURATION &" > $PEER1_SND
  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_1.dat &" >> $PEER1_SND 
  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_2.dat &" >> $PEER1_SND 
#  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_3.dat &" >> $PEER1_SND 
#  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_4.dat &" >> $PEER1_SND 
#  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_5.dat &" >> $PEER1_SND 
#  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_6.dat &" >> $PEER1_SND 
#  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_7.dat &" >> $PEER1_SND 
  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_8.dat &" >> $PEER1_SND 
  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_9.dat &" >> $PEER1_SND 
  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_10.dat &" >> $PEER1_SND 
  echo -n "./$SENDER" >> $PEER1_SND
  ./$CONFDIR/peer1params.sh >> $PEER1_SND
  chmod 777 $PEER1_SND
  
  #echo "/home/balazs/gst/cerbero-1.6/cerbero-uninstalled run ./$PEER1_SND" > $PEER1_SND_EMBED
  echo "./$PEER1_SND" > $PEER1_SND_EMBED 
  chmod 777 $PEER1_SND_EMBED  

  PEER2_SND="$TEMPDIR/sender_2.sh"
  PEER2_SND_EMBED="$TEMPDIR/sender_2_cerbero.sh"
  echo -n "./$SENDER" > $PEER2_SND
  ./$CONFDIR/peer2params.sh >> $PEER2_SND
  chmod 777 $PEER2_SND
  
  #echo "/home/balazs/gst/cerbero-1.6/cerbero-uninstalled run ./$PEER2_SND" > $PEER2_SND_EMBED 
  echo "./$PEER2_SND" > $PEER2_SND_EMBED 
  chmod 777 $PEER2_SND_EMBED  

  PEER1_RCV="$TEMPDIR/receiver_1.sh"
  PEER1_RCV_EMBED="$TEMPDIR/receiver_1_cerbero.sh"
  echo "ntrt -c$CONFDIR/ntrt_rcv_meas.ini -t$DURATION &" > $PEER1_RCV
  echo "iperf -s -p 1234 &" >> $PEER1_RCV
  echo -n "./$RECEIVER" >> $PEER1_RCV
  ./$CONFDIR/peer1params.sh >> $PEER1_RCV
  chmod 777 $PEER1_RCV
  
  #echo "/home/balazs/gst/cerbero-1.6/cerbero-uninstalled run ./$PEER1_RCV" > $PEER1_RCV_EMBED 
  echo "./$PEER1_RCV" > $PEER1_RCV_EMBED 
  chmod 777 $PEER1_RCV_EMBED  

  PEER2_RCV="$TEMPDIR/receiver_2.sh"
  PEER2_RCV_EMBED="$TEMPDIR/receiver_2_cerbero.sh"
  echo -n "./$RECEIVER" > $PEER2_RCV
  ./$CONFDIR/peer2params.sh >> $PEER2_RCV
  chmod 777 $PEER2_RCV
  
  #echo "/home/balazs/gst/cerbero-1.6/cerbero-uninstalled run ./$PEER2_RCV" > $PEER2_RCV_EMBED 
  echo "./$PEER2_RCV" > $PEER2_RCV_EMBED 
  chmod 777 $PEER2_RCV_EMBED  


  #start receiver and sender
  sudo ip netns exec $NSRCV ./$PEER1_RCV_EMBED 2> $LOGSDIR"/"receiver.log &
  sudo ip netns exec $NSRCV ./$PEER2_RCV_EMBED 2> $LOGSDIR"/"receiver_2.log &
  sleep 2
  sudo ip netns exec $NSSND ./$PEER1_SND_EMBED 2> $LOGSDIR"/"sender.log &
  sudo ip netns exec $NSSND ./$PEER2_SND_EMBED 2> $LOGSDIR"/"sender_2.log &

cleanup()
# example cleanup function
{
  pkill receiver
  pkill sender
  pkill ntrt
  pkill pyhon3
  ps -ef | grep 'main.sh' | grep -v grep | awk '{print $2}' | xargs kill
}
 
control_c()
# run if user hits control-c
{
  echo -en "\n*** Program is terminated ***\n"
  cleanup
  exit $?
}

trap control_c SIGINT

sleep $DURATION

cleanup


