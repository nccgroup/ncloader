// ncloader.cpp: A dll loading utility
// Nicolas Guigo

#pragma warning( disable : 4711) // disable informational warning. Leaving inlining up to compiler.
// #pragma warning( disable : 4191) /* uncomment to compile as c++ with /Wall and no warnings */

#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <Windows.h>
#include <Psapi.h>

#ifdef UNICODE
#define LOADLIBRARY "LoadLibraryW"
#elif
#define LOADLIBRARY "LoadLibraryA"
#endif
#define TIMEOUT_10SEC 10000
#define QUITE_LARGE_NB_PROCESSES 256

void usage(LPTSTR path)
{
  TCHAR exename[_MAX_FNAME+1];
  _tsplitpath_s(path, (LPTSTR)NULL, 0, (LPTSTR)NULL, 0, (LPTSTR)&exename, _MAX_FNAME+1, (LPTSTR)NULL, 0);
  _tprintf(L"%s [process name | pid] [dll full path]\n", exename);
  return;
}

// Either returns true (for a retry) or false (success or failure)
// Failure: pnbProcesses is 0 and there is no buffer to free
// Success: pnbProcesses is greater than 0 and *pprocesses contains a pointer to be freed
BOOL FillProcessesListWithAlloc(PDWORD *pprocesses, DWORD size, PDWORD pnbProcesses)
{
  DWORD *processes, bytes=0, result=0;
  BOOL retry=FALSE, realloc=FALSE;

  // Attempt allocation or reallocation
  if(!(*pprocesses)) {
    processes = (PDWORD)HeapAlloc(GetProcessHeap(), 0, size);
  }
  else {
    processes = (PDWORD)HeapReAlloc(GetProcessHeap(), 0, *pprocesses, size);
    realloc=TRUE;
  }
  // If allocation for requested size succeeded
  if(processes) {
    if(EnumProcesses(processes, size, &bytes)) {
      // Success
      if(bytes<size) {
        result = bytes/sizeof(DWORD);
      }
      else {
        // Buffer too small to list all processIDs
        retry=TRUE;
      }
      // Writes the allocation pointer back in case of success or retry
      *pprocesses = processes;
    }
    else {
      HeapFree(GetProcessHeap(), 0, processes);
      _tprintf(L"EnumProcesses() failure, error %#.8x\n", GetLastError());
    }
  } // if processes
  else {
     // Allocation failure handling
    _tprintf(L"Allocation failure (requested %#.8x bytes), aborting\n", size);
    // If realloc failed, a free is necessary
    if(realloc) {
      HeapFree(GetProcessHeap(), 0, *pprocesses);
    }
  }
  // Write back nb of processe only if we are done
  if(!retry) {
    *pnbProcesses = result;
  }
  return retry;
}

// Attemps to fill the stack buffer if large enough, otherwise move on to allocations
DWORD FillProcessesList(PDWORD *pprocesses, DWORD bufsize)
{
  DWORD nb_processes=0, bytes, size=bufsize;
  BOOL retry;

  // First attemps on stack buffer
  if(EnumProcesses(*pprocesses, size, &bytes)) {
    if(bytes>=size) {
      // Not large enough, allocating
      *pprocesses=NULL;
      do {
        size *= 2;    // doubling size of buffer for processIDs list
        retry =  FillProcessesListWithAlloc(pprocesses, size, &nb_processes);
      }
      while(retry);
    }
    else {
      nb_processes = bytes/sizeof(DWORD);
    }
  } // if enumProcesses
  else {
    _tprintf(L"EnumProcesses failed with error %#.8x\n", GetLastError());
  }
  return nb_processes;
}

BOOL getProcessbyNameOrId(LPTSTR searchstring, PHANDLE phProcess)
{
  BOOL found=FALSE;
  HMODULE hMod;
  DWORD *processes, lpProcesses[QUITE_LARGE_NB_PROCESSES], bytes, processId;
  SIZE_T nbProcesses, i;
  HANDLE hProcess;
  TCHAR processname[MAX_PATH+1], *stop;

  processId = _tcstoul(searchstring, &stop, 0);
  if(processId && *stop==L'\0') {
    hProcess = OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|PROCESS_VM_READ|PROCESS_VM_WRITE, FALSE, processId);
    if(hProcess) {
      *phProcess = hProcess;
      found=TRUE;
    }
    else {
      _tprintf(L"Failed to get handle to process %#.8x, error %#.8x\n", processId, GetLastError());
    }
  }
  else {
    processes = lpProcesses;
    nbProcesses = FillProcessesList(&processes, sizeof(lpProcesses));
    if(nbProcesses) {
      for(i=0; i<nbProcesses && !found; i++) {
        hProcess = OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|PROCESS_VM_READ|PROCESS_VM_WRITE, FALSE, processes[i]);
        if(hProcess) {
          if(EnumProcessModules(hProcess, &hMod, sizeof(hMod), &bytes)) {
            if(GetModuleBaseName(hProcess, hMod, processname, sizeof(processname)/sizeof(TCHAR))) {
              if(!_tcsicmp(searchstring, processname)) {
                *phProcess = hProcess;
                found=TRUE;
              } // if _tcsicmp
            } // if GetModuleBaseName
          } // if EnumProcessModules
          if(!found) {
            // only close this process handle if it is not the one we are looking for
            CloseHandle(hProcess);
          }
        } // if hProcess
      } // for all processes
      if(processes!=lpProcesses) {
        HeapFree(GetProcessHeap(), 0, processes);
      }
      if(!found) {
        _tprintf(L"Failed to get handle with sufficient permissions to process %s, verify process existence, DACL and x86/x64 mistmatch\n", searchstring);
      }
    } // if nbProcesses
  }
  return found;
}

int _tmain(int argc, _TCHAR* argv[])
{
  BOOL bResult;
  LPVOID pMem=NULL;
  HANDLE hProcess=NULL, hThread=NULL;
  DWORD threadId, waitResult, threadExitCode;
  SIZE_T sizerequired, dllpathlen, byteswritten;
  LPTHREAD_START_ROUTINE pLoadLibrary;

  // Check argc
  if(argc==3) {
    // Find the FIRST process by that name
    if(getProcessbyNameOrId(argv[1], &hProcess)) {
      // TODO: add optional check for file presence and permissions
      // Get required size, including terminating character
      dllpathlen = _tcsnlen(argv[2], MAX_PATH);
      if(dllpathlen<MAX_PATH) {
          sizerequired = sizeof(TCHAR)*(dllpathlen+1);
          // Allocate a page in the target process
          pMem = VirtualAllocEx(hProcess, 0x0, sizerequired, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
          if(pMem) {
              // Copy dll path to target process
              bResult = WriteProcessMemory(hProcess, pMem, argv[2], sizerequired, &byteswritten);
              if(bResult) {
                  // Get address to LoadLibrary function
                  pLoadLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandle(L"kernel32.dll"), LOADLIBRARY);
                  if(pLoadLibrary) {
                      // Create remote thread pointing to LoadLibrary[A|W]
                      hThread = CreateRemoteThread(hProcess, NULL, 0, pLoadLibrary, pMem, 0, &threadId);
                      if(hThread) {
                          waitResult = WaitForSingleObject(hThread, TIMEOUT_10SEC);
                          if(waitResult==WAIT_OBJECT_0) {
                              bResult = GetExitCodeThread(hThread, &threadExitCode);
                              if(bResult && threadExitCode) {
                                  _tprintf(L"Dll %s successfully injected in process %u\n", argv[2], GetProcessId(hProcess));
                              }
                              else {
                                  if(bResult) {
                                      _tprintf(L"LoadLibrary failed, check for x86/x64 mismatch\n");
                                  }
                                  else {
                                      _tprintf(L"Could not check LoadLibrary return value in remote thread, error %#.8x\n", GetLastError());
                                  }
                              }
                          } // if waitResult==WAIT_OBJECT_0
                          else {
                              _tprintf(L"Aborting: %s\n", waitResult==WAIT_TIMEOUT ? L"remote thread has been hung for 10 secs" : L"wait failed");
                          }
                          CloseHandle(hThread);
                      } // if hThread
                      else {
                          _tprintf(L"Creating remote thread in process %u failed with error %#.8x\n", GetProcessId(hProcess), GetLastError());
                      }
                  } // if pLoadLibrary
                  else {
                      _tprintf(L"Failed to get LoadLibrary function address with error %#.8x\n", GetLastError());
                  }
              } // if bResult
              else {
                  _tprintf(L"Writing remote process %u memory failed with error %#.8x\n", GetProcessId(hProcess), GetLastError());
              }
              if(!VirtualFreeEx(hProcess, pMem, 0, MEM_RELEASE)) {
                  _tprintf(L"Failed to free remote process' allocated memory, error %#.8x\n", GetLastError());
              }
          } // if pMem
          else {
              _tprintf(L"Remote process %u allocation failed with error %#.8x\n", GetProcessId(hProcess), GetLastError());
          }
      } // if pathlen valid
      else {
        _tprintf(L"Dll path too long\n");
      }
      CloseHandle(hProcess);
    } // if getProcessbyNameOrId
  } // if argc==3
  else {
      usage(argv[0]);
  }
  return 0;
}
