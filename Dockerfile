# build
FROM alpine:latest AS build_env

RUN apk update && apk upgrade 
RUN apk add --no-cache gcc g++ make cmake python3 zlib-dev

COPY . sources
WORKDIR /souces
RUN cmake /sources -DCMAKE_INSTALL_PREFIX=/install
RUN cmake --build . -t install

# runtime
FROM alpine:latest AS runtime_env
WORKDIR /TeeWare-Server/
RUN apk update && apk upgrade
RUN apk add --no-cache libstdc++
COPY --from=build_env /install .

ENTRYPOINT ["./TeeWare-Server"]
