#!/bin/bash

cd "$(dirname "$0")"


/usr/lib64/openmpi/bin/mpirun -np 2 /export/alfthan/SPH/bin/sph.out




