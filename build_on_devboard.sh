#!/bin/bash
set -e
sudo docker build -f on_devboard_docker/jetsonnano.Dockerfile -t tekn0ir/darknet:arm64v8 .
sudo docker push tekn0ir/darknet:arm64v8