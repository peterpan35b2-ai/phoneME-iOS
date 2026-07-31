FROM eclipse-temurin:8-jdk-jammy

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        make \
        gcc-multilib \
        g++-multilib \
        libc6-dev-i386 \
        lib32stdc++6 \
        zip \
        unzip \
        file \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src/cldc/build/linux_i386
