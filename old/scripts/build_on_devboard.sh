#!/bin/bash
set -e
sudo docker build -f on_devboard_docker/jetsonnano.Dockerfile -t tekn0ir/darknet_base:arm64v8 .
sudo docker push tekn0ir/darknet_base:arm64v8
#docker run -ti --rm -v $(pwd):/app -v ${HOME}/.docker:/root/.docker -w /app mplatform/manifest-tool:latest --username tekn0ir --password <<password>> push from-spec multi-arch-manifest.yaml
