FROM registry.fedoraproject.org/fedora:43
RUN dnf -y install wine xorg-x11-server-Xvfb xorg-x11-xauth mesa-dri-drivers mesa-libGL \
    && dnf clean all
ENV WINEPREFIX=/wine WINEDEBUG=-all WINEDLLOVERRIDES="dinput8=n,b;mscoree,mshtml="
RUN mkdir -p /wine && chmod 777 /wine && xvfb-run -a wineboot -i || true
WORKDIR /test
