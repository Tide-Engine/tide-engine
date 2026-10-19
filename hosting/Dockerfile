FROM node:24.16.0-bookworm-slim AS frontend
WORKDIR /service
COPY package.json package-lock.json ./
RUN npm ci
COPY vite.config.js jsconfig.json ./
COPY src ./src
COPY static ./static
RUN npm run check && npm run build

FROM node:24.16.0-bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends gosu ca-certificates libstdc++6 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /service
COPY package.json package-lock.json ./
RUN npm ci --omit=dev
COPY *.mjs ./
COPY --from=frontend /service/build ./build
COPY entrypoint.sh ./entrypoint.sh
ENV NODE_ENV=production PORT=8081 TIDE_HOSTING_DATA=/data
EXPOSE 8081
CMD ["sh", "/service/entrypoint.sh"]
