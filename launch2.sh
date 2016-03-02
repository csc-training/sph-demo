#!/bin/bash

function addhost {
  host=$1
  file=$2
  ping -c 1 -w 1 $host &> /dev/null;
  if [ $? -eq 0 ]; then
     echo "$host slots=2"  >> $2
  fi
}

echo "Preparing..."
HOSTLIST=( frontend sisu01 sisu02 sisu03 sisu04 sisu05 sisu06 sisu07 sisu08 sisu09 )
if [ -e .hosts ]
then
    rm .hosts
fi
for i in ${HOSTLIST[@]}; do
    #add host to list
    addhost $i .hosts
    #turn off blink
    ssh $i /usr/sbin/blink1-tool --off -q
done;


n_hosts=$(wc -l .hosts|gawk '{print $1 * 2}')

echo "Launching on $n_hosts nodes"

/usr/lib64/openmpi/bin/mpirun -np $n_hosts --hostfile .hosts /export/alfthan/SPH/bin/sph.out

for i in ${HOSTLIST[@]}; do
    #turn off blink
    ssh $i /usr/sbin/blink1-tool --off -q
done;



