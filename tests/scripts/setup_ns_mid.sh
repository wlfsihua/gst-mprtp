#!/bin/bash
set -x
VETH1="veth1"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH1" root
tc qdisc add dev "$VETH1" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH1" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

#tc qdisc add dev "$VETH1" root handle 1: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540
#tc qdisc add dev "$VETH1" parent 1: handle 2: netem delay "$LATENCY"ms

#dummynet version
#ipfw pipe flush
#ipfw add 100 pipe 1 ip from 10.0.0.1 to 10.0.0.2
#ipfw pipe 1 config bw 3500Kbit/s queue 1050Kbit/s

VETH4="veth2"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH4" root
tc qdisc add dev "$VETH4" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH4" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

#tc qdisc add dev "$VETH4" root handle 1: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540
#tc qdisc add dev "$VETH4" parent 1: handle 2: netem delay "$LATENCY"ms

VETH="veth5"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH" root
tc qdisc add dev "$VETH" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

#tc qdisc add dev "$VETH" root handle 1: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540
#tc qdisc add dev "$VETH" parent 1: handle 2: netem delay "$LATENCY"ms

VETH="veth6"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH" root
tc qdisc add dev "$VETH" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

#tc qdisc add dev "$VETH" root handle 1: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540
#tc qdisc add dev "$VETH" parent 1: handle 2: netem delay "$LATENCY"ms


VETH="veth9"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH" root
tc qdisc add dev "$VETH" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

#tc qdisc add dev "$VETH" root handle 1: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540
#tc qdisc add dev "$VETH" parent 1: handle 2: netem delay "$LATENCY"ms

VETH="veth10"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH" root
tc qdisc add dev "$VETH" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

#tc qdisc add dev "$VETH" root handle 1: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540
#tc qdisc add dev "$VETH" parent 1: handle 2: netem delay "$LATENCY"ms

VETH="veth13"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH" root
tc qdisc add dev "$VETH" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

#tc qdisc add dev "$VETH" root handle 1: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540
#tc qdisc add dev "$VETH" parent 1: handle 2: netem delay "$LATENCY"ms

VETH="veth14"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH" root
tc qdisc add dev "$VETH" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

#tc qdisc add dev "$VETH" root handle 1: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540
#tc qdisc add dev "$VETH" parent 1: handle 2: netem delay "$LATENCY"ms

VETH="veth17"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH" root
tc qdisc add dev "$VETH" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

#tc qdisc add dev "$VETH" root handle 1: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540
#tc qdisc add dev "$VETH" parent 1: handle 2: netem delay "$LATENCY"ms

VETH="veth18"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH" root
tc qdisc add dev "$VETH" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540












VETH21="veth21"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH21" root
tc qdisc add dev "$VETH21" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH21" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH22="veth22"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH22" root
tc qdisc add dev "$VETH22" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH22" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH="veth25"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH" root
tc qdisc add dev "$VETH" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH="veth26"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH" root
tc qdisc add dev "$VETH" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH="veth29"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH" root
tc qdisc add dev "$VETH" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH="veth30"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH" root
tc qdisc add dev "$VETH" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH="veth33"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH" root
tc qdisc add dev "$VETH" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH="veth34"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH" root
tc qdisc add dev "$VETH" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH="veth37"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH" root
tc qdisc add dev "$VETH" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH="veth38"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH" root
tc qdisc add dev "$VETH" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540
