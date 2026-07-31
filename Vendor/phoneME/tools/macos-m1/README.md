# phoneME on macOS Apple Silicon

phoneME CLDC is a 32-bit VM. This setup builds its supported Linux i386 target
inside an amd64 Docker container and runs it on Apple Silicon through Docker's
x86 emulation.

## Build

```sh
cd /Users/duypham/Developer/phoneME
bash tools/macos-m1/build-cldc.sh
```

Output:

```text
cldc/build/linux_i386/dist/bin/cldc_vm_g
cldc/build/linux_i386/dist/lib/cldc_classes.zip
```

## Verify the VM

```sh
bash tools/macos-m1/run-cldc.sh -help
bash tools/macos-m1/smoke-test.sh
```

The smoke test compiles a CLDC 1.1 class, preverifies it, and executes it with
`cldc_vm_g`.

## Run a preverified application

Mount paths are already handled by `run-cldc.sh`. Pass phoneME VM arguments
after the script name, for example:

```sh
bash tools/macos-m1/run-cldc.sh \
  -classpath dist/lib/cldc_classes.zip:/src/path/to/preverified-classes \
  com.example.Main
```

The application classes must first be compiled for Java 1.4 bytecode and passed
through `dist/bin/preverify`.

## Native arm64 status

A direct arm64 build is not a compiler-flag-only port. CLDC VM object and frame
layouts assume four-byte machine words and 32-bit native pointers. A real
native port requires redesigning those layouts or implementing compressed
references throughout the VM.
