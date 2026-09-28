FROM gcc:14-bookworm

WORKDIR /app
COPY common.h client.c mock_server.c Makefile README.md ./
COPY scripts ./scripts
RUN make

CMD ["bash"]
