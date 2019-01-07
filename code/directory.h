#ifndef _directory_h_
#define _directory_h_

#define internal static

struct Directory
{
    b32 found;
    char* name;
    HANDLE _findHandle;
    WIN32_FIND_DATA data;
};

inline b32 isThisOrParentDir(Directory* d)
{
    return d->name[0] == '.' && (d->name[1] == '\0' || (d->name[1] == '.' && d->name[2] == '\0'));
}

inline b32 isDir(Directory* d)
{
    return (d->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}

inline void dnext(Directory* d)
{
    d->found = FindNextFile(d->_findHandle, &d->data);
}

inline void dnextFile(Directory* d)
{
    do
    {
        dnext(d);
    }
    while (d->found && isDir(d));
}

inline void dnextDir(Directory* d)
{
    do
    {
        dnext(d);
    }
    while (d->found && !isDir(d));
}

internal void dfind_(Directory* d, char* searchPath)
{
    d->name = d->data.cFileName;
    d->_findHandle = FindFirstFileA(searchPath, &d->data);
    d->found = (d->_findHandle != INVALID_HANDLE_VALUE);
}

internal void dfind(Directory* d, char* searchPath)
{
    dfind_(d, searchPath);
    while(d->found && isDir(d) && isThisOrParentDir(d))
    {
        dnext(d);
    }
}

internal void dfindDir(Directory* d, char* searchPath)
{
    dfind_(d, searchPath);
    while(d->found && (!isDir(d) || isThisOrParentDir(d)))
    {
        dnext(d);
    }
}

internal void dfindFile(Directory* d, char* searchPath)
{
    dfind_(d, searchPath);
    while(d->found && isDir(d))
    {
        dnext(d);
    }
}

inline void dclose(Directory* d)
{
    FindClose(d->_findHandle);
    ZeroStruct(*d);
}

inline bool isHidden(Directory* d)
{
	return (d->data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
}

inline u64 getFileSize(Directory* d)
{
	return ((u64)d->data.nFileSizeHigh * ((u64)MAXDWORD + 1)) + d->data.nFileSizeLow;
}

internal b32 PathFileExists(char* path)
{
    b32 result = false;
    WIN32_FIND_DATA ffd;
    HANDLE hFind = FindFirstFileA(path, &ffd);
    if (INVALID_HANDLE_VALUE != hFind)
    {
        result = true;
        FindClose(hFind);
    }
    return result;
}

internal b32 PathFileExists(MemoryArena& arena, String pathNonZero)
{
	TempMemory _temp(arena);
	String path = pushStringZeroTerminated(arena, pathNonZero);
	b32 result = PathFileExists(path.str);
	return result;
}

#endif
