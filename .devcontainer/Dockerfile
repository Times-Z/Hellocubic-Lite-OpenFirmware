FROM python:3.12.3-slim

ENV LC_ALL=C.UTF-8
ENV LANG=C.UTF-8
ENV EDITOR=nvim
ARG USER=debian
ARG UUCP_GID=986
ENV PATH=$PATH:~/.arduino15/packages:~/Arduino/libraries:~/.platformio/penv/bin
ENV ESPPORT=/dev/ttyUSB0

RUN set -xe; \
    apt-get update \
    && apt-get upgrade -y \
    && apt-get install -y \
        build-essential \
        clang-tidy \
        cppcheck \
        curl \
        git \
        gnupg2 \
        liblwip-dev \
        neovim \
        sudo \
        udev \
        usbutils \
    && useradd -m ${USER} \
    && echo "source /opt/esp/idf/export.sh > /dev/null 2>&1" >> /home/${USER}/.bashrc \
    && chown -R ${USER}:${USER} /usr \
    && usermod -aG sudo ${USER} \
    && chown root:root /usr/bin/sudo \
    && chmod 4755 /usr/bin/sudo \
    && echo "${USER} ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/${USER} \
    && groupadd -g ${UUCP_GID} uucp_arch \
    && usermod -aG uucp_arch ${USER} \
    && usermod -aG dialout ${USER} \
    && usermod -aG plugdev ${USER} \
    && git config --global --add safe.directory / \
    && curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh -s 1.4.0 \
    && curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules | tee /etc/udev/rules.d/99-platformio-udev.rules \
    && service udev restart

USER ${USER}

WORKDIR /home/debian

RUN curl -fsSL -o get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py \
    && python3 get-platformio.py \
    && rm get-platformio.py \
    && echo "alias build='pio run'" >> /home/${USER}/.bashrc \
    && echo "alias buildfs='pio run --target buildfs'" >> /home/${USER}/.bashrc \
    && echo "alias flash='pio run --target upload'" >> /home/${USER}/.bashrc \
    && echo "alias flashfs='pio run --target uploadfs'" >> /home/${USER}/.bashrc \
    && echo "alias check='pio check'" >> /home/${USER}/.bashrc \
    && echo "alias monitor='pio device monitor'" >> /home/${USER}/.bashrc
