FROM gcc:14-bookworm

WORKDIR /app

COPY Makefile common.h client.c server.c reservation.c reservation.h logger.c logger.h ./
COPY scripts ./scripts
COPY tests ./tests

RUN make && mkdir -p results

# Keep the container alive; open terminals with `docker exec -it cinema bash`.
CMD ["bash"]
