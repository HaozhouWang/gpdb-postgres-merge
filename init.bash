#!/bin/bash

/usr/bin/pip2 install --upgrade pip
/usr/bin/pip2 install psutil==4.4.2 lockfile
cd /home/gpadmin
gpdb_src/concourse/scripts/setup_gpadmin_user.bash

su - gpadmin
cat >> ~/.bash_profile <<EOF
export PS1='\n\w\n$ '
EOF
source ~/.bash_profile

su - gpadmin -c "
pushd /home/gpadmin/gpdb_src/; \
KCFLAGS=-ggdb3 CFLAGS=\"-O0 -g3\" ./configure --prefix=/home/gpadmin/gpdb.pl  --with-gssapi  --with-libedit-preferred --with-perl  --with-python --enable-cassert --enable-debug --enable-depend --with-libxml --disable-orca --without-zstd --disable-gpcloud; \
make -j8; \
make install; \
popd; \

source /home/gpadmin/gpdb.pl/greenplum_path.sh; \
initdb -D ~/data/ --max_connections=20; \
"
echo "listen_addresses='*'" >> /home/gpadmin/data/postgresql.conf; 
echo "port=7000" >> /home/gpadmin/data/postgresql.conf;
echo "gp_contentid=0" >> /home/gpadmin/data/postgresql.conf;
echo "gp_dbid=0" >> /home/gpadmin/data/internal.auto.conf;


mv /home/gpadmin/gpdb_src/start.sh /home/gpadmin/ ;

