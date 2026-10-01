FROM gcc:14-bookworm

WORKDIR /app

COPY Makefile ./
COPY src ./src
COPY scripts ./scripts
COPY tests ./tests

RUN make && mkdir -p results

# Keep the container alive; open terminals with `docker exec -it cinema bash`.
CMD ["bash"]
