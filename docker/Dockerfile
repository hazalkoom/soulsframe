FROM registry.fedoraproject.org/fedora:43
RUN dnf -y install cmake ninja-build ccache git python3 clang-tools-extra \
      mingw64-gcc-c++ mingw64-winpthreads-static mingw64-headers mingw64-crt \
      binutils \
    && dnf clean all
ENV CCACHE_DIR=/ccache
WORKDIR /src
