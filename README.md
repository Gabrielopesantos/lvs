# lvs

A C-based web server using a master-worker model with Unix domain sockets for IPC.

# Building and running

Building:
```sh
make all
```

Running:

`lvs` accepts a port and serve directory arguments, e.g.: `lvs --port {port} --serve-directory {dir}`

By default, if no arguments are provided, it will bind to port 8080 and serve from the current directory.

```sh
make run
```

## References

- https://github.com/mrnugget/helles

## License

All code except `src/http_parser.c` and `src/http_parser.h`: MIT

The http_parser is under copyright of nodejs: [http_parser](https://github.com/nodejs/http-parser)
