ARG DOCKER_TAG=latest
FROM espressif/idf:${DOCKER_TAG}

# Locale (ESP-IDF expects UTF-8)
ENV LC_ALL=C.UTF-8
ENV LANG=C.UTF-8

# Install udev + USB tools so serial devices appear inside the container
RUN apt-get update && \
    apt-get install -y \
        udev \
        usbutils \
        hwdata \
        pciutils \
        file && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

# Required so udev behaves correctly inside Docker
ENV container docker

# Ensure udev runtime directory exists
RUN mkdir -p /run/udev

# Add udev rules for common ESP32 USB bridges
# CP210x (Silicon Labs), CH340, FTDI
RUN echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="10c4", MODE="0666"'  > /etc/udev/rules.d/99-esp.rules && \
    echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", MODE="0666"' >> /etc/udev/rules.d/99-esp.rules && \
    echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="0403", MODE="0666"' >> /etc/udev/rules.d/99-esp.rules

# Reload udev rules (safe even if udev isn't running yet)
RUN udevadm control --reload-rules || true

# Source ESP-IDF automatically in interactive shells
RUN echo "source /opt/esp/idf/export.sh > /dev/null 2>&1" >> ~/.bashrc

# Keep Espressif's entrypoint
ENTRYPOINT [ "/opt/esp/entrypoint.sh" ]

# Use init so udev actually runs and creates /dev/ttyUSB0
CMD ["/sbin/init"]
