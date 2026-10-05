# The builders' image (builder.mjs, fly.builder.toml): Node, the builder, and
# tide for Linux at the version tide.version says, installed as install.sh
# installs it: the release's package, checked against the checksums published
# with it. tide brings its own clang and linker, and downloads packages with
# curl and tar. unshare, setpriv and prlimit, which keep a build apart, are
# Debian's own (util-linux). The builder starts as root, to make the network
# namespace a build runs in, and runs tide as the image's `node` user.
FROM node:24-trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl && rm -rf /var/lib/apt/lists/*
WORKDIR /builds
COPY tide.version ./
RUN set -eu; \
    version="$(cat tide.version)"; \
    base="https://github.com/BlenMiner/tide-engine/releases/download/v$version"; \
    cd /tmp; \
    curl -fsSL "$base/tide-linux-x64.tar.gz" -o tide-linux-x64.tar.gz; \
    curl -fsSL "$base/SHA256SUMS" -o SHA256SUMS; \
    grep ' tide-linux-x64.tar.gz$' SHA256SUMS | sha256sum -c -; \
    mkdir -p /opt/tide; \
    tar -xzf tide-linux-x64.tar.gz -C /opt/tide; \
    rm tide-linux-x64.tar.gz SHA256SUMS; \
    /opt/tide/bin/tide version
ENV PATH=/opt/tide/bin:$PATH TIDE=/opt/tide/bin/tide TIDE_NO_UPDATE_CHECK=1
COPY builder.mjs ./
CMD ["node", "/builds/builder.mjs"]
