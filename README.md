Ncloader
========

#A simple dll injection utility#
The current design only implements the well-known dll injection technique:
  - VirtualAllocEx (allocates memory for string in remote process)
  - WriteProcessMemory (writes the "path/to/dll/file" in remotely allocated memory)
  - CreateRemoteThread (with start address of LoadLibrary[A/W] and address to "path/do/dll/file" as parameter)

##Features##
  - From elevated admin prompt, injects in any (non-protected) process regardless of
    - User
    - Integrity level
    - Session
  - Standalone (no third-party library, statically compiled)
  - Simplicity (single c-code file)
  - Clean clode (compiles with no warnings and /Wall on MSVC)
  - Strict error checking and verbose reporting
  - No undocumented NT api
  - Not creating services
  - Not using driver
  - 32bit and 64bit pre-compiled binaries

###Usage###
```
ncloader.exe [process name | pid] [dll full path]
```

###Examples###
By process name from regular prompt (debug privilege not present in restricted token)
```
ncloader.exe notepad.exe c:\path\to\library.dll
Dll c:\path\to\library.dll successfully injected in process 1234
```
By PID from elevated prompt (token has debug privilege present but disabled)
```
ncloader.exe 1234 c:\path\to\library.dll
Dll c:\path\to\library.dll successfully injected in process 1234 (debug privilege was enabled)
```
