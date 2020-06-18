#FROM balenalib/jetson-nano-ubuntu:bionic
#RUN [ "cross-build-start" ]
#
#ENV APT_KEY_DONT_WARN_ON_DANGEROUS_USAGE=DontWarn
#ENV DEBIAN_FRONTEND noninteractive
#
#ADD apt-trusted-keys .
#RUN apt-key add apt-trusted-keys && rm apt-trusted-keys
#RUN echo "deb http://international.download.nvidia.com/jetson/repos/common r32 main" > /etc/apt/sources.list.d/nvidia-l4t-apt-source.list && \
#    echo "deb http://international.download.nvidia.com/jetson/repos/t210 r32 main" >> /etc/apt/sources.list.d/nvidia-l4t-apt-source.list
#
#RUN apt-get update
##&& apt-get install -y && \
##    cuda-cudart-10.0
#
#RUN [ "cross-build-end" ]




ARG JETPACK_VERSION=r32.3.1

##################################################
FROM nvcr.io/nvidia/l4t-base:${JETPACK_VERSION} as base
# because Nvidia has no keyserver for Tegra currently, we DL the whole BSP tarball, just for the apt key.
ARG BSP_URI="https://developer.nvidia.com/embedded/dlc/r32-3-1_Release_v1.0/t210ref_release_aarch64/Tegra210_Linux_R32.3.1_aarch64.tbz2"
ARG BSP_SHA512="13c4dd8e6b20c39c4139f43e4c5576be4cdafa18fb71ef29a9acfcea764af8788bb597a7e69a76eccf61cbedea7681e8a7f4262cd44d60cefe90e7ca5650da8a"

WORKDIR /tmp
# install apt key and configure apt sources
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        wget \
    && BSP_SHA512_ACTUAL="$(wget --https-only -nv --show-progress --progress=bar:force:noscroll -O- ${BSP_URI} | tee bsp.tbz2 | sha512sum -b | cut -d ' ' -f 1)" \
    && [ ${BSP_SHA512_ACTUAL} = ${BSP_SHA512} ] \
    && echo "Extracting bsp.tbz2" \
    && tar --no-same-permissions -xjf bsp.tbz2 \
    && cp Linux_for_Tegra/nv_tegra/jetson-ota-public.key /etc/apt/trusted.gpg.d/jetson-ota-public.asc \
    && chmod 644 /etc/apt/trusted.gpg.d/jetson-ota-public.asc \
    && cp Linux_for_Tegra/bootloader/nv_boot_control.conf /etc/nv_boot_control.conf \
    && chmod 644 /etc/nv_boot_control.conf

# This determines what <SOC> gets filled in in the nvidia apt sources list:
# putting it here so there's a common layer for all boards and build_all.sh builds faster
# valid choices: t210, t186, t194
ARG SOC="t210"
# r32.4 for r32.4.2 or r32 for r32.3.1
ARG APT_SOURCE_JETPACK_VERSION="r32"

RUN echo "deb https://repo.download.nvidia.com/jetson/common ${APT_SOURCE_JETPACK_VERSION} main" > /etc/apt/sources.list.d/nvidia-l4t-apt-source2.list \
    && echo "deb https://repo.download.nvidia.com/jetson/${SOC} ${APT_SOURCE_JETPACK_VERSION} main" >> /etc/apt/sources.list.d/nvidia-l4t-apt-source2.list \
    && apt-get update
# the final apt-get update to test it works.



###################################################
## Finally, copy the working stuff to a fresh base,
## just in case there are some files still around
##FROM nvcr.io/nvidia/l4t-ml:r32.4.2-py3 as l4t-xbase
#FROM nvcr.io/nvidia/l4t-base:${JETPACK_VERSION} as l4t-xbase
## Add docker cross architecture build toolchain
#COPY --from=balenalib/generic-aarch64-ubuntu:bionic-build /usr/bin/qemu-aarch64-static /usr/bin
#COPY --from=balenalib/generic-aarch64-ubuntu:bionic-build /usr/bin/cross-build-start /usr/bin
#COPY --from=balenalib/generic-aarch64-ubuntu:bionic-build /usr/bin/cross-build-end /usr/bin
#COPY --from=balenalib/generic-aarch64-ubuntu:bionic-build /usr/bin/resin-xbuild /usr/bin
#COPY --from=balenalib/generic-aarch64-ubuntu:bionic-build /bin/sh.real /bin
#
#COPY --from=base /etc/apt/trusted.gpg.d/jetson-ota-public.asc /etc/apt/trusted.gpg.d/jetson-ota-public.asc
#COPY --from=base /etc/apt/sources.list.d/nvidia-l4t-apt-source.list /etc/apt/sources.list.d/nvidia-l4t-apt-source.list
#ENV SUDO_FORCE_REMOVE=yes
#
#RUN [ "cross-build-start" ]
#RUN apt-get purge -y --autoremove sudo \
#    &&  for f in $(find / -perm 4000); do chmod -s "$f"; done;
## sudo should not be in an image, among other suid binaries
#RUN [ "cross-build-end" ]


###################################################
#FROM l4t-xbase as yolo-app-build
##FROM gcr.io/teknoir-poc/jetson-nano-base:arm64v8 as yolo-app-build
#RUN [ "cross-build-start" ]
#
#ENV DEBIAN_FRONTEND=noninteractive
##RUN apt update && apt-get --no-install-recommends install -y build-essential gcc make cmake cmake-gui cmake-curses-gui libssl-dev git
#RUN apt update && apt-get --no-install-recommends install -y \
#    build-essential gcc make libssl-dev git curl libopenblas-dev \
#    gnupg2 \
#    bzip2 \
#    lbzip2 \
#    apt-utils \
#    wget \
#    python \
#    device-tree-compiler \
#    libegl1 \
#    libffi6 \
#    libx11-6 \
#    libxext6 \
#    libgles2 \
#    libwayland-egl1 \
#    libxkbcommon0 \
#    libasound2 \
#    libglib2.0-0 \
#    libgstreamer1.0-0 \
#    libgstreamer-plugins-base1.0-0 \
#    libgstreamer-plugins-bad1.0-0 \
#    libcairo2 \
#    libdatrie1 \
#    libfontconfig1 \
#    libharfbuzz0b \
#    libpangoft2-1.0-0 \
#    libpixman-1-0 \
#    libxrender1 \
#    libpango-1.0-0 \
#    libpangocairo-1.0-0 \
#    libinput10 \
#    libjpeg-turbo8 \
#    libpng16-16 \
#    libunwind8 \
#    libwayland-cursor0

## Install Cmake
#RUN CMAKE_VERSION=3.15 && \
#    CMAKE_BUILD=3.15.0 && \
#    curl -L https://cmake.org/files/v${CMAKE_VERSION}/cmake-${CMAKE_BUILD}.tar.gz | tar -xzf - && \
#    cd /cmake-${CMAKE_BUILD} && \
#    ./bootstrap --parallel=$(grep ^processor /proc/cpuinfo | wc -l) && \
#    make -j"$(grep ^processor /proc/cpuinfo | wc -l)" install && \
#    rm -rf /cmake-${CMAKE_BUILD}
#
#ENV PAHO_MQTT_HOME=/paho.mqtt
#ENV C_INCLUDE_PATH=${PAHO_MQTT_HOME}/include:${C_INCLUDE_PATH}
#ENV CPATH=${PAHO_MQTT_HOME}/include:$CPATH
#WORKDIR ${PAHO_MQTT_HOME}
#RUN git clone https://github.com/eclipse/paho.mqtt.c.git && \
#    cd paho.mqtt.c && git checkout v1.3.1 && \
#    cmake -Bbuild -H. -DPAHO_WITH_SSL=TRUE -DPAHO_BUILD_DOCUMENTATION=FALSE -DPAHO_BUILD_SAMPLES=FALSE -DPAHO_ENABLE_TESTING=FALSE -DCMAKE_INSTALL_PREFIX=${PAHO_MQTT_HOME} && \
#    cmake --build build/ --target install
#
##ENV L4T_DIR=/usr/src/t4l
##ENV L4T_NV_TEGRA_DIR=${L4T_DIR}/nv_tegra
##RUN mkdir -p ${L4T_DIR}
##ADD scripts/* ${L4T_DIR}
##RUN cd ${L4T_DIR} && \
##    wget -qO- https://developer.nvidia.com/embedded/dlc/r32-3-1_Release_v1.0/t210ref_release_aarch64/Tegra210_Linux_R32.3.1_aarch64.tbz2 | tar -jxpf - --strip-components=1 -C ./ && \
##    install --owner=root --group=root "${L4T_NV_TEGRA_DIR}/jetson-ota-public.key" "/etc/apt" && \
##    apt-key add "/etc/apt/jetson-ota-public.key" && \
##    rm -f "/etc/apt/jetson-ota-public.key" && \
##    ./nv-apply-debs.sh
#
#RUN git clone https://github.com/eclipse/paho.mqtt.cpp && \
#    cd paho.mqtt.cpp && \
#    cmake -Bbuild -H. -DPAHO_BUILD_DOCUMENTATION=FALSE -DPAHO_BUILD_SAMPLES=FALSE -DCMAKE_INSTALL_PREFIX=${PAHO_MQTT_HOME} -DCMAKE_PREFIX_PATH=${PAHO_MQTT_HOME} && \
#    cmake --build build/ --target install
#
#RUN cp -rf ${PAHO_MQTT_HOME}/lib/* /usr/lib/ && \
#    cp -rf ${PAHO_MQTT_HOME}/include/* /usr/include/
#
#WORKDIR /darknet
#RUN git clone https://github.com/AlexeyAB/darknet.git && \
#    cd darknet && \
#    make GPU=1 LIBSO=1 ARCH=" -gencode arch=compute_53,code=[sm_53,compute_53]" LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/cuda-10.2/lib64
##    make LIBSO=1
##WORKDIR /darknet
##RUN git clone https://github.com/AlexeyAB/darknet.git && \
##    cd darknet && \
##    ./build.sh
#
#RUN cp -rf /darknet/darknet/libdarknet.so /usr/lib/ && \
#    cp -rf /darknet/darknet/include/* /usr/include/
#
#WORKDIR /app
#ADD . /app
#RUN cmake -Bbuild -H. -DCMAKE_INSTALL_PREFIX=/app && \
#    cmake --build build/ --target install
#
#RUN [ "cross-build-end" ]
#
#
###########################
#FROM l4t-xbase as yolo-app
#
#RUN [ "cross-build-start" ]
#ENV DEBIAN_FRONTEND=noninteractive
#RUN apt update && apt-get --no-install-recommends install -y openssl
#
#COPY --from=yolo-app-build /paho.mqtt/lib /usr/lib
#COPY --from=yolo-app-build /darknet/darknet/libdarknet.so /usr/lib
#COPY --from=yolo-app-build /app/bin /usr/bin
#ADD ./darknet /darknet
#RUN [ "cross-build-end" ]
#
#CMD ["/usr/bin/teknoir_app"]