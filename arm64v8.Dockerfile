FROM gcr.io/teknoir/darknet_base:jetson_nano
ENV NAMES_FILE="/darknet/coco.names"
ENV CFG_FILE="/darknet/yolov3.cfg"
ENV WEIGHTS_FILE="/darknet/yolov3.weights"
ENV MODEL="yolov3"
ADD ./darknet /darknet
