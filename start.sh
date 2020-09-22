#!/bin/bash

source /home/gpadmin/gpdb.pl/greenplum_path.sh

gp_version=$(/home/gpadmin/gpdb.pl/bin/postgres --gp-version)

/home/gpadmin/gpdb.pl/sbin/gpsegstart.py -M mirrorless -V "${gp_version}" -n 1 --era c033c10679c7a813_181008153119 -t 600 --master-checksum-version 0 -D "1|0|p|p|n|u|localhost.localdomain|localhost.localdomain|7000|/home/gpadmin/data"

# keep the container running
sleep infinity
