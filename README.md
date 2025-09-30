# DNS Server Setup and Test

## Running the DNS Server

1. Navigate to the build directory and compile the project:
```bash
cd build
cmake ..
make
sudo ./aiori
```
2. In another terminal:
```
dig @127.0.0.1 -p 5353 example.com
```
