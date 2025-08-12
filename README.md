Warining this is a toy, small project that does not guarantee correctness or suport. For any real use case you are advised to use https://github.com/IHaskell/IHaskell instead.

# HJNKernel - Haskell Jupyter Notebook Kernel

A custom Jupyter kernel implementation that enables interactive Haskell programming in Jupyter lab notebooks through GHCi (Glasgow Haskell Compiler Interactive).

## Overview

HJNKernel is a C++ implementation of the Jupyter kernel protocol that communicates with GHCi to execute Haskell code interactively. The kernel supports the full Jupyter messaging protocol, including code execution.

A custom JSON parser handles message serialization and deserialization, while custom SHA-256 message authentication is just a pro forma implementation since Jupyter lab signs the messages by default even when both client and kernel are run locally.

## Usage

A look into examples: (examples/Fourier.ipynb, examples/Mandelbrot.ipynb, examples/Newton.ipynb) should give the right idea about how the project is ment to be used.

In general single line code cells are send directly to ghci and multi-line are reloaded into main module if sucessfully compiled and ignored otherwise. Multi-line code cells are expected to house only top level definitions and one line cells expressions based on the definitions from multi-line cells. This is a major limitation that was taken on for the sake of simplicity of the kernel implementation.

## Dependencies

Kernel executable depends on ZeroMQ. Project only makes sense with GHC and Jupyter installed.

## Installation

### Building the Kernel

Project is setup with Visual Studio, but should be relatevly easy to build using mingw on Windows as well. Building on Linux would require removing dependency on windows.h

### Kernel Installation and Registration

Once the kernel executable is successfully built, the next step is to install and register it with Jupyter. 

The kernel specification file should be structured as follows, with paths adjusted to match your installation:

```json
{
  "argv": [
    "C:\\path\\to\\your\\HJNKernel.exe",
    "{connection_file}"
  ],
  "display_name": "Haskell (GHCi)",
  "language": "haskell",
  "interrupt_mode": "message",
  "env": {
    "PATH": "C:\\path\\to\\ghc\\bin;%PATH%"
  }
}
```

The most straightforward approach is to use the `jupyter kernelspec install` command, which copies the kernel specification to the appropriate system or user directory. Navigate to the directory containing your kernel specification and run:

```bash
jupyter kernelspec install ./haskell --user
```
Verify the kernel installation by listing available kernels with `jupyter kernelspec list`. Your Haskell kernel should appear in the output, indicating that Jupyter has successfully discovered and registered the kernel specification. If everything is correct you should see haskell

Alternativly you can "install" manualy. Jupyter kernels are defined by a `kernel.json` file that specifies how to launch the kernel and provides metadata about its capabilities.  Create a directory for your kernel specification, typically named `haskell` or `hjnkernel`, but location for that may vary.

The `argv` field specifies the command line used to launch the kernel, including the path to your compiled HJNKernel.exe and the connection file placeholder that Jupyter will replace with the actual connection file path. 

Verify the kernel installation by listing available kernels with `jupyter kernelspec list`. Your Haskell kernel should appear in the output, indicating that Jupyter has successfully discovered and registered the kernel specification.

## Testing Framework

The HJNKernel includes a few python test scripts allowing to test the kernel without the use of jupyter lab.

## Protocol Implementation

The HJNKernel implements Jupyter messaging protocol version 5.3. Not all message types are supported yet.
