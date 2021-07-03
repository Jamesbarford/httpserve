# httpserve

A programme that reads from `stdin` and serves this content by default on `localhost:8000`. Born from needing to test setting arbitrary http response headers.


## Examples

To serve up a hello world html page:

```bash
echo '<html><h1>Hello, World!<h1></html>' | httpserve
```

You can read files from stdin with something like the following:

```bash
cat ./index.html | httpserve
```

### Setting custom headers
Set custom headers this with the  `-h` switch.

```bash
echo '{"foo": "bar"}' | httpserve \
  -h 'Content-Type: application/json' \
  -h 'Connection: close'
```

### Setting a custom port
Set a custom port with the `-p` switch.

```bash
cat ./content.html | httpserve -p 9000
```
Will serve the output of `cat ./content.html` on port `9000`


# Install
```bash
make clean
make
make install # Might need to be run with sudo privileges
``
