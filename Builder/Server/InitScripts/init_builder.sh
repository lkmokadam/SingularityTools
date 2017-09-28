#!/bin/bash

cd /home/cades

# Fetch server init scripts
git clone https://github.com/olcf/SingularityTools.git

SCRIPTS_DIR=/home/cades/SingularityTools/Builder/Server/ServerScripts

# Install docker
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -
sudo add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable"
sudo apt-get update
sudo apt-get install -y docker-ce

# Docker by default seems to use aufs which isn't compatible with singularity(atleast when bootstrapping from docker)
# We force the use of overlayfs instead: https://docs.docker.com/engine/userguide/storagedriver/overlayfs-driver/#configure-docker-with-the-overlay-or-overlay2-storage-driver
sudo systemctl stop docker
cat << EOF > /etc/docker/daemon.json
{
  "storage-driver": "overlay2"
}
EOF
sudo systemctl start docker

# Enable apparmor profile
sudo apparmor_parser -r -W ${SCRIPTS_DIR}/apparmour.builder

# Create builder user
sudo useradd --create-home --home-dir /home/builder --shell /bin/bash builder

# Allow builder user to run docker as sudo by adding to docker group
sudo gpasswd -a builder docker

# Increase the number of loop devices
LOOP_MAX=63
for i in $(seq 8 ${LOOP_MAX}); do
  sudo mknod -m 660 "/dev/loop${i}" b 7 "$i"
done

# Install SQLite
sudo apt-get install -y sqlite3 libsqlite3-dev

# Create the Builder database with loop device and build queue tables
# We use AUTOINCREMENT so we can can sort our queue
sqlite3 Builder 'CREATE TABLE available_loops(id INTEGER PRIMARY KEY, loop_id INTEGER);'
sqlite3 Builder 'CREATE TABLE build_queue(id INTEGER PRIMARY KEY AUTOINCREMENT, job_id STRING);'

# Populate the available loop devices
for i in $(seq 0 ${LOOP_MAX}); do
  sqlite3 Builder "INSERT into available_loops(loop_id) VALUES($i)"
done

# Create singularity builder docker image
sudo docker build -t singularity_builder -f ${SCRIPTS_DIR}/Dockerfile  .

# Create builder scratch work directory
mkdir /home/builder/container_scratch
sudo chown builder /home/builder/container_scratch
sudo chgrp builder /home/builder/container_scratch

# Create SSH key for builder
ssh-keygen -f /home/cades/BuilderKey
sudo chown cades /home/cades/BuilderKey

# Install a new version of boost
sudo apt-get install -y cmake
wget https://dl.bintray.com/boostorg/release/1.65.1/source/boost_1_65_1.tar.gz
tar xf boost_1_65_1.tar.gz
cd boost_1_65_1
./bootstrap.sh
sudo ./b2 install

# Install SSH_Sanitizer
cd ${SCRIPTS_DIR}/../SSH_Sanitizer
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTAL_PREFIX="/usr/local" ..
make
sudo make install

# Install SingularityBuilder
cd ${SCRIPTS_DIR}/../SingularityBuilder
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTAL_PREFIX="/usr/local" ..
make
sudo make install

# Add security options to lock down builder user SSH capabilities
SSH_KEY_OPTS='command=\"/usr/local/bin/SSH_Sanitizer \\\"\${SSH_ORIGINAL_COMMAND}\\\"\",no-port-forwarding,no-X11-forwarding,no-agent-forwarding'
sudo sh -c "echo -n \"${SSH_KEY_OPTS} \" > /home/builder/.ssh/authorized_keys"

# Add newly created key to builders authorized_keys
sudo sh -c "cat /home/cades/BuilderKey.pub >> /home/builder/.ssh/authorized_keys"