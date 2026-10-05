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
# The engine's objects for a web build, which are the same for every game,
# made once here: each build starts from a copy of them (TIDE_BUILD_WARM) and
# compiles the game alone. tide keeps them by its version, its compiler and
# its flags, never by the game's folder.
RUN set -eu; \
    mkdir -p /tmp/warm/game /opt/warm/web-release; \
    printf 'scene Main { }\n' > /tmp/warm/game/game.tide; \
    tide build /tmp/warm/game --web --release -o /tmp/warm/game.html; \
    mv /tmp/warm/game/.tide/web-release/engine /opt/warm/web-release/engine; \
    rm -rf /tmp/warm
ENV TIDE_BUILD_WARM=/opt/warm
COPY builder.mjs ./
CMD ["node", "/builds/builder.mjs"]
