ARG QT_VERSION=6.9.3

# FORCE the platform to linux/amd64 so it works on GitHub Actions
# (Even if you build this on ARM machine)
# Use Debian testing for GLIBC 2.38+ compatibility with Qt 6.9.3 moc binary
FROM --platform=linux/amd64 debian:testing-slim@sha256:03841df45ea063aaf5937b2931d3560d89fa5cd839382ebd5ab50348ebc1de37  AS dep-builder
ARG QT_VERSION

ENV DEBIAN_FRONTEND=noninteractive

# Install builder tools
RUN apt-get update && apt-get install -y --no-install-recommends \
    wget tar build-essential python3 python3-pip python3-venv ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# 1. Build ICU 56 (Legacy dependency)
WORKDIR /tmp
RUN wget -q https://github.com/unicode-org/icu/releases/download/release-56-1/icu4c-56_1-src.tgz && \
    tar -xzf icu4c-56_1-src.tgz && cd icu/source && \
    ./configure --prefix=/usr/local --enable-static=no && \
    make -j$(nproc) && make install

# 2. Install Qt
#
# To explore available Qt versions and components, run these commands locally:
#
# List all available Qt versions for Linux desktop:
#   aqt list-qt linux desktop
#
# List available architectures for a specific Qt version:
#   aqt list-qt linux desktop --arch 6.9.3
#
# List available modules for a specific Qt version and architecture:
#   aqt list-qt linux desktop --modules 6.9.3 linux_gcc_64
#
# Example output for versions:
#   5.15.2  6.2.4  6.5.3  6.6.3  6.7.3  6.8.2  6.9.3
#
# Example output for architectures (for Qt 6.9.3):
#   linux_gcc_64  linux_gcc_arm64  linux_clang_arm64
#
# Example output for modules (for Qt 6.9.3 linux_gcc_64):
#   qtmultimedia  qt5compat  qtcharts  qtdatavis3d  qtnetworkauth
#   qtpurchasing  qtquick3d  qtremoteobjects  qtscxml  qtsensors
#   qtserialbus  qtserialport  qtspeech  qtvirtualkeyboard  qtwebchannel
#   qtwebengine  qtwebsockets  qtwebview
RUN python3 -m venv /tmp/venv && \
    /tmp/venv/bin/pip install aqtinstall && \
    /tmp/venv/bin/aqt install-qt linux desktop ${QT_VERSION} linux_gcc_64 -m qtmultimedia qt5compat --outputdir /opt/qt

# --- Final Stage ---
FROM --platform=linux/amd64 debian:testing-slim@sha256:03841df45ea063aaf5937b2931d3560d89fa5cd839382ebd5ab50348ebc1de37
ARG QT_VERSION
ENV DEBIAN_FRONTEND=noninteractive

# Copy artifacts
COPY --from=dep-builder /usr/local/lib/libicu* /usr/local/lib/
COPY --from=dep-builder /usr/local/include/unicode /usr/local/include/unicode
COPY --from=dep-builder /opt/qt /opt/qt

# Install CI tools
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    build-essential cmake git ninja-build locales \
    autoconf automake \
    libgl1-mesa-dev libxkbcommon0 libx11-6 libx11-xcb1 libxcb1 \
    libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
    libxcb-render-util0 libxcb-xinerama0 libxcb-shape0 libxcb-cursor0 libglib2.0-0 \
    libssl-dev zlib1g-dev uuid-dev libbrotli-dev \
    libfontconfig-dev libfreetype-dev libdbus-1-dev \
    && rm -rf /var/lib/apt/lists/* && ldconfig

# Configure UTF-8 locale for Qt
RUN locale-gen C.UTF-8 && update-locale LANG=C.UTF-8

# Set ENV safely (Without self-referencing unset variables)
ENV QT_VERSION=${QT_VERSION}
ENV PATH=/opt/qt/${QT_VERSION}/gcc_64/bin:$PATH
ENV LD_LIBRARY_PATH=/opt/qt/${QT_VERSION}/gcc_64/lib:/usr/local/lib
ENV Qt6_DIR=/opt/qt/${QT_VERSION}/gcc_64/lib/cmake/Qt6
ENV CMAKE_PREFIX_PATH=/opt/qt/${QT_VERSION}/gcc_64/lib/cmake/Qt6:/opt/qt/${QT_VERSION}/gcc_64:$CMAKE_PREFIX_PATH
ENV QT_INSTALL_PATH=/opt/qt/${QT_VERSION}/gcc_64
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8
