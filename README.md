# vaapi-level-zero-interop

install dependencies

```bash
cd workdir/source
git clone https://github.com/oneapi-src/level-zero.git
cd level-zero
mkdir build && cd build
cmake ..
make -j8
sudo make install
```

generate spv kernel file from OpenCL kernel source

```bash
# for skl-gen9
ocloc -file copy_nv12.cl -device skl
# for dg2
ocloc -file copy_nv12.cl -device dg2
```
