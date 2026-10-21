# The site on Fly.io (see fly.toml), built beforehand by `npm run build`: its
# pages need the rest of the repo (the grammar, the code they show, the demos),
# so the image only takes what the build made. The server needs nothing from
# node_modules: the build bundles it all.
FROM node:24-alpine

RUN apk add --no-cache su-exec

WORKDIR /site
ENV NODE_ENV=production PORT=3000
COPY package.json headers.js releases.js analytics.js admin.js prototypes.js server.js prototypes.jso[n] ./
COPY build ./build

EXPOSE 3000
# A new Fly volume belongs to root. Only startup owns this directory; the
# server runs as node, and no request data is copied into the image.
CMD ["sh", "-c", "mkdir -p /data && chown node:node /data && chmod 700 /data && exec su-exec node node server.js"]
