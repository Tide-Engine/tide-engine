# The build service's image, for Fly (fly.toml): Node and the service, nothing
# else. It runs no tide and unpacks nothing: its builders do, in machines of
# their own (Dockerfile.builder). It runs as root, as the volume it keeps
# everything on is root's.
FROM node:24-alpine
WORKDIR /builds
COPY builds.mjs store.mjs machines.mjs main.mjs tide.version ./
ENV PORT=8080 TIDE_BUILDS_DATA=/data
EXPOSE 8080
CMD ["node", "main.mjs"]
