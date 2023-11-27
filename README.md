# tee for Windows

A simple [**`tee`**](https://en.wikipedia.org/wiki/Tee_(command)) implementation for Microsoft Windows.

![tee](etc/images/tee.png)  
<small>(image created by [Sven](https://commons.wikimedia.org/wiki/User:Sven), CC BY-SA 4.0)</small>

## Usage

```
tee for Windows

Copy standard input to output file(s), and also to standard output.

Usage:
  gizmo.exe [...] | tee.exe [options] <file_1> ... <file_n>

Options:
  -a --append  Append to the existing file, instead of truncating
  -b --buffer  Enable write combining, i.e. buffer small chunks
  -f --flush   Flush output file after each write operation
  -i --ignore  Ignore the interrupt signal (SIGINT), e.g. CTRL+C
  -d --delay   Add a small delay after each read operation
```

### Terminal output

Tee can be used as an intermediate buffer (i.e. *without* writing to a file) to greatly speed-up terminal output:
```
gizmo.exe [...] | tee.exe NUL
```

## Implementation

This is a "native" implementation of the **`tee`** command that builds directly on top of the Win32 API.

It uses multi-threaded I/O and triple buffering for maximum throughput.

## System Requirements

This application requires Windows Vista or later. All 32-Bit and 64-Bit editions, including ARM64, are supported.

## Website

Git mirrors for this project:

* <https://github.com/dEajL3kA/tee-win32>
* <https://gitlab.com/deajl3ka1/tee-for-windows>
* <https://repo.or.cz/tee-win32.git>

## License

Copyright (c) 2023 “dEajL3kA” &lt;Cumpoing79@web.de&gt;  
This work has been released under the MIT license. See [LICENSE.txt](LICENSE.txt) for details!

### Acknowledgement

Using [T-junction icons](https://www.flaticon.com/free-icons/t-junction) created by Smashicons &ndash; Flaticon.
