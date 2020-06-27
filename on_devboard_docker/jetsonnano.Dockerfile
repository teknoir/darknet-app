ARG JETPACK_VERSION=r32.4.2
#ARG JETPACK_VERSION=r32.3.1

##################################################
FROM nvcr.io/nvidia/l4t-base:${JETPACK_VERSION} as yolo-app-build

ENV DEBIAN_FRONTEND=noninteractive
RUN apt update && apt-get --no-install-recommends install -y build-essential gcc make libssl-dev git curl

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

ENV CUDA_VERSION 10.2
WORKDIR /darknet
RUN git clone https://github.com/AlexeyAB/darknet.git && \
    cd darknet && \
    make GPU=1 CUDNN=1 LIBSO=1 ARCH=" -gencode arch=compute_53,code=[sm_53,compute_53]" LD_LIBRARY_PATH=/usr/local/cuda-10.2/lib64:/usr/lib/aarch64-linux-gnu
#WORKDIR /darknet
#RUN git clone https://github.com/AlexeyAB/darknet.git && \
#    cd darknet && \
#    git checkout darknet_yolo_v3_optimal && \
#    mkdir -p share/darknet/ && \
#    ./build.sh

RUN cp -rf /darknet/darknet/libdarknet.so /usr/lib/ && \
    cp -rf /darknet/darknet/include/* /usr/include/

WORKDIR /app
ADD ./app /app
RUN cmake -Bbuild -H. -DCMAKE_INSTALL_PREFIX=/app && \
    cmake --build build/ --target install

##################################################
FROM nvcr.io/nvidia/l4t-base:${JETPACK_VERSION} as app_release

ENV DEBIAN_FRONTEND=noninteractive
RUN apt update && apt-get --no-install-recommends install -y openssl

ENV PAHO_MQTT_HOME=/paho.mqtt
COPY --from=yolo-app-build ${PAHO_MQTT_HOME}/lib /usr/lib
COPY --from=yolo-app-build /darknet/darknet/libdarknet.so /usr/lib
COPY --from=yolo-app-build /darknet/darknet/darknet /usr/bin
COPY --from=yolo-app-build /darknet/darknet/uselib /usr/bin
COPY --from=yolo-app-build /darknet/darknet/data/person.jpg /root
COPY --from=yolo-app-build /app/bin /usr/bin
RUN ln -s /usr/local/cuda-10.2/lib64/libcudart.so.10.2 /usr/lib/aarch64-linux-gnu/libcudart.so.10.0

CMD ["/usr/bin/teknoir_app"]