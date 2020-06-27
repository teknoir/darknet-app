###########################
## deepstream-l4t:5.0-dp-20.04-base
##docker pull nvcr.io/nvidia/deepstream:5.0-dp-20.04-triton
##docker pull nvcr.io/nvidia/deepstream-l4t:5.0-dp-20.04-base
#
##FROM nvcr.io/nvidia/l4t-base:r32.4.2 as l4t-xbase
#FROM nvcr.io/nvidia/l4t-ml:r32.4.2-py3 as l4t-xbase
## Add docker cross architecture build toolchain
#COPY --from=balenalib/generic-aarch64-ubuntu:bionic-build /usr/bin/qemu-aarch64-static /usr/bin
#COPY --from=balenalib/generic-aarch64-ubuntu:bionic-build /usr/bin/cross-build-start /usr/bin
#COPY --from=balenalib/generic-aarch64-ubuntu:bionic-build /usr/bin/cross-build-end /usr/bin
#COPY --from=balenalib/generic-aarch64-ubuntu:bionic-build /usr/bin/resin-xbuild /usr/bin
#COPY --from=balenalib/generic-aarch64-ubuntu:bionic-build /bin/sh.real /bin

##########################
#FROM l4t-xbase as yolo-app-build
FROM balenalib/jetson-nano-ubuntu:bionic as yolo-app-build
RUN [ "cross-build-start" ]

ENV DEBIAN_FRONTEND=noninteractive
#RUN apt update && apt-get --no-install-recommends install -y build-essential gcc make cmake cmake-gui cmake-curses-gui libssl-dev git
RUN apt update && apt-get --no-install-recommends install -y build-essential gcc make libssl-dev git curl libopenblas-dev

# Install Cmake
RUN CMAKE_VERSION=3.15 && \
    CMAKE_BUILD=3.15.0 && \
    curl -L https://cmake.org/files/v${CMAKE_VERSION}/cmake-${CMAKE_BUILD}.tar.gz | tar -xzf - && \
    cd /cmake-${CMAKE_BUILD} && \
    ./bootstrap --parallel=$(grep ^processor /proc/cpuinfo | wc -l) && \
    make -j"$(grep ^processor /proc/cpuinfo | wc -l)" install && \
    rm -rf /cmake-${CMAKE_BUILD}

ENV PAHO_MQTT_HOME=/paho.mqtt
ENV C_INCLUDE_PATH=${PAHO_MQTT_HOME}/include:${C_INCLUDE_PATH}
ENV CPATH=${PAHO_MQTT_HOME}/include:$CPATH
WORKDIR ${PAHO_MQTT_HOME}
RUN git clone https://github.com/eclipse/paho.mqtt.c.git && \
    cd paho.mqtt.c && git checkout v1.3.1 && \
    cmake -Bbuild -H. -DPAHO_WITH_SSL=TRUE -DPAHO_BUILD_DOCUMENTATION=FALSE -DPAHO_BUILD_SAMPLES=FALSE -DPAHO_ENABLE_TESTING=FALSE -DCMAKE_INSTALL_PREFIX=${PAHO_MQTT_HOME} && \
    cmake --build build/ --target install

RUN git clone https://github.com/eclipse/paho.mqtt.cpp && \
    cd paho.mqtt.cpp && \
    cmake -Bbuild -H. -DPAHO_BUILD_DOCUMENTATION=FALSE -DPAHO_BUILD_SAMPLES=FALSE -DCMAKE_INSTALL_PREFIX=${PAHO_MQTT_HOME} -DCMAKE_PREFIX_PATH=${PAHO_MQTT_HOME} && \
    cmake --build build/ --target install

RUN cp -rf ${PAHO_MQTT_HOME}/lib/* /usr/lib/ && \
    cp -rf ${PAHO_MQTT_HOME}/include/* /usr/include/

WORKDIR /darknet
RUN git clone https://github.com/AlexeyAB/darknet.git && \
    cd darknet && \
    make LIBSO=1
#    make GPU=1 LIBSO=1 ARCH="-gencode arch=compute_53,code=[sm_53,compute_53]" LD_LIBRARY_PATH=/usr/local/cuda-10.2/lib64:$LD_LIBRARY_PATH
#WORKDIR /darknet
#RUN git clone https://github.com/AlexeyAB/darknet.git && \
#    cd darknet && \
#    ./build.sh

RUN cp -rf /darknet/darknet/libdarknet.so /usr/lib/ && \
    cp -rf /darknet/darknet/include/* /usr/include/

WORKDIR /app
ADD . /app
RUN cmake -Bbuild -H. -DCMAKE_INSTALL_PREFIX=/app && \
    cmake --build build/ --target install

RUN [ "cross-build-end" ]

##########################
#FROM l4t-xbase as yolo-app
FROM balenalib/jetson-nano-ubuntu:bionic as yolo-app

RUN [ "cross-build-start" ]
ENV DEBIAN_FRONTEND=noninteractive
RUN apt update && apt-get --no-install-recommends install -y openssl

COPY --from=yolo-app-build /paho.mqtt/lib /usr/lib
COPY --from=yolo-app-build /darknet/darknet/libdarknet.so /usr/lib
COPY --from=yolo-app-build /app/bin /usr/bin
RUN [ "cross-build-end" ]

STOPSIGNAL SIGINT
CMD ["/usr/bin/teknoir_app"]