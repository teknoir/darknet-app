FROM  gcr.io/teknoir/darknet_base:amd64 as yolo-app
ADD ./darknet /darknet
