/* Compile-time fixture for the Win64 structure offsets rt/win.inc and
   rt/winposix.c hard-code.  The runtime is freestanding and cannot include
   windows.h, so its VEH pokes EXCEPTION_RECORD and CONTEXT at fixed byte
   offsets and its WASI host declares the file-information structures by
   hand; this file checks each one against the real SDK headers.  There is
   nothing to run -- compiling it is the test. */
#include <windows.h>
#include <winternl.h>
#include <stddef.h>

_Static_assert(offsetof(EXCEPTION_POINTERS, ExceptionRecord) == 0x00,
               "VEH info[0] is not the EXCEPTION_RECORD pointer");
_Static_assert(offsetof(EXCEPTION_POINTERS, ContextRecord) == 0x08,
               "VEH info[1] is not the CONTEXT pointer");
_Static_assert(offsetof(EXCEPTION_RECORD, ExceptionCode) == 0x00,
               "ExceptionCode moved");
_Static_assert(offsetof(EXCEPTION_RECORD, ExceptionInformation) == 0x20,
               "ExceptionInformation moved (fault VA read at +0x28)");
_Static_assert(offsetof(CONTEXT, Rcx) == 0x80, "CONTEXT.Rcx moved");
_Static_assert(offsetof(CONTEXT, Rsp) == 0x98, "CONTEXT.Rsp moved");
_Static_assert(offsetof(CONTEXT, Rip) == 0xF8, "CONTEXT.Rip moved");
_Static_assert(EXCEPTION_ACCESS_VIOLATION == 0xC0000005u,
               "WIN_EXC_ACCESS_VIOLATION mismatch");

/* rt/winposix.c: HandleInfo */
_Static_assert(sizeof(BY_HANDLE_FILE_INFORMATION) == 52, "HandleInfo size");
_Static_assert(offsetof(BY_HANDLE_FILE_INFORMATION, dwFileAttributes) == 0, "attr");
_Static_assert(offsetof(BY_HANDLE_FILE_INFORMATION, ftCreationTime) == 4, "ctime");
_Static_assert(offsetof(BY_HANDLE_FILE_INFORMATION, ftLastAccessTime) == 12, "atime");
_Static_assert(offsetof(BY_HANDLE_FILE_INFORMATION, ftLastWriteTime) == 20, "wtime");
_Static_assert(offsetof(BY_HANDLE_FILE_INFORMATION, dwVolumeSerialNumber) == 28, "volser");
_Static_assert(offsetof(BY_HANDLE_FILE_INFORMATION, nFileSizeHigh) == 32, "size_hi");
_Static_assert(offsetof(BY_HANDLE_FILE_INFORMATION, nFileSizeLow) == 36, "size_lo");
_Static_assert(offsetof(BY_HANDLE_FILE_INFORMATION, nNumberOfLinks) == 40, "nlinks");
_Static_assert(offsetof(BY_HANDLE_FILE_INFORMATION, nFileIndexHigh) == 44, "idx_hi");
_Static_assert(offsetof(BY_HANDLE_FILE_INFORMATION, nFileIndexLow) == 48, "idx_lo");

/* rt/winposix.c: TagInfo */
_Static_assert(offsetof(FILE_ATTRIBUTE_TAG_INFO, FileAttributes) == 0, "tag attr");
_Static_assert(offsetof(FILE_ATTRIBUTE_TAG_INFO, ReparseTag) == 4, "tag tag");

/* rt/winposix.c: DirInfo, the one the directory walk reads entry by entry */
_Static_assert(offsetof(FILE_ID_BOTH_DIR_INFO, NextEntryOffset) == 0, "de next");
_Static_assert(offsetof(FILE_ID_BOTH_DIR_INFO, FileIndex) == 4, "de index");
_Static_assert(offsetof(FILE_ID_BOTH_DIR_INFO, CreationTime) == 8, "de ctime");
_Static_assert(offsetof(FILE_ID_BOTH_DIR_INFO, LastAccessTime) == 16, "de atime");
_Static_assert(offsetof(FILE_ID_BOTH_DIR_INFO, LastWriteTime) == 24, "de wtime");
_Static_assert(offsetof(FILE_ID_BOTH_DIR_INFO, ChangeTime) == 32, "de chtime");
_Static_assert(offsetof(FILE_ID_BOTH_DIR_INFO, EndOfFile) == 40, "de eof");
_Static_assert(offsetof(FILE_ID_BOTH_DIR_INFO, AllocationSize) == 48, "de alloc");
_Static_assert(offsetof(FILE_ID_BOTH_DIR_INFO, FileAttributes) == 56, "de attr");
_Static_assert(offsetof(FILE_ID_BOTH_DIR_INFO, FileNameLength) == 60, "de namelen");
_Static_assert(offsetof(FILE_ID_BOTH_DIR_INFO, EaSize) == 64, "de easize");
_Static_assert(offsetof(FILE_ID_BOTH_DIR_INFO, ShortNameLength) == 68, "de shortlen");
_Static_assert(offsetof(FILE_ID_BOTH_DIR_INFO, ShortName) == 70, "de shortname");
_Static_assert(offsetof(FILE_ID_BOTH_DIR_INFO, FileId) == 96, "de fileid");
_Static_assert(offsetof(FILE_ID_BOTH_DIR_INFO, FileName) == 104, "de name");
_Static_assert(FileIdBothDirectoryInfo == 10, "FileIdBothDirectoryInfo class");
_Static_assert(FileIdBothDirectoryRestartInfo == 11, "restart class");
_Static_assert(FileAttributeTagInfo == 9, "FileAttributeTagInfo class");

/* rt/winposix.c: ReparseLink, the symbolic-link shape of REPARSE_DATA_BUFFER.
   winternl.h names it REPARSE_DATA_BUFFER only in the DDK, so the offsets are
   spelled out against the documented layout instead. */
_Static_assert(FSCTL_GET_REPARSE_POINT == 0x000900A8u, "FSCTL_GET_REPARSE_POINT");
_Static_assert(IO_REPARSE_TAG_SYMLINK == 0xA000000Cu, "TAG_SYMLINK");
_Static_assert(IO_REPARSE_TAG_MOUNT_POINT == 0xA0000003u, "TAG_MOUNT_POINT");

int main(void) { return 0; }
