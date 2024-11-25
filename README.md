# await

A tool to check the availability of one or multiple network endpoints.

## Features

- small statically linked binary
- minimal memory usage

## Installation

```bash
wget -O await https://raw.githubusercontent.com/jackcvr/await/refs/heads/main/await && chmod +x ./await
```

## Usage

```bash
await <host>:<port>[/<timeout>] [<host>:<port>[/<timeout>] ...] [-- <command>]
```

## Arguments

- `<host>:<port>/<timeout>`: Specifies the hostname, port, and optional timeout (in seconds).
- `--`: Separates the list of endpoints from the optional command.
- `<command>`: The command to execute after all endpoints are connected.

## Example

```bash
await localhost:22 localhost:80/10 -- ls -la
```

This command checks the availability of two endpoints:

- localhost:22: waits indefinitely.
- localhost:80: waits for a maximum of 10 seconds.
- if both endpoints become available within their respective timeouts, the `ls -la` command is executed.

## License

MIT
