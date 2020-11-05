#!/bin/bash
set -e

USER=${1:-teknoir}
DEVICE=${2:-jetson-nano-test}

ssh -o "UserKnownHostsFile=/dev/null" -o "StrictHostKeyChecking=no" -t ${USER}@${DEVICE}.local "mkdir -p ~/build_darknet_base"
scp -o "UserKnownHostsFile=/dev/null" -o "StrictHostKeyChecking=no" -C -r ./on_devboard_docker/jetsonnano.Dockerfile ${USER}@${DEVICE}.local:~/build_darknet_base/Dockerfile
scp -o "UserKnownHostsFile=/dev/null" -o "StrictHostKeyChecking=no" -C -r ./app ${USER}@${DEVICE}.local:~/build_darknet_base/
ssh -o "UserKnownHostsFile=/dev/null" -o "StrictHostKeyChecking=no" -t ${USER}@${DEVICE}.local "cd ~/build_darknet_base && sudo docker build -t gcr.io/teknoir/darknet_base:jetson_nano ."
ssh -o "UserKnownHostsFile=/dev/null" -o "StrictHostKeyChecking=no" -t ${USER}@${DEVICE}.local "sudo docker save gcr.io/teknoir/darknet_base:jetson_nano | gzip > ~/build_darknet_base/darknet_base_jetson_nano.tar.gz"
scp -o "UserKnownHostsFile=/dev/null" -o "StrictHostKeyChecking=no" ${USER}@${DEVICE}.local:~/build_darknet_base/darknet_base_jetson_nano.tar.gz .
docker load < darknet_base_jetson_nano.tar.gz
docker push gcr.io/teknoir/darknet_base:jetson_nano